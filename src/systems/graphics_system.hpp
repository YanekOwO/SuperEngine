// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2006, 2007 Elliot Glaysher
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
//
// -----------------------------------------------------------------------

#pragma once

#include <boost/serialization/access.hpp>
#include <boost/serialization/split_member.hpp>
#include <boost/serialization/version.hpp>
#include <filesystem>

#include <cstdint>
#include <iosfwd>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/cgm_table.hpp"
#include "core/event_listener.hpp"
#include "core/rect.hpp"
#include "core/tone_curve.hpp"

#include "lru_cache.hpp"
#include "systems/igraphics_backend.hpp"
#include "utilities/clock.hpp"
#include "utilities/lazy_array.hpp"

class AssetScanner;
class IGraphicsBackend;
class ISceneRenderer;
class Gameexe;
class GraphicsObject;
class GraphicsObjectData;
class Haikei;
class MouseCursor;
class Renderable;
class RGBAColour;
class RLMachine;
class Size;
class SDLSurface;
class Stage;
class System;
struct ObjectSettings;
class Album;

template <typename T>
class LazyArray;

// Variables and configuration data that are global across all save
// game files in a game.
struct GraphicsSystemGlobals {
  GraphicsSystemGlobals();
  explicit GraphicsSystemGlobals(Gameexe& gameexe);

  // ShowObject flags
  int show_object_1, show_object_2;

  int show_weather;

  // Whether we should skip animations (such as those made by Effect)
  int skip_animations;

  // screen mode. 1 is windowed (default), 0 is full-screen.
  int screen_mode;

  // CG Table
  CGMTable cg_table;

  // tone curve table
  ToneCurve tone_curves;

  // boost::serialization support
  template <class Archive>
  void serialize(Archive& ar, const unsigned int version) {
    ar & show_object_1 & show_object_2 & show_weather;

    if (version > 0)
      ar & cg_table;

    if (version > 1)
      ar & screen_mode;
  }
};

BOOST_CLASS_VERSION(GraphicsSystemGlobals, 2)

// When marking the screen as dirty, we need to know what kind of
// operation was done
enum GraphicsUpdateType {
  GUT_DRAW_DC0,
  GUT_DRAW_HIK,
  GUT_DISPLAY_OBJ,
  GUT_TEXTSYS,
  GUT_MOUSE_MOTION
};

// Abstract interface to a graphics system. Platform-specific behaviour is
// implemented through IGraphicsBackend instances (see SDLGraphicsBackend for
// the current implementation).
//
// Two device contexts must be allocated during initialization; DC 0,
// which should refer to a surface that is (usually) blitted onto the
// screen immediatly after it is written to, and DC 1, which is simply
// guarenteed to be allocated, and is guarenteed to not be smaller
// then the screen. (Many {rec,grp} functions will load data onto DC1
// and then copy it onto DC0 with some sort of fancy transition
// effect.)
class GraphicsSystem : public EventListener {
 public:
  // The current display context drawing mode. The Reallive system
  // will update the screen after certain events in user code
  // regarding DCs.
  //
  // Note that these are not the only times when the screen will be
  // updated. Most functions that deal with text windows will trigger
  // screen updates. (Object manipulation functions *don't*.) Having
  // this fine level of control is why DCs are often used for smooth
  // animation...
  enum DCScreenUpdateMode {
    // The screen will be redrawn after every load or blit to DC 0.
    SCREENUPDATEMODE_AUTOMATIC,

    // We currently don't understand how this differs from automatic
    // mode. We declare it anyway for compatibility and the hope that
    // someday we will.
    SCREENUPDATEMODE_SEMIAUTOMATIC,

    // The screen is updated after refresh() is called
    SCREENUPDATEMODE_MANUAL
  };

  GraphicsSystem(System& system,
                 Gameexe& gameexe,
                 std::shared_ptr<IGraphicsBackend> backend);
  virtual ~GraphicsSystem();

