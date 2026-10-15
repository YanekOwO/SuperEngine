// -----------------------------------------------------------------------
//
// This file is part of RLVM
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2026 Serina Sakurai
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

#include "libsiglus/bindings/registry.hpp"

#include "core/avdec/gan.hpp"
#include "core/colour.hpp"
#include "core/frame_counter.hpp"
#include "core/memory_internal/facade.hpp"
#include "core/object.hpp"
#include "core/object_internal/drawer/colour_filter.hpp"
#include "core/object_internal/drawer/file.hpp"
#include "core/object_internal/drawer/gan.hpp"
#include "core/object_internal/drawer/movie.hpp"
#include "core/object_internal/objdrawer.hpp"
#include "core/object_internal/object_mutator.hpp"
#include "core/object_internal/object_parameter.hpp"
#include "core/stage.hpp"
#include "libsiglus/bindings/frame_action.hpp"
#include "libsiglus/bindings/loader.hpp"
#include "libsiglus/bindings/util.hpp"
#include "libsiglus/bindings/wait_helpers.hpp"
#include "libsiglus/siglus_machine.hpp"
#include "srbind/srbind.hpp"
#include "systems/event_system.hpp"
#include "systems/graphics_system.hpp"
#include "systems/system.hpp"
#include "utilities/file.hpp"
#include "vm/exception.hpp"
#include "vm/object.hpp"
#include "vm/value.hpp"
#include "vm/vm.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace libsiglus::binding {
namespace sb = srbind;
namespace sr = serilang;

struct ObjectHandle {
  std::shared_ptr<GraphicsObject> object;
};

struct ObjectListHandle {
  int layer;
};

struct ObjectChildListHandle {
  std::shared_ptr<GraphicsObject> object;
};

struct ObjectEventHandle {
  std::shared_ptr<GraphicsObject> object;
  std::string name;
  std::function<int(const ObjectParameter&)> getter;
  std::function<void(ObjectParameter&, int)> setter;
};

struct ObjectRepnoEventHandle {
  using Getter = std::function<int(const ObjectParameter&, int)>;
  using Setter = std::function<void(ObjectParameter&, int, int)>;

  std::shared_ptr<GraphicsObject> object;
  int repno;
  std::string name;
  Getter getter;
  Setter setter;
};

struct ObjectRepnoEventListHandle {
  std::shared_ptr<GraphicsObject> object;
  std::string name;
  ObjectRepnoEventHandle::Getter getter;
  ObjectRepnoEventHandle::Setter setter;
};

constexpr int kBtnNormal = static_cast<int>(ButtonState::Normal);
constexpr int kBtnSelect = static_cast<int>(ButtonState::Select);
constexpr int kBtnDisable = static_cast<int>(ButtonState::Disable);

struct MovieCreateParams {
  std::string file_name;
  std::optional<int> display;
  std::optional<int> x;
  std::optional<int> y;
  bool loop = false;
  bool wait = false;
  bool key_skip = false;
  bool auto_free = true;
  bool real_time = true;
  bool ready_only = false;
};

void ApplyObjectFileSuffix(std::string& filename, ObjectParameter& param) {
  const std::size_t pos = filename.find('?');
  if (pos == std::string::npos)
    return;

  const std::string tone_curve = filename.substr(pos + 1);
  filename.erase(pos);

  try {
    std::size_t parsed = 0;
    int value = std::stoi(tone_curve, &parsed);
    if (parsed == tone_curve.size() && value > 0)
      param.tonecurve_no = value;
  } catch (const std::exception&) {
  }
}

void SetClipRectValue(ObjectParameter& param,
                      void (Rect::*setter)(int),
                      int value) {
  Rect rect = param.has_clip_rect() ? param.clip_rect() : Rect::GRP(0, 0, 0, 0);
  (rect.*setter)(value);
  param.SetClipRect(rect);
}

void SetOwnClipRectValue(ObjectParameter& param,
                         void (Rect::*setter)(int),
                         int value) {
  Rect rect =
      param.has_own_clip_rect() ? param.own_clip_rect() : Rect::GRP(0, 0, 0, 0);
  (rect.*setter)(value);
  param.SetOwnClipRect(rect);
}

void ReplaceFileDrawer(GraphicsObject& object,
                       GraphicsSystem* graphics,
                       std::string filename) {
  ObjectParameter params = object.Param();
  ApplyObjectFileSuffix(filename, params);
  if (filename.empty())
    throw std::runtime_error("Object filename is empty");

  std::unique_ptr<GraphicsObjectData> drawer;
  if (IsCompositeObjectName(filename)) {
    std::vector<CompositeGraphicsObjectLayer> layers;
    for (const CompositeObjectPart& part : ParseCompositeObjectName(filename)) {
      layers.push_back({.surface = graphics->GetSurfaceNamed(part.file_name),
                        .offset = Point(part.x, part.y),
                        .cut_no = part.cut_no,
                        .blend_type = part.blend_type});
    }
    drawer = std::make_unique<CompositeGraphicsObject>(std::move(layers));
  } else {
    auto surface = graphics->GetSurfaceNamed(filename);
    drawer = std::make_unique<GraphicsObjectOfFile>(surface);
  }

  object.Param() = std::move(params);
  object.SetDrawer(std::move(drawer));
  object.SetFilePath(std::move(filename));
}

void CreateObject(GraphicsObject& object,
                  System* system,
                  std::string filename) {
  GraphicsSystem* graphics = system ? system->graphics_ptr().get() : nullptr;
  if (!graphics)
    throw std::runtime_error("Object.create requires a graphics system");

  if (filename.empty())
    throw std::runtime_error("Object.create filename is empty");

  object.Reset();
  ReplaceFileDrawer(object, graphics, std::move(filename));
}

