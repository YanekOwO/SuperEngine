// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 RLVM contributors
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
// -----------------------------------------------------------------------

#include "libsiglus/save_load.hpp"
#include "core/memory_internal/proxy.hpp"
#include "libsiglus/bindings/memory.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/bindings/util.hpp"
#include "platforms/implementor.hpp"
#include "srbind/srbind.hpp"
#include "vm/list.hpp"
#include "vm/object.hpp"
#include "vm/string.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace libsiglus::binding {
namespace sr = serilang;
namespace sb = srbind;
namespace {
using Source = SiglusSaveLoad::Source;

void WriteReference(SiglusMachine& machine,
                    sr::Fiber& fiber,
                    const sr::Value& value,
                    int result) {
  const auto* ref = RequireList(value, "integer output reference");
  if (ref->items.size() != 3)
    throw sr::RuntimeError("invalid integer output reference");
  const int kind = RequireInt(ref->items[0], "reference kind");
  const int index =
      kind == 1 ? 0 : RequireInt(ref->items[2], "reference index");
  if (kind == 2) {
    machine.host()
        .properties.Get(RequireInt(ref->items[1], "property scene"), index)
        .value = result;
  } else if (kind == 0) {
    if (auto* list = IntListData(machine, fiber, ref->items[1])) {
      if (index < 0)
        throw sr::RuntimeError("negative integer output index");
      IntListProxy(*list).Set(index, result);
    } else if (auto* list = ref->items[1].Get_if<sr::List>()) {
      const_cast<sr::List*>(list)->items.at(index) = result;
    } else if (const auto* native = ref->items[1].Get_if<sr::NativeInstance>();
               native &&
               native->klass == machine.class_registry.GetClass<IntBitView>()
                                    .native_class()) {
      auto* bits =
          const_cast<sr::NativeInstance*>(native)->GetForeign<IntBitView>();
      bits->Set(fiber, index, result);
    } else {
      throw sr::RuntimeError("integer output reference is not writable");
    }
  } else if (kind == 1) {
    auto object = ref->items[1];
    object.SetMember(ref->items[2].Str(), result);
  } else {
    throw sr::RuntimeError("invalid integer output reference kind");
  }
}
}  // namespace

void BindSavepoint(SiglusMachine& machine) {
  sb::module_ m(machine.vm->gc_.get(), machine.vm->globals_.get());
  m.def("savepoint", [&machine](sr::Fiber& fiber) {
    machine.host().Saves().RequestCheckpoint(machine, fiber);
    return 0;
  });
  m.def("clear_savepoint",
        [&machine] { machine.host().Saves().ClearCheckpoint(); });
  m.def("check_savepoint", [&machine] {
    return machine.host().Saves().manager().HasCheckpoint() ? 1 : 0;
  });
  m.def("get_title", [&machine] { return machine.host().scene_title; });
  m.def("owari", [&machine] {
    machine.host().Saves().RequestQuit();
    machine.vm->RequestHostYield();
  });
  m.def(
      "returnmenu",
      [&machine](std::vector<sr::Value> raw) {
        auto args = CallPacket::DecodeFrom(std::move(raw)).args;
        auto& host = machine.host();
        if (args.size() > 2)
          throw sr::RuntimeError(
              "returnmenu expects zero, one or two arguments");
        if (!host.gameexe && args.empty())
          throw sr::RuntimeError("returnmenu requires MENU_SCENE");
        std::string scene =
            args.empty()
                ? (*host.gameexe)("MENU_SCENE").StrAt(0).value_or("_menu")
                : args[0].Str();
        const int entry =
            args.size() > 1 ? RequireInt(args[1], "returnmenu entry")
            : args.empty()  ? (*host.gameexe)("MENU_SCENE").IntAt(1).value_or(0)
                            : 0;
        if (!args.empty() && host.platform_implementor &&
            !host.platform_implementor->AskUserPrompt("Restart the game?", "",
                                                      "Yes", "No"))
          return;
        machine.loader->ResolveSceneEntry(scene, entry);
        if (!host.Saves().FlushGlobal())
          throw sr::RuntimeError(
              "Could not persist global state before returning to menu");
        host.QueueReturnToMenu(std::move(scene), entry, false, 0);
        machine.vm->RequestHostYield();
      },
      sb::vararg);
}