  std::shared_ptr<IGraphicsBackend> GetBackend() const { return impl_; }

  // Resize window
  void Resize(Size display_size);

  bool is_responsible_for_update() const { return is_responsible_for_update_; }
  void set_is_responsible_for_update(bool in) {
    is_responsible_for_update_ = in;
  }

  // Define who is responsible for screen updates.
  DCScreenUpdateMode screen_update_mode() const { return screen_update_mode_; }
  virtual void SetScreenUpdateMode(DCScreenUpdateMode u);

  inline System& system() { return system_; }

  inline void BindSceneRenderer(std::weak_ptr<ISceneRenderer> scene_renderer) {
    scene_renderer_ = std::move(scene_renderer);
  }

  // Screen Shaking

  // Reads #SHAKE.spec and loads the offsets into the screen shaking queue.
  void QueueShakeSpec(int spec);

  // Returns the current screen origin. This is used for simple #SHAKE.* based
  // screen shaking. While the screen is not shaking, this returns (0,0).
  Point GetScreenOrigin() const;

  // Whether we are currently shaking.
  bool IsShaking() const;

  // How long the current frame in the shaking should last. 10ms if there are
  // no frames.
  int CurrentShakingFrameTime() const;

  // Mouse Cursor Management

  // Whether we are using a custom cursor. Verifies that there was a
  // \#MOUSE_CURSOR entry in the Gameexe.ini file, and that the currently
  // selected cursor exists.
  int ShouldUseCustomCursor();

  // Sets the cursor to the incoming cursor index.
  virtual void SetCursor(int cursor);

  // Returns the current index.
  int cursor() const { return cursor_; }

  // Whether we display a cursor at all.
  void set_show_cursor_from_bytecode(const int in) {
    show_cursor_from_bytecode_ = in;
  }

  // -----------------------------------------------------------------------

  // Individual LongOperations can also hook into the end of the rendering
  // pipeline by injecting Renderables. There should only really be one
  // Renderable on screen at a time, but the interface allows for multiple
  // ones.
  inline void AddRenderable(std::weak_ptr<Renderable> renderable) {
    final_renderers_.emplace_back(renderable);
  }

  // -----------------------------------------------------------------------
  // Subtitle management

  // Sets the current value of the subtitle, as set with title(). This
  // is virtual so that UTF8 or other charset systems can convert for
  // their own internal copy.
  virtual void SetWindowSubtitle(const std::string& cp932str,
                                 int text_encoding);
  void SetWindowSubtitle(std::string utf8str);

  // Returns the current window subtitle, in native encoding.
  std::string window_subtitle() const { return subtitle_; }

  // Wether we should display the subtitle.
  bool should_display_subtitle() const { return display_subtitle_; }

  // Access to the GrapihcsSystem global variables.
  GraphicsSystemGlobals& globals() { return globals_; }

  // The `show object' flags are used to provide a way of enabling or
  // disabling interface elements from the menu. If an object's
  // `ObjectOnOff' property is set to 1 or 2, it will be shown or
  // hidden depending on the corresponding `show object' flag. This is
  // one of the properties controlled by the \#OBJECT variables in
  // gameexe.ini.
  int should_show_object1() const { return globals_.show_object_1; }
  void set_should_show_object1(const int in) { globals_.show_object_1 = in; }
  int should_show_object2() const { return globals_.show_object_2; }
  void set_should_show_object2(const int in) { globals_.show_object_2 = in; }
  int should_show_weather() const { return globals_.show_weather; }
  void set_should_show_weather(const int in) { globals_.show_weather = in; }

  // Sets whether we're in fullscreen mode. SetScreenMode() is virtual so we
  // can tell SDL to switch the screen mode.
  int screen_mode() const { return globals_.screen_mode; }
  virtual void SetScreenMode(const int in);
  void ToggleFullscreen();

  // Toggles whether the interface is shown. Called by
  // PauseLongOperation and related functors.
  void ToggleInterfaceHidden();
  bool is_interface_hidden() { return interface_hidden_; }