void ChangeObjectFile(GraphicsObject& object,
                      System* system,
                      std::string filename) {
  GraphicsSystem* graphics = system ? system->graphics_ptr().get() : nullptr;
  if (!graphics)
    throw std::runtime_error("Object.change_file requires a graphics system");
  if (filename.empty())
    throw std::runtime_error("Object.change_file filename is empty");

  if (!object.GetDrawer<GraphicsObjectOfFile>() &&
      !object.GetDrawer<CompositeGraphicsObject>()) {
    throw std::runtime_error(
        "Object.change_file requires a file-backed object");
  }

  ReplaceFileDrawer(object, graphics, std::move(filename));
}

void LoadGan(GraphicsObject& object,
             System* system,
             AssetScanner* asset_scanner,
             std::string filename) {
  GraphicsSystem* graphics = system ? system->graphics_ptr().get() : nullptr;
  EventSystem* event = system ? system->event_ptr().get() : nullptr;
  if (filename.empty())
    return;
  if (!graphics)
    throw std::runtime_error("Object.load_gan requires a graphics system");
  if (!asset_scanner)
    throw std::runtime_error("Object.load_gan requires an asset scanner");

  auto gan_path = asset_scanner->FindFile(filename, {"gan"});
  if (!gan_path) {
    throw std::runtime_error("Object.load_gan could not find " + filename +
                             ".gan: " + gan_path.error().what());
  }

  GanDecoder decoder(LoadFile(*gan_path));
  const std::string image_name =
      decoder.raw_file_name.empty() ? filename : decoder.raw_file_name;
  auto image = graphics->GetSurfaceNamed(image_name);
  auto data = std::make_unique<GanGraphicsObjectData>(
      image, std::move(decoder.animation_sets),
      event ? event->GetClock() : std::make_shared<Clock>());
  if (data->HasSet(0))
    data->PrimeSet(0);
  object.ResetChildren();
  object.SetDrawer(std::move(data));
}

void StartGan(GraphicsObject& object, std::vector<sr::Value> raw_args) {
  CallPacket packet = CallPacket::DecodeFrom(std::move(raw_args));
  const std::vector<sr::Value>& args = packet.args;
  if (args.size() > 3)
    throw std::runtime_error("Object.start_gan expects 0 to 3 args");

  int set_no = 0;
  bool loop = true;
  bool real_time = false;
  if (args.size() >= 1)
    set_no = RequireInt(args[0], "Object.start_gan set");
  if (args.size() >= 2)
    loop = RequireInt(args[1], "Object.start_gan loop") != 0;
  if (args.size() >= 3)
    real_time = RequireInt(args[2], "Object.start_gan real_time") != 0;
  (void)real_time;

  if (auto* data = object.GetDrawer<GanGraphicsObjectData>()) {
    data->PlaySet(set_no);
    data->GetAnimator()->SetAfterAction(loop ? AFTER_LOOP : AFTER_NONE);
  }
}

MovieCreateParams ParseCreateMovie(std::vector<sr::Value> raw_args,
                                   bool loop,
                                   bool wait,
                                   bool key_skip) {
  CallPacket packet = CallPacket::DecodeFrom(std::move(raw_args));
  const std::vector<sr::Value>& args = packet.args;
  if (args.size() != 1 && args.size() != 2 && args.size() != 4) {
    throw std::runtime_error(
        "Object.create_movie expects 1, 2, or 4 positional args");
  }

  MovieCreateParams params;
  params.loop = loop;
  params.wait = wait;
  params.key_skip = key_skip;
  params.file_name = AsString(args[0]);
  if (params.file_name.empty())
    throw std::runtime_error("Object.create_movie filename is empty");

  if (args.size() >= 2)
    params.display = RequireInt(args[1], "disp");
  if (args.size() >= 4) {
    params.x = RequireInt(args[2], "x");
    params.y = RequireInt(args[3], "y");
  }

  ForEachKeywordId(packet.kwargs, [&](int id, const sr::Value& value) {
    switch (id) {
      case 0:
        params.auto_free = AsInt(value).value_or(0) != 0;
        break;
      case 1:
        params.real_time = AsInt(value).value_or(0) != 0;
        break;
      case 2:
        params.ready_only = AsInt(value).value_or(0) != 0;
        break;
      default:
        break;
    }
  });

  return params;
}

bool CreateMovie(GraphicsObject& object,
                 System* system,
                 AssetScanner* asset_scanner,
                 std::vector<sr::Value> raw_args,
                 bool loop,
                 bool wait,
                 bool key_skip) {
  GraphicsSystem* graphics = system ? system->graphics_ptr().get() : nullptr;
  EventSystem* event = system ? system->event_ptr().get() : nullptr;
  if (!graphics)
    throw std::runtime_error("Object.create_movie requires a graphics system");
  if (!asset_scanner)
    throw std::runtime_error("Object.create_movie requires an asset scanner");

  MovieCreateParams params =
      ParseCreateMovie(std::move(raw_args), loop, wait, key_skip);
  auto movie_path = asset_scanner->FindFile(params.file_name, {"omv"});
  if (!movie_path) {
    throw std::runtime_error("Object.create_movie could not find " +
                             params.file_name +
                             ".omv: " + movie_path.error().what());
  }

  object.Reset();
  object.SetDrawer(std::make_unique<ObjectMovieData>(
      movie_path.value(), params.loop, params.auto_free, params.real_time,
      params.ready_only, graphics->GetBackend(),
      event ? event->GetClock() : std::make_shared<Clock>()));
  object.SetFilePath(params.file_name);

  if (params.display)
    object.Param().SetVisible(*params.display);
  if (params.x)
    object.Param().SetX(*params.x);
  if (params.y)
    object.Param().SetY(*params.y);

  return params.wait && !params.ready_only;
}

void SeekMovie(GraphicsObject& object, std::vector<sr::Value> raw_args) {
  if (ObjectMovieData* data = object.GetDrawer<ObjectMovieData>()) {
    CallPacket packet = CallPacket::DecodeFrom(std::move(raw_args));
    if (!packet.args.empty())
      data->Seek(AsInt(packet.args[0]).value_or(0));
  }
}