void BindSaveLoad(SiglusMachine& machine) {
  auto& host = machine.host();
  auto& vm = *machine.vm;
  auto entry = vm.builtins_->try_emplace("syscom", sr::nil).first;
  auto* syscom = entry->second.Get_if<sr::Module>();
  if (!syscom) {
    syscom = vm.gc_->Allocate<sr::Module>("syscom");
    entry->second = syscom;
  }
  // Persistence extends the system module; constructing another named module
  // would discard its existing configuration and sound bindings.
  sb::module_ m(vm.gc_.get(), syscom->globals.get());
  for (const auto& [name, source] :
       {std::pair{"save", Source::Disk}, {"qsave", Source::Quick}}) {
    m.def(name, [&host, source](int slot, int warning, int sound) {
      return host.Saves().Save(slot, warning != 0, sound != 0, source) ? 1 : 0;
    });
  }
  for (const auto& [name, source] :
       {std::pair{"load", Source::Disk}, {"qload", Source::Quick}}) {
    m.def(name, [&machine, source](int slot, int warning, int sound, int fade) {
      machine.host().Saves().Load(machine, slot, warning != 0, sound != 0,
                                  fade != 0, source);
    });
  }
  m.def("endsave", [&host](int warning, int sound) {
    return host.Saves().Save(0, warning != 0, sound != 0, Source::End) ? 1 : 0;
  });
  m.def("endload", [&machine](int warning, int sound, int fade) {
    machine.host().Saves().Load(machine, 0, warning != 0, sound != 0, fade != 0,
                                Source::End);
  });
  m.def("inner_save", [&host](int slot) {
    host.Saves().manager().SaveInner(slot);
    return 1;
  });
  m.def("clear_inner_save", [&host](int slot) {
    host.Saves().manager().ClearInner(slot);
    return 1;
  });
  m.def("copy_inner_save", [&host](int from, int to) {
    host.Saves().manager().CopyInner(from, to);
    return 1;
  });
  m.def("check_inner_save", [&host](int slot) {
    return host.Saves().manager().HasInner(slot) ? 1 : 0;
  });
  m.def("inner_load", [&machine](int slot, int warning, int sound, int fade) {
    return machine.host().Saves().Load(machine, slot, warning != 0, sound != 0,
                                       fade != 0, Source::Inner)
               ? 1
               : 0;
  });
  for (const auto& [name, source] :
       {std::pair{"return_to_sel", Source::Selection},
        {"msgbk_load", Source::Backlog}}) {
    m.def(name, [&machine, source](int warning, int sound, int fade) {
      machine.host().Saves().Load(machine, 0, warning != 0, sound != 0,
                                  fade != 0, source);
    });
  }
  m.def("open_save", [&host] { host.Saves().OpenMenu(true); });
  m.def("open_load", [&host] { host.Saves().OpenMenu(false); });
  m.def("get_save_count",
        [&host] { return host.Saves().manager().counts().normal; });
  m.def("get_qsave_count",
        [&host] { return host.Saves().manager().counts().quick; });
  for (bool quick : {false, true}) {
    const std::string prefix = quick ? "qsave" : "save";
    const auto translate = [&host, quick](int slot) {
      auto& manager = host.Saves().manager();
      return (quick ? manager.QuickSlot(slot) : manager.NormalSlot(slot))
          .value_or(-1);
    };
    m.def(
        "get_new_" + prefix + "_no",
        [&host, quick](std::vector<sr::Value> raw) {
          const auto args = CallPacket::DecodeFrom(std::move(raw)).args;
          if (args.empty())
            return host.Saves().manager().Newest(quick);
          if (args.size() != 2)
            throw sr::RuntimeError(
                "newest-save lookup expects zero or two arguments");
          return host.Saves().manager().Newest(
              RequireInt(args[0], "save start"),
              RequireInt(args[1], "save count"), quick);
        },
        sb::vararg);
    m.def("is_" + prefix + "_exist", [&host, translate](int slot) {
      return host.Saves().manager().Exists(translate(slot)) ? 1 : 0;
    });
    const auto header = [&host, translate](int slot) {
      return host.Saves()
          .manager()
          .ReadHeader(translate(slot))
          .value_or(SaveHeader{});
    };
    const auto integer = [&](std::string_view name, int SaveHeader::* field) {
      m.def("get_" + prefix + "_" + std::string(name),
            [header, field](int slot) { return header(slot).*field; });
    };
    integer("year", &SaveHeader::year);
    integer("month", &SaveHeader::month);
    integer("day", &SaveHeader::day);
    integer("weekday", &SaveHeader::weekday);
    integer("hour", &SaveHeader::hour);
    integer("minute", &SaveHeader::minute);
    integer("second", &SaveHeader::second);
    integer("millisecond", &SaveHeader::millisecond);
    const auto string = [&](std::string_view name,
                            std::string SaveHeader::* field) {
      m.def("get_" + prefix + "_" + std::string(name),
            [header, field](int slot) { return header(slot).*field; });
    };
    string("title", &SaveHeader::title);
    string("message", &SaveHeader::message);
    string("full_message", &SaveHeader::full_message);
    string("comment", &SaveHeader::comment);
    string("append_dir", &SaveHeader::append_dir);
    string("append_name", &SaveHeader::append_name);
    m.def("set_" + prefix + "_comment",
          [&host, translate](int slot, std::string text) {
            host.Saves().manager().SetComment(translate(slot), text);
          });
    m.def("get_" + prefix + "_value",
          [&machine, translate](sr::Fiber& fiber, int slot,
                                sr::Value destination, int start, int count) {
            auto* list = IntListData(machine, fiber, destination);
            const auto header =
                machine.host().Saves().manager().ReadHeader(translate(slot));
            if (!list || !header || count <= 0 || start < 0)
              return;
            for (int i = 0; i < std::min(count, 256); ++i)
              IntListProxy(*list).Set(static_cast<std::size_t>(start) + i,
                                      header->values[i]);
          });
    m.def("set_" + prefix + "_value",
          [&machine, translate](sr::Fiber& fiber, int slot, sr::Value source,
                                int start, int count) {
            auto* list = IntListData(machine, fiber, source);
            if (!list || count <= 0 || start < 0)
              return;
            std::vector<int> values;
            for (int i = 0; i < std::min(count, 256); ++i)
              values.push_back(
                  IntListProxy(*list).Get(static_cast<std::size_t>(start) + i));
            machine.host().Saves().manager().SetValues(translate(slot), values);
          });
    m.def("copy_" + prefix, [&host, translate](int from, int to) {
      return host.Saves().manager().CopySlot(translate(from), translate(to))
                 ? 1
                 : 0;
    });
    m.def("change_" + prefix, [&host, translate](int first, int second) {
      return host.Saves().manager().SwapSlots(translate(first),
                                              translate(second))
                 ? 1
                 : 0;
    });
    m.def("delete_" + prefix, [&host, translate](int slot) {
      return host.Saves().manager().DeleteSlot(translate(slot)) ? 1 : 0;
    });
  }
  m.def("is_endsave_exist", [&host](int offset) {
    const auto slot = host.Saves().manager().EndSlot(offset);
    return slot && host.Saves().manager().Exists(*slot) ? 1 : 0;
  });
  m.def("get_current_save_title",
        [&host] { return host.Saves().manager().active().title; });
  m.def("get_current_save_message",
        [&host] { return host.Saves().manager().active().message; });
  m.def("is_save_enable", [&host] {
    const auto& entry = host.local_config.syscommenu.save;
    return entry.exist && entry.enable && host.Saves().manager().HasCheckpoint()
               ? 1
               : 0;
  });
  m.def("is_retsel_enable", [&host] {
    const auto& entry = host.local_config.syscommenu.return_to_sel;
    return entry.exist && entry.enable &&
                   host.Saves().manager().selection_points().HasPoint()
               ? 1
               : 0;
  });
  m.def("get_total_playtime",
        [&machine](sr::Fiber& fiber, sr::Value day, sr::Value hour,
                   sr::Value minute, sr::Value second, sr::Value millisecond) {
          auto value = machine.host().total_play_time;
          std::array<sr::Value, 5> outputs{day, hour, minute, second,
                                           millisecond};
          constexpr std::array<std::int64_t, 5> units{86400000, 3600000, 60000,
                                                      1000, 1};
          for (std::size_t i = 0; i < outputs.size(); ++i) {
            WriteReference(machine, fiber, outputs[i],
                           static_cast<int>(value / units[i]));
            value %= units[i];
          }
        });
  m.def("set_total_playtime", [&host](int day, int hour, int minute, int second,
                                      int millisecond) {
    host.total_play_time =
        (((static_cast<std::int64_t>(day) * 24 + hour) * 60 + minute) * 60 +
         second) *
            1000 +
        millisecond;
  });
}

RLVM_REGISTER(SiglusBindingRegistry, "0_savepoint", BindSavepoint)
// Extend syscom after its configuration bindings create the module.
RLVM_REGISTER(SiglusBindingRegistry, "5_save_load", BindSaveLoad)

}  // namespace libsiglus::binding
