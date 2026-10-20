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

#include "libsiglus/bindings/capture.hpp"

#include "core/memory_internal/facade.hpp"
#include "libsiglus/bindings/registry.hpp"
#include "libsiglus/bindings/util.hpp"
#include "log/domain_logger.hpp"
#include "long_operations/siglus_wait.hpp"
#include "platforms/implementor.hpp"
#include "srbind/srbind.hpp"
#include "utilities/file.hpp"
#include "vm/exception.hpp"
#include "vm/object.hpp"
#include "vm/vm.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace libsiglus::binding {
namespace sr = serilang;
namespace sb = srbind;
namespace fs = std::filesystem;

CaptureRequest DecodeCaptureRequest(CaptureKind kind,
                                    std::vector<sr::Value> args) {
  CaptureRequest request{.kind = kind};
  const auto packet = CallPacket::DecodeFrom(std::move(args));
  if (!packet.args.empty())
    throw sr::RuntimeError(
        "Capture command does not take positional arguments");
  int width = 0, height = 0;
  ForEachKeywordId(packet.kwargs, [&](int id, const sr::Value& value) {
    const int v = RequireInt(value, "capture option");
    switch (id) {
      case 0:
        request.end.first = v;
        break;
      case 1:
        request.end.second = v;
        break;
      case 2:
        request.display = v != 0;
        break;
      case 3:
      case 4:
        if (kind != CaptureKind::Local)
          throw sr::RuntimeError(
              "Capture size options require capture_for_local_save");
        if (v <= 0)
          throw sr::RuntimeError("Capture dimensions must be positive");
        (id == 3 ? width : height) = v;
        break;
      default:
        throw sr::RuntimeError("Unknown capture option");
    }
  });
  request.size = Size(width, height);
  return request;
}