void SetMovieAutoFree(GraphicsObject& object, std::vector<sr::Value> raw_args) {
  if (ObjectMovieData* data = object.GetDrawer<ObjectMovieData>()) {
    CallPacket packet = CallPacket::DecodeFrom(std::move(raw_args));
    if (!packet.args.empty())
      data->SetAutoFree(AsInt(packet.args[0]).value_or(0) != 0);
  }
}

sr::Value WaitMovie(std::shared_ptr<GraphicsObject> object,
                    System* system,
                    sr::VM& vm,
                    bool key_skip) {
  auto done = [object = std::move(object)] {
    const auto* data = object->GetDrawer<ObjectMovieData>();
    return !data || !data->CheckMovie();
  };
  return MakePollingWaitFuture(vm, std::move(done), key_skip,
                               system ? system->event_ptr().get() : nullptr);
}

int EventRepno(const ObjectEventHandle* h) { return -1; }
int EventRepno(const ObjectRepnoEventHandle* h) { return h->repno; }

int EventStartValue(const ObjectEventHandle* h) {
  return h->getter(h->object->Param());
}
int EventStartValue(const ObjectRepnoEventHandle* h) {
  return h->getter(h->object->Param(), h->repno);
}

std::function<void(ObjectParameter&, int)> EventSetter(
    const ObjectEventHandle* h) {
  return h->setter;
}
std::function<void(ObjectParameter&, int)> EventSetter(
    const ObjectRepnoEventHandle* h) {
  return
      [setter = h->setter, repno = h->repno](
          ObjectParameter& param, int value) { setter(param, repno, value); };
}

template <typename Handle>
void AddObjectEvent(Handle* h, FrameCounter counter) {
  h->object->AddObjectMutator(
      ObjectMutator(ObjectParameterMutator(EventSetter(h), std::move(counter)),
                    EventRepno(h), h->name));
}

template <typename Handle>
void BindObjectEvents(sb::class_<Handle>& event_class,
                      System* system,
                      SiglusMachine& machine) {
  for (const bool real_time : {false, true}) {
    auto make_counter = [&machine, real_time] {
      auto counter = machine.CreateCounter();
      if (real_time)
        counter->StartReal();
      else
        counter->StartGame();
      return counter;
    };

    auto set = [make_counter](Handle* h, int end_value, int duration_time,
                              int delay, int type) {
      h->object->EndObjectMutatorMatching(EventRepno(h), h->name, 0);
      const int start = EventStartValue(h);
      AddObjectEvent(h, MakeFrameCounter(duration_time, delay, start, end_value,
                                         type, make_counter()));
    };
    event_class.def(real_time ? "set_real" : "set", set, sb::arg("end_value"),
                    sb::arg("duration_time"), sb::arg("delay"),
                    sb::arg("type"));

    auto repeating = [make_counter](InterpolationType type) {
      return [make_counter, type](Handle* h, int start_value, int end_value,
                                  int loop_time, int delay, int speed_type) {
        h->object->EndObjectMutatorMatching(EventRepno(h), h->name, 0);
        AddObjectEvent(h, MakeRepeatingFrameCounter(
                              type, loop_time, delay, start_value, end_value,
                              speed_type, make_counter()));
      };
    };
    event_class.def(real_time ? "loop_real" : "loop",
                    repeating(InterpolationType::Loop), sb::arg("start_value"),
                    sb::arg("end_value"), sb::arg("loop_time"),
                    sb::arg("delay"), sb::arg("speed_type"));
    event_class.def(real_time ? "turn_real" : "turn",
                    repeating(InterpolationType::Turn), sb::arg("start_value"),
                    sb::arg("end_value"), sb::arg("loop_time"),
                    sb::arg("delay"), sb::arg("speed_type"));
  }

  event_class.def("end", [](Handle* h) {
    h->object->EndObjectMutatorMatching(EventRepno(h), h->name, 0);
  });
  event_class.def("check", [](Handle* h) -> int {
    return h->object->IsMutatorRunningMatching(EventRepno(h), h->name);
  });

  auto wait = [system](bool key_skip) {
    return [system, key_skip](Handle* h, sr::VM& vm, std::vector<sr::Value>) {
      auto done = [object = h->object, repno = EventRepno(h), name = h->name] {
        return !object->IsMutatorRunningMatching(repno, name);
      };
      return MakePollingWaitFuture(
          vm, std::move(done), key_skip,
          system ? system->event_ptr().get() : nullptr);
    };
  };
  event_class.def("wait", wait(false), sb::vararg);
  event_class.def("wait_key", wait(true), sb::vararg);
}

struct DirectObjectPropertyBinder {
  sb::class_<ObjectHandle>& object;

  template <auto member>
  void Member(const char* name) {
    object.prop(
        name,
        [](const ObjectHandle* h) {
          return static_cast<int>(h->object->Param().*member);
        },
        [](ObjectHandle* h, int value) {
          using MemberType = ObjectParameterMemberType<member>;
          h->object->Param().*member = static_cast<MemberType>(value);
        });
  }

  template <typename Getter, typename Setter>
  void Property(const char* name, Getter getter, Setter setter) {
    object.prop(
        name,
        [getter = std::move(getter)](const ObjectHandle* h) {
          return getter(h->object->Param());
        },
        [setter = std::move(setter)](ObjectHandle* h, int value) {
          setter(h->object->Param(), value);
        });
  }

