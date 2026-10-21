// -*- Mode: C++; tab-width:2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
// vi:tw=80:et:ts=2:sts=2
//
// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2009 Elliot Glaysher
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

#include "modules/module_bgr.hpp"

#include <filesystem>
#include <iostream>
#include <string>

#include "core/colour.hpp"
#include "core/haikei.hpp"
#include "core/hik.hpp"
#include "core/stage.hpp"
#include "long_operations/effects/effect.hpp"
#include "long_operations/effects/effect_factory.hpp"
#include "machine/general_operations.hpp"
#include "machine/rlmachine.hpp"
#include "machine/rloperation.hpp"
#include "machine/rloperation/argc_t.hpp"
#include "machine/rloperation/complex_t.hpp"
#include "machine/rloperation/special_t.hpp"
#include "modules/module_grp.hpp"
#include "systems/event_system.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "systems/system.hpp"
#include "utilities/graphics.hpp"

namespace fs = std::filesystem;

static const std::set<std::string> HIK_FILETYPES = {"hik", "g00", "pdt"};

// Working theory of how this module works: The haikei module is one backing
// surface and (optionally) a HIK script. Games like AIR and the Maiden Halo
// demo use just the surface with a combination of bgrMulti and
// bgrLoadHaikei. OTOH, ALMA and planetarian use HIK scripts and the whole
// point of HIK scripts is to manipulate the backing surface on a timer that's
// divorced from the main interpreter loop.