CaptureFileOptions DecodeCaptureFileOptions(std::vector<sr::Value> args,
                                            bool save) {
  const auto packet = CallPacket::DecodeFrom(std::move(args));
  if (packet.args.size() != 2)
    throw sr::RuntimeError(
        "Capture file operation expects filename and extension");
  CaptureFileOptions options{.name = AsString(packet.args[0]),
                             .extension = AsString(packet.args[1]),
                             .title = save ? "Save capture" : "Open capture"};
  std::transform(options.extension.begin(), options.extension.end(),
                 options.extension.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  if (options.extension != "bmp" && options.extension != "png")
    throw sr::RuntimeError("Capture format must be bmp or png");
  ForEachKeywordId(packet.kwargs, [&](int id, const sr::Value& value) {
    switch (id) {
      case 0:
        options.dialog = RequireInt(value, "capture dialog") != 0;
        break;
      case 1:
        options.title = AsString(value);
        break;
      case 2:
        options.integers = value;
        break;
      case 3:
        options.int_start = RequireInt(value, "capture integer index");
        break;
      case 4:
        options.int_count = RequireInt(value, "capture integer count");
        break;
      case 5:
        options.strings = value;
        break;
      case 6:
        options.str_start = RequireInt(value, "capture string index");
        break;
      case 7:
        options.str_count = RequireInt(value, "capture string count");
        break;
      default:
        throw sr::RuntimeError("Unknown capture file option");
    }
  });
  if (options.extension == "png" &&
      (!save || options.int_count > 0 || options.str_count > 0))
    throw sr::RuntimeError("Capture flag data requires BMP format");
  return options;
}

namespace {

template <typename T>
T* Bank(sr::Value value, const char* name) {
  auto* instance = value.Get_if<sr::NativeInstance>();
  if (!instance || !instance->klass || instance->klass->name != name ||
      !instance->foreign)
    throw sr::RuntimeError(std::string("Capture flags require ") + name);
  return instance->GetForeign<T>();
}

fs::path CaptureFilePath(const CaptureFileOptions& options,
                         CaptureManager& captures,
                         IPlatformImplementor* platform,
                         bool save) {
  auto path = captures.FilePath(options.name, options.extension);
  if (options.dialog) {
    path = platform ? platform->SelectFile(save, options.title, path,
                                           options.extension)
                    : fs::path{};
    if (save && !path.empty() && path.extension().empty())
      path += '.' + options.extension;
    return path;
  }

  if (save)
    return path;
  return CorrectPath(path.is_absolute() ? path.root_path() : fs::current_path(),
                     path.relative_path().string());
}

std::pair<int, int> Range(int begin, int count, int size) {
  const auto end = std::clamp<std::int64_t>(
      static_cast<std::int64_t>(begin) + std::max(count, 0), 0, size);
  begin = std::clamp(begin, 0, size);
  return {begin, static_cast<int>(std::max<std::int64_t>(begin, end))};
}

CaptureFlags ReadFlags(const CaptureFileOptions& options, sr::Fiber& fiber) {
  CaptureFlags flags;
  if (options.int_count > 0) {
    auto* bank = Bank<SiglusIntListFacade>(options.integers, "__SiglusIntList");
    const auto [begin, end] =
        Range(options.int_start, options.int_count, bank->size(fiber));
    for (int i = begin; i < end; ++i)
      flags.integers.push_back(bank->get(fiber, i));
  }
  if (options.str_count > 0) {
    auto* bank = Bank<SiglusStrListFacade>(options.strings, "__SiglusStrList");
    const auto [begin, end] =
        Range(options.str_start, options.str_count, bank->size(fiber));
    for (int i = begin; i < end; ++i)
      flags.strings.push_back(bank->get(fiber, i));
  }
  return flags;
}

void WriteFlags(const CaptureFileOptions& options,
                sr::Fiber& fiber,
                const CaptureFlags& flags) {
  // Validate both destinations before writing either bank.
  auto* ints = options.int_count > 0 ? Bank<SiglusIntListFacade>(
                                           options.integers, "__SiglusIntList")
                                     : nullptr;
  auto* strs = options.str_count > 0 ? Bank<SiglusStrListFacade>(
                                           options.strings, "__SiglusStrList")
                                     : nullptr;
  if (ints) {
    const auto [begin, end] =
        Range(options.int_start,
              std::min<std::size_t>(options.int_count, flags.integers.size()),
              ints->size(fiber));
    for (int i = begin; i < end; ++i)
      ints->set(fiber, i, flags.integers[i - begin]);
  }
  if (strs) {
    const auto [begin, end] =
        Range(options.str_start,
              std::min<std::size_t>(options.str_count, flags.strings.size()),
              strs->size(fiber));
    for (int i = begin; i < end; ++i)
      strs->set(fiber, i, flags.strings[i - begin]);
  }
}

}  // namespace

void BindCapture(SiglusMachine& runtime) {
  auto& vm = *runtime.vm;
  const auto captures = runtime.captures;
  if (!captures)
    throw sr::RuntimeError("Capture support is unavailable");
  sb::module_ globals(vm.gc_.get(), vm.globals_.get());
  globals.def("capture", [&runtime] {
    return runtime.RequestCapture({.kind = CaptureKind::Save});
  });
  globals.def("capture_from_file", [captures](std::string filename) {
    captures->FromFile(filename);
  });
  globals.def("capture_free",
              [captures] { captures->Release(CaptureKind::Save); });
  globals.def(
      "capture_for_object",
      [&runtime](std::vector<sr::Value> args) {
        return runtime.RequestCapture(
            DecodeCaptureRequest(CaptureKind::Object, std::move(args)));
      },
      sb::vararg);
  globals.def("capture_for_object_free",
              [captures] { captures->Release(CaptureKind::Object); });
  globals.def("capture_for_tweet", [&runtime] {
    return runtime.RequestCapture({.kind = CaptureKind::Tweet});
  });
  globals.def("capture_free_for_tweet",
              [captures] { captures->Release(CaptureKind::Tweet); });
  globals.def(
      "capture_for_local_save",
      [&runtime](std::vector<sr::Value> args) {
        return runtime.RequestCapture(
            DecodeCaptureRequest(CaptureKind::Local, std::move(args)));
      },
      sb::vararg);
}

void BindCaptureFiles(SiglusMachine& runtime) {
  auto& vm = *runtime.vm;
  const auto captures = runtime.captures;
  auto* module = vm.builtins_->at("syscom").Get_if<sr::Module>();
  sb::module_ syscom(vm.gc_.get(), module->globals.get());
  syscom.def("create_capture_buffer", [captures](int width, int height) {
    captures->CreateBuffer(Size(width, height));
  });
  syscom.def("destroy_capture_buffer",
             [captures] { captures->DestroyBuffer(); });
  syscom.def("capture_to_capture_buffer", [&runtime](int x, int y) {
    return runtime.RequestCapture(
        {.kind = CaptureKind::Buffer, .offset = Point(x, y)});
  });
  syscom.def("capture_to_png", [&runtime](int x, int y, std::string name) {
    return runtime.RequestCapture({.kind = CaptureKind::Buffer,
                                   .offset = Point(x, y),
                                   .png_name = std::move(name)});
  });
  syscom.def(
      "save_capture_buffer_to_file",
      [captures, platform = runtime.platform_implementor](
          sr::VM& vm, sr::Fiber& fiber, std::vector<sr::Value> args) {
        const auto options = DecodeCaptureFileOptions(std::move(args), true);
        if (!captures->Snapshot(CaptureKind::Buffer))
          throw sr::RuntimeError("Capture buffer has not been created");
        auto flags = ReadFlags(options, fiber);
        const auto path =
            CaptureFilePath(options, *captures, platform.get(), true);
        if (path.empty())
          return lop::MakeResolvedFuture(*vm.gc_, 0);
        return lop::MakeResolvedFuture(
            *vm.gc_, captures->Export(path, options.extension, flags) ? 1 : 0);
      },
      sb::vararg);
  syscom.def(
      "load_flag_from_capture_file",
      [captures, platform = runtime.platform_implementor](
          sr::VM& vm, sr::Fiber& fiber, std::vector<sr::Value> args) {
        const auto options = DecodeCaptureFileOptions(std::move(args), false);
        const auto path =
            CaptureFilePath(options, *captures, platform.get(), false);
        if (path.empty())
          return lop::MakeResolvedFuture(*vm.gc_, 0);
        CaptureFlags flags;
        try {
          flags = CaptureManager::ImportFlags(path);
        } catch (const std::exception& e) {
          DomainLogger("SiglusCapture")(Severity::Warn) << e.what();
          return lop::MakeResolvedFuture(*vm.gc_, 0);
        }
        WriteFlags(options, fiber, flags);
        return lop::MakeResolvedFuture(*vm.gc_, 1);
      },
      sb::vararg);
}

RLVM_REGISTER(SiglusBindingRegistry, "capture", BindCapture)
RLVM_REGISTER(SiglusBindingRegistry, "syscom_capture", BindCaptureFiles)

}  // namespace libsiglus::binding