  void Bind() {
    Member<&ObjectParameter::wipe_copy>("wipe_copy");
    Member<&ObjectParameter::wipe_erase>("wipe_erase");
    Member<&ObjectParameter::click_disable>("click_disable");
    Member<&ObjectParameter::is_visible>("disp");
    Member<&ObjectParameter::pattern_number>("patno");
    Member<&ObjectParameter::z_order>("order");
    Member<&ObjectParameter::z_layer>("layer");
    Member<&ObjectParameter::position_x>("x");
    Member<&ObjectParameter::position_y>("y");
    Member<&ObjectParameter::z_depth>("z");
    Member<&ObjectParameter::origin_x>("center_x");
    Member<&ObjectParameter::origin_y>("center_y");
    Member<&ObjectParameter::repetition_origin_x>("center_rep_x");
    Member<&ObjectParameter::repetition_origin_y>("center_rep_y");
    Member<&ObjectParameter::high_quality_scale_x_percent>("scale_x");
    Member<&ObjectParameter::high_quality_scale_y_percent>("scale_y");
    Member<&ObjectParameter::rotation_div10>("rotate_z");

    Property(
        "clip_use",
        [](const ObjectParameter& param) {
          return param.has_clip_rect() ? 1 : 0;
        },
        [](ObjectParameter& param, int value) {
          if (value) {
            if (!param.has_clip_rect())
              param.SetClipRect(Rect::GRP(0, 0, 0, 0));
          } else {
            param.ClearClipRect();
          }
        });
    Property(
        "clip_left",
        [](const ObjectParameter& param) { return param.clip_rect().x(); },
        [](ObjectParameter& param, int value) {
          SetClipRectValue(param, &Rect::set_x, value);
        });
    Property(
        "clip_top",
        [](const ObjectParameter& param) { return param.clip_rect().y(); },
        [](ObjectParameter& param, int value) {
          SetClipRectValue(param, &Rect::set_y, value);
        });
    Property(
        "clip_right",
        [](const ObjectParameter& param) { return param.clip_rect().x2(); },
        [](ObjectParameter& param, int value) {
          SetClipRectValue(param, &Rect::set_x2, value);
        });
    Property(
        "clip_bottom",
        [](const ObjectParameter& param) { return param.clip_rect().y2(); },
        [](ObjectParameter& param, int value) {
          SetClipRectValue(param, &Rect::set_y2, value);
        });

    Property(
        "src_clip_use",
        [](const ObjectParameter& param) {
          return param.has_own_clip_rect() ? 1 : 0;
        },
        [](ObjectParameter& param, int value) {
          if (value) {
            if (!param.has_own_clip_rect())
              param.SetOwnClipRect(Rect::GRP(0, 0, 0, 0));
          } else {
            param.ClearOwnClipRect();
          }
        });
    Property(
        "src_clip_left",
        [](const ObjectParameter& param) { return param.own_clip_rect().x(); },
        [](ObjectParameter& param, int value) {
          SetOwnClipRectValue(param, &Rect::set_x, value);
        });
    Property(
        "src_clip_top",
        [](const ObjectParameter& param) { return param.own_clip_rect().y(); },
        [](ObjectParameter& param, int value) {
          SetOwnClipRectValue(param, &Rect::set_y, value);
        });
    Property(
        "src_clip_right",
        [](const ObjectParameter& param) { return param.own_clip_rect().x2(); },
        [](ObjectParameter& param, int value) {
          SetOwnClipRectValue(param, &Rect::set_x2, value);
        });
    Property(
        "src_clip_bottom",
        [](const ObjectParameter& param) { return param.own_clip_rect().y2(); },
        [](ObjectParameter& param, int value) {
          SetOwnClipRectValue(param, &Rect::set_y2, value);
        });

    Member<&ObjectParameter::alpha_source>("tr");
    Member<&ObjectParameter::monochrome_transform>("mono");
    Member<&ObjectParameter::invert_transform>("reverse");
    Property(
        "bright", [](const ObjectParameter& param) { return param.Bright(); },
        [](ObjectParameter& param, int value) { param.SetBright(value); });
    Property(
        "dark", [](const ObjectParameter& param) { return param.Dark(); },
        [](ObjectParameter& param, int value) { param.SetDark(value); });

    Property(
        "color_r",
        [](const ObjectParameter& param) { return param.colour_red(); },
        [](ObjectParameter& param, int value) { param.SetColourRed(value); });
    Property(
        "color_g",
        [](const ObjectParameter& param) { return param.colour_green(); },
        [](ObjectParameter& param, int value) { param.SetColourGreen(value); });
    Property(
        "color_b",
        [](const ObjectParameter& param) { return param.colour_blue(); },
        [](ObjectParameter& param, int value) { param.SetColourBlue(value); });
    Property(
        "color_rate",
        [](const ObjectParameter& param) { return param.colour_level(); },
        [](ObjectParameter& param, int value) { param.SetColourLevel(value); });
    Property(
        "color_add_r",
        [](const ObjectParameter& param) { return param.tint_red(); },
        [](ObjectParameter& param, int value) { param.SetTintRed(value); });
    Property(
        "color_add_g",
        [](const ObjectParameter& param) { return param.tint_green(); },
        [](ObjectParameter& param, int value) { param.SetTintGreen(value); });
    Property(
        "color_add_b",
        [](const ObjectParameter& param) { return param.tint_blue(); },
        [](ObjectParameter& param, int value) { param.SetTintBlue(value); });

    Member<&ObjectParameter::mask_no>("mask_no");
    Member<&ObjectParameter::tonecurve_no>("tonecurve_no");
    Member<&ObjectParameter::culling>("culling");
    Member<&ObjectParameter::alpha_test>("alpha_test");
    Member<&ObjectParameter::alpha_blend>("alpha_blend");
    Member<&ObjectParameter::light_no>("light_no");
    Member<&ObjectParameter::fog_use>("fog_use");
    Property(
        "blend",
        [](const ObjectParameter& param) { return param.composite_mode; },
        [](ObjectParameter& param, int value) {
          param.SetCompositeMode(std::clamp(value, 0, 4));
        });
  }
};

struct ObjectEventPropertyBinder {
  sb::class_<ObjectHandle>& object;
  sb::class_<ObjectEventHandle>& event_class;

  template <auto member>
  void Member(std::string name) {
    Property(std::move(name), CreateGetter<member>(), CreateSetter<member>());
  }