namespace {

struct bgrLoadHaikei_blank : public RLOpcode<IntConstant_T> {
  void operator()(RLMachine& machine, int sel) {
    Haikei& haikei = machine.haikei();
    machine.GetLocalConfig().reallive.default_bgr_name = "";
    haikei.ClearHikRenderer();
    haikei.set_graphics_background(BACKGROUND_HIK);

    std::shared_ptr<SDLSurface> before = machine.RenderToSurface();
    haikei.GetHaikei()->Fill(RGBAColour::Clear());

    if (!machine.replaying_graphics_stack())
      machine.stage().Wipe();

    std::shared_ptr<SDLSurface> after = machine.RenderToSurface();

    std::shared_ptr<lop::LongOperation> effect(
        lop::EffectFactory::BuildFromSEL(machine, after, before, sel));
    machine.PushLongOperation(effect);
  }
};

struct bgrLoadHaikei_main : RLOpcode<StrConstant_T, IntConstant_T> {
  void operator()(RLMachine& machine, std::string filename, int sel) {
    System& system = machine.GetSystem();
    Haikei& haikei = machine.haikei();
    machine.GetLocalConfig().reallive.default_bgr_name = filename;
    haikei.set_graphics_background(BACKGROUND_HIK);

    // bgrLoadHaikei clears the stack.
    machine.stage().graphics_stack.clear();

    fs::path path;
    if (auto f = system.GetAssetScanner()->FindFile(filename, HIK_FILETYPES);
        f.has_value())
      path = f.value();
    else
      throw f.error();

    if (path.string().ends_with("hik")) {
      if (!machine.replaying_graphics_stack())
        machine.stage().Wipe();

      haikei.LoadHikRenderer(filename, path);
    } else {
      std::shared_ptr<SDLSurface> before = machine.RenderToSurface();

      if (!path.empty()) {
        std::shared_ptr<const SDLSurface> source(
            machine.GetSurfaceNamedAndMarkViewed(filename));
        std::shared_ptr<SDLSurface> haikei_surface = haikei.GetHaikei();
        source->BlitToSurface(*haikei_surface, source->GetRect(),
                              source->GetRect(), 255, true);
      }

      // Promote the objects if we're in normal mode. If we're restoring the
      // graphics stack, we already have our layers promoted.
      if (!machine.replaying_graphics_stack())
        machine.stage().Wipe();

      std::shared_ptr<SDLSurface> after = machine.RenderToSurface();

      std::shared_ptr<lop::LongOperation> effect(
          lop::EffectFactory::BuildFromSEL(machine, after, before, sel));
      machine.PushLongOperation(effect);
    }
  }
};

struct bgrLoadHaikei_wtf
    : RLOpcode<StrConstant_T, IntConstant_T, IntConstant_T, IntConstant_T> {
  void operator()(RLMachine& machine,
                  std::string filename,
                  int sel,
                  int a,
                  int b) {
    bgrLoadHaikei_main()(machine, filename, sel);
  }
};

struct bgrLoadHaikei_wtf2 : RLOpcode<StrConstant_T,
                                     IntConstant_T,
                                     IntConstant_T,
                                     IntConstant_T,
                                     IntConstant_T,
                                     IntConstant_T> {
  void operator()(RLMachine& machine,
                  string filename,
                  int sel,
                  int a,
                  int b,
                  int c,
                  int d) {
    // cerr << "Filename: " << filename
    //      << "(a: " << a << ", b: " << b << ", c: " << c << ", d: " << d <<
    // ")"
    //      << endl;
    bgrLoadHaikei_main()(machine, filename, sel);
  }
};

// -----------------------------------------------------------------------

typedef Argc_T<
    Special_T<DefaultSpecialMapper,
              // 0:copy(strC 'filename')
              StrConstant_T,
              // 1:DUMMY. Unknown.
              Complex_T<StrConstant_T, IntConstant_T>,
              // 2:copy(strC 'filename', '?')
              Complex_T<StrConstant_T, IntConstant_T>,
              // 3:DUMMY. Unknown.
              Complex_T<StrConstant_T, IntConstant_T>,
              // 4:copy(strC, '?', '?')
              Complex_T<StrConstant_T, IntConstant_T, IntConstant_T>>>
    BgrMultiCommand;

struct bgrMulti_1
    : public RLOpcode<StrConstant_T, IntConstant_T, BgrMultiCommand> {
 public:
  void operator()(RLMachine& machine,
                  string filename,
                  int effectNum,
                  BgrMultiCommand::type commands) {
    Haikei& haikei = machine.haikei();

    // Get the state of the world before we do any processing.
    std::shared_ptr<SDLSurface> before = machine.RenderToSurface();

    haikei.set_graphics_background(BACKGROUND_HIK);

    // May need to use current background.
    if (filename == "???")
      filename = machine.GetLocalConfig().reallive.default_bgr_name;

    // Load "filename" as the background.
    std::shared_ptr<const SDLSurface> surface(
        machine.GetSurfaceNamedAndMarkViewed(filename));
    surface->BlitToSurface(*haikei.GetHaikei(), surface->GetRect(),
                           surface->GetRect(), 255, true);

    // TODO(erg): Unsure about the alpha in these implementation.
    for (BgrMultiCommand::type::const_iterator it = commands.begin();
         it != commands.end(); it++) {
      switch (it->type) {
        case 0: {
          // 0:copy(strC 'filename')
          surface = machine.GetSurfaceNamedAndMarkViewed(it->first);
          surface->BlitToSurface(*haikei.GetHaikei(), surface->GetRect(),
                                 surface->GetRect(), 255, true);
          break;
        }
        case 2: {
          // 2:copy(strC 'filename', '?')
          Rect srcRect;
          Point dest;
          GetSELPointAndRect(machine, std::get<1>(it->third), srcRect, dest);

          surface =
              machine.GetSurfaceNamedAndMarkViewed(std::get<0>(it->third));
          Rect destRect = Rect(dest, srcRect.size());
          surface->BlitToSurface(*haikei.GetHaikei(), srcRect, destRect, 255,
                                 true);
          break;
        }
        default: {
          std::cerr << "Don't know what to do with a type " << it->type
                    << " in bgrMulti_1" << std::endl;
          break;
        }
      }
    }

    // Promote the objects if we're in normal mode. If we're restoring the
    // graphics stack, we already have our layers promoted.
    if (!machine.replaying_graphics_stack())
      machine.stage().Wipe();

    std::shared_ptr<SDLSurface> after = machine.RenderToSurface();
    std::shared_ptr<lop::LongOperation> effect(
        lop::EffectFactory::BuildFromSEL(machine, after, before, effectNum));
    machine.PushLongOperation(effect);
  }
};

struct bgrNext : public RLOpcode<> {
  void operator()(RLMachine& machine) {
    HIKRenderer* renderer = machine.haikei().hik_renderer();
    if (renderer) {
      renderer->NextAnimationFrame();
    }
  }
};

struct bgrSetXOffset : public RLOpcode<IntConstant_T> {
  void operator()(RLMachine& machine, int offset) {
    HIKRenderer* renderer = machine.haikei().hik_renderer();
    if (renderer) {
      renderer->set_x_offset(offset);
    }
  }
};

struct bgrSetYOffset : public RLOpcode<IntConstant_T> {
  void operator()(RLMachine& machine, int offset) {
    HIKRenderer* renderer = machine.haikei().hik_renderer();
    if (renderer) {
      renderer->set_y_offset(offset);
    }
  }
};

struct bgrPreloadScript : public RLOpcode<IntConstant_T, StrConstant_T> {
  void operator()(RLMachine& machine, int slot, string name) {
    System& system = machine.GetSystem();
    fs::path path;
    if (auto f = system.GetAssetScanner()->FindFile(name, HIK_FILETYPES);
        f.has_value())
      path = f.value();
    else
      throw f.error();
    if (path.string().ends_with("hik")) {
      machine.haikei().PreloadHIKScript(slot, name, path);
    }
  }
};

}  // namespace

// -----------------------------------------------------------------------

BgrModule::BgrModule() : MappedRLModule(GraphicsStackMappingFun, "Bgr", 1, 40) {
  AddOpcode(10, 0, "bgrLoadHaikei", new bgrLoadHaikei_blank);
  AddOpcode(10, 1, "bgrLoadHaikei", new bgrLoadHaikei_main);
  AddOpcode(10, 2, "bgrLoadHaikei", new bgrLoadHaikei_wtf);
  AddOpcode(10, 3, "bgrLoadHaikei", new bgrLoadHaikei_wtf2);

  AddUnsupportedOpcode(100, 0, "bgrMulti");
  AddOpcode(100, 1, "bgrMulti", new bgrMulti_1);

  AddOpcode(1000, 0, "bgrNext", new bgrNext);

  AddOpcode(1104, 0, "bgrSetXOffset", new bgrSetXOffset);
  AddOpcode(1105, 0, "bgrSetYOffset", new bgrSetYOffset);

  AddOpcode(2000, 0, "bgrPreloadScript", new bgrPreloadScript);
  AddOpcode(2001, 0, "bgrClearPreloadedScript",
            CallFunction(&Haikei::ClearPreloadedHIKScript));
  AddOpcode(2002, 0, "bgrClearAllPreloadedScripts",
            CallFunction(&Haikei::ClearAllPreloadedHIKScripts));
}