  // Whether we should skip animations (such as all the Effect subclasses).
  int should_skip_animations() const { return globals_.skip_animations; }
  void set_should_skip_animations(const int in) {
    globals_.skip_animations = in;
  }

  // Returns the ObjectSettings from the Gameexe for obj_num. The data
  // from this method should be used by all subclasses of
  // GraphicsSystem when deciding whether to render an object or not.
  const ObjectSettings& GetObjectSettings(const int obj_num);

  // Forces a refresh of the screen the next time the graphics system
  // executes.
  virtual void ForceRefresh();

  bool screen_needs_refresh() const { return screen_needs_refresh_; }

  void SetDebugFrameDumpConfig(DebugFrameDumpConfig config);

  void RenderFrame(bool should_refresh = true);
  void RenderCustomFrame(const DrawCallback& draw_scene,
                         const DrawCallback& draw_after = DrawCallback());

  // Draws the screen (as if refresh() was called), but draw to the returned
  // surface instead of the screen.
  std::shared_ptr<SDLSurface> RenderToSurface();

  // Called from the game loop; Does everything that's needed to keep
  // things up.
  void ExecuteGraphicsSystem();

  // Returns the size of the window in pixels.
  Size screen_size() const noexcept { return screen_size_; }
  Size GetDisplaySize() const noexcept { return display_size_; }

  // Maps a position in window coordinates (SDL mouse space) to game screen
  // coordinates. The map includes the letterbox offset and the scale.
  Point DisplayToScreenPoint(const Point& display_pos) const;

  // Returns a rectangle with an origin of (0,0) and a size returned by
  // screen_size().
  Rect screen_rect() const { return screen_rect_; }

  // Loads an image, optionally marking that this image has been loaded (if it
  // is in the game's CGM table).
  std::shared_ptr<SDLSurface> GetSurfaceNamedAndMarkViewed(
      RLMachine& machine,
      const std::string& short_filename);

  // Just loads an image. This shouldn't be used for images that are destined
  // for one of the DCs, since those can be CGs.
  std::shared_ptr<SDLSurface> GetSurfaceNamed(
      const std::string& short_filename);

  // The number of objects in a layer for this game. Defaults to 256 and can be
  // overridden with #OBJECT_MAX.
  int GetObjectLayerSize();

  // Override from EventListener
  virtual void OnEvent(std::shared_ptr<Event> event) override;

  // Reset the system. Should clear all state for when a user loads a game.
  void Reset();

  // Access to the cgtable for the cg* functions.
  CGMTable& cg_table() { return globals_.cg_table; }

  // Access to the tone curve effects file
  ToneCurve& tone_curve() { return globals_.tone_curves; }

  // Gets the emoji surface, if any.
  std::shared_ptr<SDLSurface> GetEmojiSurface();

  // We have a cache of preloaded g00 files.
  void PreloadG00(int slot, const std::string& name);
  void ClearPreloadedG00(int slot);
  void ClearAllPreloadedG00();
  std::shared_ptr<SDLSurface> GetPreloadedG00(const std::string& name);

  // Gets a platform appropriate surface loaded.
  std::shared_ptr<SDLSurface> LoadSurfaceFromFile(
      const std::string& short_filename);

  std::shared_ptr<SDLSurface> CreateSurfaceBGRA(Size size,
                                                std::span<char> data,
                                                bool is_alpha_mask = false);

 protected:
  const Point& cursor_pos() const { return cursor_pos_; }

  std::shared_ptr<MouseCursor> GetCurrentCursor();

  void SetScreenSize(const Size& size);

  RenderFrameConfig BuildPresentationFrameConfig();
  std::optional<std::filesystem::path> NextDebugFrameDumpPath();
  void RollBackDebugFrameDumpCounter();

  void DrawFrame();
  void UpdateWindowTitle();
  std::string ComposeWindowTitle() const;