  template <typename Getter, typename Setter>
  void Property(std::string name, Getter getter, Setter setter) {
    std::function<int(const ObjectParameter&)> property_getter =
        std::move(getter);
    std::function<void(ObjectParameter&, int)> property_setter =
        std::move(setter);
    std::string field_name = name;
    object.add_gc_root(event_class);
    object.prop_r(
        field_name.c_str(),
        [event = event_class, name = std::move(name),
         getter = std::move(property_getter),
         setter = std::move(property_setter)](ObjectHandle* h) mutable {
          return sr::Value(event.make_inst(h->object, name, getter, setter));
        });
  }

  void Bind() {
    Member<&ObjectParameter::pattern_number>("patno_eve");
    Member<&ObjectParameter::position_x>("x_eve");
    Member<&ObjectParameter::position_y>("y_eve");
    Member<&ObjectParameter::z_depth>("z_eve");
    Member<&ObjectParameter::origin_x>("center_x_eve");
    Member<&ObjectParameter::origin_y>("center_y_eve");
    Member<&ObjectParameter::repetition_origin_x>("center_rep_x_eve");
    Member<&ObjectParameter::repetition_origin_y>("center_rep_y_eve");
    Member<&ObjectParameter::high_quality_scale_x_percent>("scale_x_eve");
    Member<&ObjectParameter::high_quality_scale_y_percent>("scale_y_eve");
    Member<&ObjectParameter::rotation_div10>("rotate_z_eve");

    Property(
        "clip_left_eve",
        [](const ObjectParameter& param) { return param.clip_rect().x(); },
        [](ObjectParameter& param, int value) {
          SetClipRectValue(param, &Rect::set_x, value);
        });
    Property(
        "clip_top_eve",
        [](const ObjectParameter& param) { return param.clip_rect().y(); },
        [](ObjectParameter& param, int value) {
          SetClipRectValue(param, &Rect::set_y, value);
        });
    Property(
        "clip_right_eve",
        [](const ObjectParameter& param) { return param.clip_rect().x2(); },
        [](ObjectParameter& param, int value) {
          SetClipRectValue(param, &Rect::set_x2, value);
        });
    Property(
        "clip_bottom_eve",
        [](const ObjectParameter& param) { return param.clip_rect().y2(); },
        [](ObjectParameter& param, int value) {
          SetClipRectValue(param, &Rect::set_y2, value);
        });
    Property(
        "src_clip_left_eve",
        [](const ObjectParameter& param) { return param.own_clip_rect().x(); },
        [](ObjectParameter& param, int value) {
          SetOwnClipRectValue(param, &Rect::set_x, value);
        });
    Property(
        "src_clip_top_eve",
        [](const ObjectParameter& param) { return param.own_clip_rect().y(); },
        [](ObjectParameter& param, int value) {
          SetOwnClipRectValue(param, &Rect::set_y, value);
        });
    Property(
        "src_clip_right_eve",
        [](const ObjectParameter& param) { return param.own_clip_rect().x2(); },
        [](ObjectParameter& param, int value) {
          SetOwnClipRectValue(param, &Rect::set_x2, value);
        });
    Property(
        "src_clip_bottom_eve",
        [](const ObjectParameter& param) { return param.own_clip_rect().y2(); },
        [](ObjectParameter& param, int value) {
          SetOwnClipRectValue(param, &Rect::set_y2, value);
        });

    Member<&ObjectParameter::alpha_source>("tr_eve");
    Member<&ObjectParameter::monochrome_transform>("mono_eve");
    Member<&ObjectParameter::invert_transform>("reverse_eve");
    Property(
        "bright_eve",
        [](const ObjectParameter& param) { return param.Bright(); },
        [](ObjectParameter& param, int value) { param.SetBright(value); });
    Property(
        "dark_eve", [](const ObjectParameter& param) { return param.Dark(); },
        [](ObjectParameter& param, int value) { param.SetDark(value); });

    Property(
        "color_r_eve",
        [](const ObjectParameter& param) { return param.colour_red(); },
        [](ObjectParameter& param, int value) { param.SetColourRed(value); });
    Property(
        "color_g_eve",
        [](const ObjectParameter& param) { return param.colour_green(); },
        [](ObjectParameter& param, int value) { param.SetColourGreen(value); });
    Property(
        "color_b_eve",
        [](const ObjectParameter& param) { return param.colour_blue(); },
        [](ObjectParameter& param, int value) { param.SetColourBlue(value); });
    Property(
        "color_rate_eve",
        [](const ObjectParameter& param) { return param.colour_level(); },
        [](ObjectParameter& param, int value) { param.SetColourLevel(value); });
    Property(
        "color_add_r_eve",
        [](const ObjectParameter& param) { return param.tint_red(); },
        [](ObjectParameter& param, int value) { param.SetTintRed(value); });
    Property(
        "color_add_g_eve",
        [](const ObjectParameter& param) { return param.tint_green(); },
        [](ObjectParameter& param, int value) { param.SetTintGreen(value); });
    Property(
        "color_add_b_eve",
        [](const ObjectParameter& param) { return param.tint_blue(); },
        [](ObjectParameter& param, int value) { param.SetTintBlue(value); });
  }
};

void BindObject(SiglusMachine& runtime) {
  auto& vm = *runtime.vm;
  Stage* stage_state = runtime.stage.get();
  if (!stage_state)
    throw std::runtime_error("Object requires a stage buffer");
  if (!runtime.frame_action_manager)
    throw std::runtime_error("Object binding requires frame-action state");
  System* system = runtime.system.get();
  AssetScanner* asset_scanner = runtime.asset_scanner.get();
  FrameActionManager* frame_actions = runtime.frame_action_manager.get();
  sb::module_ m(vm.gc_.get(), vm.globals_.get());
  sb::class_<ObjectHandle> object(m, "Object");
  sb::class_<ObjectChildListHandle> child(m, "ObjectChild", false);
  sb::class_<ObjectEventHandle> event(m, "ObjectEvent", false);
  sb::class_<ObjectRepnoEventHandle> repno_event(m, "ObjectRepnoEvent", false);
  sb::class_<ObjectRepnoEventListHandle> repno_event_list(
      m, "ObjectRepnoEventList", false);
  object.def(sb::init([stage_state](int layer, int index) {
               if (index < 0)
                 throw sr::RuntimeError("Invalid object number: " +
                                        std::to_string(index));
               return std::make_unique<ObjectHandle>(
                   stage_state->objects.Get(layer, index));
             }),
             sb::arg("layer") = static_cast<int>(kLayerFg),
             sb::arg("index") = 0);

  sb::class_<ObjectListHandle> object_array(m, "ObjectArray");
  object_array.add_gc_root(object);
  object_array.def(
      sb::init([](int layer) {
        if (layer != kLayerFg && layer != kLayerBg && layer != kLayerNext)
          throw sr::RuntimeError("invalid object layer: " +
                                 std::to_string(layer));
        return std::make_unique<ObjectListHandle>(layer);
      }),
      sb::arg("layer"));
  object_array.def(
      "__getitem__",
      [stage_state, object](ObjectListHandle* h, int index) mutable {
        if (index < 0)
          throw sr::RuntimeError("Invalid object number: " +
                                 std::to_string(index));
        return sr::Value(
            object.make_inst(stage_state->objects.Get(h->layer, index)));
      },
      sb::arg("index"));

  child.add_gc_root(object);
  child.def(
      "__getitem__",
      [object](ObjectChildListHandle* h, int index) mutable {
        if (index < 0)
          throw sr::RuntimeError("object.child index is negative: " +
                                 std::to_string(index));
        const std::size_t size = h->object->GetChildren().size();
        if (static_cast<std::size_t>(index) >= size)
          throw sr::RuntimeError(
              "object.child index out of range: " + std::to_string(index) +
              " for size " + std::to_string(size));
        h->object->TouchChild(index);
        return sr::Value(object.make_inst(h->object->GetChildren()[index]));
      },
      sb::arg("index"));
  child.def(
      "resize",
      [](ObjectChildListHandle* h, int size) {
        if (size < 0)
          throw sr::RuntimeError("object.child size is negative: " +
                                 std::to_string(size));
        h->object->GetChildren().resize(static_cast<std::size_t>(size));
      },
      sb::arg("size"));
  child.def("size", [](ObjectChildListHandle* h) {
    const std::size_t size = h->object->GetChildren().size();
    if (size > static_cast<std::size_t>(std::numeric_limits<int>::max()))
      throw sr::RuntimeError("object.child size exceeds script integer range");
    return static_cast<int>(size);
  });
  object.add_gc_root(child);
  object.prop_r("child", [child](ObjectHandle* h) mutable {
    return sr::Value(child.make_inst(h->object));
  });

  // FrameActionManager's C++ factory is opaque to the VM's garbage collector.
  (*vm.builtins_)["%%siglus_object_class"] = object.native_class();
  runtime.make_object = [object](ActionOwner owner) mutable {
    return sr::Value(object.make_inst(std::move(owner)));
  };
  frame_actions->SetObjectFactory(runtime.make_object);
  object.prop_r("frame_action", [frame_actions](ObjectHandle* h) {
    return frame_actions->MakeActionValue(
        ActionReference{h->object->GetFrameAction(), h->object});
  });
  object.prop_r("frame_action_ch", [frame_actions](ObjectHandle* h) {
    return frame_actions->MakeListValue(h->object);
  });

  auto ilist_param = [&](const char* name, auto member) {
    object.add_gc_root(*runtime.ilist_cls);
    object.prop_r(
        name, [ilist = *runtime.ilist_cls, member](ObjectHandle* h) mutable {
          auto getter = [object = h->object,
                         member](sr::Fiber&) -> std::vector<int>& {
            return std::invoke(member, object->Param());
          };
          return sr::Value(ilist.make_inst(std::move(getter)));
        });
  };
  ilist_param("F", &ObjectParameter::F);
  ilist_param("x_rep", &ObjectParameter::adjustment_offsets_x);
  ilist_param("y_rep", &ObjectParameter::adjustment_offsets_y);
  ilist_param("tr_rep", &ObjectParameter::adjustment_alphas);

  DirectObjectPropertyBinder{object}.Bind();
  BindObjectEvents(event, system, runtime);
  BindObjectEvents(repno_event, system, runtime);
  ObjectEventPropertyBinder{object, event}.Bind();

  repno_event_list.add_gc_root(repno_event);
  repno_event_list.def(
      "__getitem__",
      [repno_event](ObjectRepnoEventListHandle* h, int index) mutable {
        if (index < 0 || index >= 8)
          throw sr::RuntimeError("object rep event index out of range: " +
                                 std::to_string(index));
        return sr::Value(repno_event.make_inst(h->object, index, h->name,
                                               h->getter, h->setter));
      },
      sb::arg("index"));
  repno_event_list.def(
      "resize", [](ObjectRepnoEventListHandle* h, int size) {},
      sb::arg("size"));
  repno_event_list.def("size", [](ObjectRepnoEventListHandle* h) { return 8; });

  auto bind_repno_event_list = [&](const char* name,
                                   ObjectRepnoEventHandle::Getter getter,
                                   ObjectRepnoEventHandle::Setter setter) {
    object.add_gc_root(repno_event_list);
    object.prop_r(
        name, [list = repno_event_list, name = std::string(name),
               getter = std::move(getter),
               setter = std::move(setter)](ObjectHandle* h) mutable {
          return sr::Value(list.make_inst(h->object, name, getter, setter));
        });
  };
  bind_repno_event_list("x_rep_eve",
                        CreateGetter<&ObjectParameter::adjustment_offsets_x>(),
                        CreateSetter<&ObjectParameter::adjustment_offsets_x>());
  bind_repno_event_list("y_rep_eve",
                        CreateGetter<&ObjectParameter::adjustment_offsets_y>(),
                        CreateSetter<&ObjectParameter::adjustment_offsets_y>());
  bind_repno_event_list("tr_rep_eve",
                        CreateGetter<&ObjectParameter::adjustment_alphas>(),
                        CreateSetter<&ObjectParameter::adjustment_alphas>());

  object.def("init", [](ObjectHandle* h) { h->object->Reset(); });
  object.def("init_param", [](ObjectHandle* h) {
    auto& object = *h->object;
    object.ResetFrameActions();
    object.InitializeParams();
  });
  object.def("free", [](ObjectHandle* h) { h->object->FreeOwnObjectData(); });
  object.def(
      "create",
      [system](ObjectHandle* h, std::vector<sr::Value> args) -> void {
        if (args.size() != 1 && args.size() != 2 && args.size() != 4 &&
            args.size() != 5) {
          throw std::runtime_error("Object.create expects 1, 2, 4, or 5 args");
        }
        std::string filename = AsString(args[0]);
        if (filename.empty())
          throw std::runtime_error("Object.create filename is empty");

        std::optional<int> visible, x, y, pattern;
        if (args.size() >= 2)
          visible = RequireInt(args[1], "disp");
        if (args.size() >= 4)
          x = RequireInt(args[2], "x"), y = RequireInt(args[3], "y");
        if (args.size() == 5)
          pattern = RequireInt(args[4], "pat");

        CreateObject(*h->object, system, std::move(filename));
        if (visible)
          h->object->Param().SetVisible(*visible);
        if (x)
          h->object->Param().SetX(*x);
        if (y)
          h->object->Param().SetY(*y);
        if (pattern)
          h->object->Param().SetPattNo(*pattern);
      },
      sb::vararg);
  object.def(
      "create_movie",
      [system, asset_scanner](ObjectHandle* h, std::vector<sr::Value> args) {
        CreateMovie(*h->object, system, asset_scanner, std::move(args), false,
                    false, false);
      },
      sb::vararg);
  object.def(
      "create_movie_loop",
      [system, asset_scanner](ObjectHandle* h, std::vector<sr::Value> args) {
        CreateMovie(*h->object, system, asset_scanner, std::move(args), true,
                    false, false);
      },
      sb::vararg);
  object.def(
      "create_movie_wait",
      [system, asset_scanner](ObjectHandle* h, sr::VM& vm,
                              std::vector<sr::Value> args) {
        if (CreateMovie(*h->object, system, asset_scanner, std::move(args),
                        false, true, false))
          return WaitMovie(h->object, system, vm, false);
        return MakeResolvedFuture(*vm.gc_);
      },
      sb::vararg);
  object.def(
      "create_movie_waitkey",
      [system, asset_scanner](ObjectHandle* h, sr::VM& vm,
                              std::vector<sr::Value> args) {
        if (CreateMovie(*h->object, system, asset_scanner, std::move(args),
                        false, true, true))
          return WaitMovie(h->object, system, vm, true);
        return MakeResolvedFuture(*vm.gc_);
      },
      sb::vararg);
  object.def("load_gan",
             [system, asset_scanner](ObjectHandle* h, std::string filename) {
               LoadGan(*h->object, system, asset_scanner, std::move(filename));
             });
  object.def(
      "start_gan",
      [](ObjectHandle* h, std::vector<sr::Value> args) {
        StartGan(*h->object, std::move(args));
      },
      sb::vararg);
  object.def("create_rect",
             [](ObjectHandle* h, int left, int top, int right, int down, int r,
                int g, int b, int alpha, int display) {
               h->object->Reset();
               auto& param = h->object->Param();
               param.blend_colour = RGBAColour(r, g, b, alpha);
               h->object->SetDrawer(std::make_unique<ColourFilterObjectData>(
                   Rect::GRP(left, top, right, down)));
               h->object->ClearFilePath();
               param.SetVisible(display);
             });
  object.def("exist_type",
             [](ObjectHandle* h) { return h->object->HasDrawer() ? 1 : 0; });
  object.def(
      "get_size_x",
      [](const ObjectHandle* h, int cut_no) { return h->object->PixelWidth(); },
      sb::arg("cut_no") = 0);
  object.def(
      "get_size_y",
      [](const ObjectHandle* h, int cut_no) {
        return h->object->PixelHeight();
      },
      sb::arg("cut_no") = 0);
  object.def("get_file_path",
             [](ObjectHandle* h) { return h->object->FilePath(); });
  object.def("set_clip", [](ObjectHandle* h, int use, int left, int top,
                            int right, int bottom) {
    auto clip = Rect::GRP(left, top, right, bottom);
    if (use)
      h->object->Param().SetClipRect(clip);
    else
      h->object->Param();
  });
  object.def("set_src_clip", [](ObjectHandle* h, int use, int left, int top,
                                int right, int bottom) {
    auto clip = Rect::GRP(left, top, right, bottom);
    if (use)
      h->object->Param().SetOwnClipRect(clip);
    else
      h->object->Param().ClearOwnClipRect();
  });
  object.def("set_center_rep", [](ObjectHandle* h, int x, int y) {
    h->object->Param().SetRepOriginX(x);
    h->object->Param().SetRepOriginY(y);
  });
  object.def("set_scale", [](ObjectHandle* h, int x, int y) {
    h->object->Param().SetHqScaleX(x);
    h->object->Param().SetHqScaleY(y);
  });
  object.def("set_pos", [](ObjectHandle* h, int x, int y) {
    h->object->Param().SetX(x);
    h->object->Param().SetY(y);
  });
  object.def("clear_button", [](ObjectHandle* h) {
    ButtonProperties button;
    button.action = -1;
    button.se = -1;
    h->object->Param().SetButtonProperty(button);
  });
  object.def(
      "set_button",
      [](ObjectHandle* h, std::vector<sr::Value> raw_args) {
        CallPacket packet = CallPacket::DecodeFrom(std::move(raw_args));
        const std::vector<sr::Value>& args = packet.args;
        if (args.empty() || args.size() > 4) {
          throw std::runtime_error("Object.set_button expects 1 to 4 args");
        }

        int button_no = 0;
        int group_no = 0;
        int action_no = 0;
        int se_no = 0;
        if (args.size() >= 1)
          button_no = RequireInt(args[0], "Object.set_button button_no");
        if (args.size() >= 2)
          group_no = RequireInt(args[1], "Object.set_button group_no");
        if (args.size() >= 3)
          action_no = RequireInt(args[2], "Object.set_button action_no");
        if (args.size() >= 4)
          se_no = RequireInt(args[3], "Object.set_button se_no");

        h->object->Param().SetButtonOpts(action_no, se_no, group_no, button_no);
      },
      sb::vararg);
  object.def(
      "set_button_group",
      [](ObjectHandle* h, std::vector<sr::Value> raw_args) {
        CallPacket packet = CallPacket::DecodeFrom(std::move(raw_args));
        const std::vector<sr::Value>& args = packet.args;
        if (args.size() != 1)
          throw std::runtime_error("Object.set_button_group expects 1 arg");

        ButtonProperties button = h->object->Param().ButtonProperty();
        button.group = RequireInt(args[0], "Object.set_button_group group_no");
        h->object->Param().SetButtonProperty(button);
      },
      sb::vararg);
  object.def("set_button_state_normal", [](ObjectHandle* h) {
    h->object->Param().SetButtonState(kBtnNormal);
  });
  object.def("set_button_state_select", [](ObjectHandle* h) {
    h->object->Param().SetButtonState(kBtnSelect);
  });
  object.def("set_button_state_disable", [](ObjectHandle* h) {
    h->object->Param().SetButtonState(kBtnDisable);
  });
  object.def("get_button_state", [](ObjectHandle* h) {
    return h->object->Param().GetButtonState();
  });
  object.def("get_button_hit_state", [](ObjectHandle* h) {
    return static_cast<int>(h->object->Param().button.hit_state);
  });
  object.def("get_button_real_state", [](ObjectHandle* h) {
    const int state = h->object->Param().GetButtonState();
    if (state == kBtnSelect || state == kBtnDisable)
      return state;
    return static_cast<int>(h->object->Param().button.real_state);
  });
  object.def("set_button_call", [loader = runtime.loader.get()](
                                    ObjectHandle* h, sr::Fiber& fiber,
                                    std::string command) {
    if (!loader)
      throw sr::RuntimeError("set_button_call requires a scene loader");
    const auto& resolved = loader->ResolveUserCommand(fiber, command);
    if (!resolved.args.empty())
      throw sr::RuntimeError(
          "button CALL requires a command with no arguments");
    // Resolve the current scene now; the function remains owned by its module.
    h->object->Param().button.call = resolved.function;
  });
  object.def("clear_button_call", [](ObjectHandle* h) {
    h->object->Param().button.call = nullptr;
    if (h->object->Param().button.config)
      h->object->Param().button.config->call.reset();
  });
  object.def("pause_movie", [](ObjectHandle* h) {
    if (auto* data = h->object->GetDrawer<ObjectMovieData>())
      data->Pause();
  });
  object.def("resume_movie", [](ObjectHandle* h) {
    if (auto* data = h->object->GetDrawer<ObjectMovieData>())
      data->Resume();
  });
  object.def(
      "seek_movie",
      [](ObjectHandle* h, std::vector<sr::Value> args) {
        SeekMovie(*h->object, std::move(args));
      },
      sb::vararg);
  object.def("get_movie_seek_time", [](ObjectHandle* h) {
    if (const auto* data = h->object->GetDrawer<ObjectMovieData>())
      return data->GetSeekTime();
    return 0;
  });
  object.def("check_movie", [](ObjectHandle* h) {
    if (const auto* data = h->object->GetDrawer<ObjectMovieData>())
      return data->CheckMovie() ? 1 : 0;
    return 0;
  });
  object.def(
      "wait_movie",
      [system](ObjectHandle* h, sr::VM& vm, std::vector<sr::Value>) {
        return WaitMovie(h->object, system, vm, false);
      },
      sb::vararg);
  object.def(
      "wait_movie_key",
      [system](ObjectHandle* h, sr::VM& vm, std::vector<sr::Value>) {
        return WaitMovie(h->object, system, vm, true);
      },
      sb::vararg);
  object.def("end_movie_loop", [](ObjectHandle* h) {
    if (auto* data = h->object->GetDrawer<ObjectMovieData>())
      data->EndLoop();
  });
  object.def(
      "set_movie_auto_free",
      [](ObjectHandle* h, std::vector<sr::Value> args) {
        SetMovieAutoFree(*h->object, std::move(args));
      },
      sb::vararg);
  object.def("alleve_end", [](ObjectHandle* h) {
    GraphicsObject& o = *h->object;
    o.EndAllMutators();
  });
  object.def("alleve_wait", [system](ObjectHandle* h, sr::VM& vm) {
    auto done = [object = h->object] { return object->CountMutators() == 0; };
    return MakePollingWaitFuture(vm, std::move(done), false,
                                 system ? system->event_ptr().get() : nullptr);
  });
  object.def("alleve_check", [](ObjectHandle* h) -> int {
    GraphicsObject& o = *h->object;
    return o.CountMutators() > 0;
  });
  object.def("change_file", [system](ObjectHandle* h, std::string filename) {
    ChangeObjectFile(*h->object, system, std::move(filename));
  });
  object.def(
      "copy_from",
      [](ObjectHandle* h, sr::Value src_value) {
        auto die = [] {
          throw sr::RuntimeError("copy_from requires an object");
        };
        auto* inst = src_value.Get_if<sr::NativeInstance>();
        if (!inst || !inst->klass || inst->klass->name != "Object")
          die();
        auto* src = inst->GetForeign<ObjectHandle>();
        if (!src)
          die();

        *h->object = src->object->Clone();
      },
      sb::arg("src"));
}

RLVM_REGISTER(SiglusBindingRegistry, "1_object", BindObject);

}  // namespace libsiglus::binding