  // Current screen update mode
  DCScreenUpdateMode screen_update_mode_;

  // Flag set to redraw the screen NOW
  bool screen_needs_refresh_;

  // Whether it is the Graphics system's responsibility to redraw the
  // screen. Some LongOperations temporarily take this responsibility
  // to implement pretty fades and wipes
  bool is_responsible_for_update_;

  // Whether we should try to append subtitle_ in the window
  // titlebar
  bool display_subtitle_;

  // cp932 encoded subtitle string
  std::string subtitle_;
  std::string subtitle_utf8_;

  // Controls whether we render the interface (this can be
  // temporarily toggled by the user at runtime)
  bool interface_hidden_;

  // Mutable global data to be saved in the globals file
  GraphicsSystemGlobals globals_;

  // Size of our screen and display window.
  Size screen_size_;
  Size display_size_;

  // Rectangle of the screen.
  Rect screen_rect_;

  // Queued origin/time pairs. The front of the queue shall be the current
  // screen offset.
  std::queue<std::pair<Point, int>> screen_shake_queue_;

  // The last time |screen_shake_queue_| was modified.
  unsigned int time_at_last_queue_change_;

  // Immutable
  struct GraphicsObjectSettings;
  // Immutable global data that's constructed from the Gameexe.ini file.
  std::unique_ptr<GraphicsObjectSettings> graphics_object_settings_;

  // Whether we should use a custom mouse cursor. Set while parsing the Gameexe
  // file, and then left unchanged. We only use a custom mouse cursor if
  // \#MOUSE_CURSOR is set in the Gameexe
  bool use_custom_mouse_cursor_;

  // Whether we should render any cursor. Controller by the bytecode.
  bool show_cursor_from_bytecode_;

  // Current cursor id. Initially set to \#MOUSE_CURSOR if the key exists.
  int cursor_;

  // Location of the cursor's hotspot
  Point cursor_pos_;

  // Current mouse cursor
  std::shared_ptr<MouseCursor> mouse_cursor_;

  // MouseCursor construction is nontrivial so cache everything we
  // build:
  typedef std::map<int, std::shared_ptr<MouseCursor>> MouseCursorCache;
  MouseCursorCache cursor_cache_;

  std::string caption_title_utf8_;
  std::string current_window_title_;
  Clock window_title_clock_;
  Clock::timepoint_t last_window_title_update_;
  Clock::duration_t window_title_update_interval_;

  // A set of renderers
  std::vector<std::weak_ptr<Renderable>> final_renderers_;

  // Runtime-specific scene renderer. GraphicsSystem owns frame presentation,
  // while this object owns scene traversal and draw order.
  std::weak_ptr<ISceneRenderer> scene_renderer_;

  // Our parent system object.
  System& system_;

  // Graphics backend implementation
  std::shared_ptr<IGraphicsBackend> impl_;

  DebugFrameDumpConfig debug_frame_dump_config_;
  std::uint64_t debug_frame_dump_frame_number_ = 0;

  std::shared_ptr<AssetScanner> asset_scanner_;

  // Preloaded G00 images.
  typedef std::pair<std::string, std::shared_ptr<SDLSurface>> G00ArrayItem;
  typedef LazyArray<G00ArrayItem> G00ScriptList;
  G00ScriptList preloaded_g00_;

  // LRU cache filled with the last fifteen accessed images.
  //
  // This cache's contents are assumed to be immutable.
  LRUCache<std::string, std::shared_ptr<SDLSurface>> image_cache_;

  // boost::serialization support
  friend class boost::serialization::access;

  // boost::serialization forward declaration
  template <class Archive>
  void save(Archive& ar, const unsigned int file_version) const;

  // boost::serialization forward declaration
  template <class Archive>
  void load(Archive& ar, const unsigned int file_version);

  BOOST_SERIALIZATION_SPLIT_MEMBER()
};

BOOST_CLASS_VERSION(GraphicsSystem, 1)
