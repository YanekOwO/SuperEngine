// -----------------------------------------------------------------------
//
// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2006, 2007 Elliot Glaysher
// Copyright (C) 2024 Serina Sakurai
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

#include "machine/rlmachine.hpp"

#include "core/gameexe.hpp"
#include "core/haikei.hpp"
#include "core/memory.hpp"
#include "core/memory_internal/memory.hpp"
#include "core/mouse_cursor.hpp"
#include "core/stage.hpp"
#include "libreallive/expression.hpp"
#include "libreallive/parser.hpp"
#include "log/domain_logger.hpp"
#include "long_operations/reallive_task.hpp"
#include "long_operations/textout_long_operation.hpp"
#include "machine/reallive_dll.hpp"
#include "machine/rlmodule.hpp"
#include "machine/rloperation.hpp"
#include "machine/scene_renderer.hpp"
#include "machine/serialization.hpp"
#include "machine/stack_frame.hpp"
#include "systems/event_system.hpp"
#include "systems/frame_presenter.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "systems/sound_system.hpp"
#include "systems/system.hpp"
#include "systems/text_page.hpp"
#include "systems/text_system.hpp"
#include "utilities/clock.hpp"
#include "utilities/exception.hpp"
#include "utilities/string_utilities.hpp"

#include <algorithm>
#include <exception>
#include <format>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

// -----------------------------------------------------------------------
// RLMachine
// -----------------------------------------------------------------------
static DomainLogger logger("RLMachine");
RLMachine::RLMachine(std::shared_ptr<Gameexe> gameexe,
                     std::unique_ptr<System> system,
                     std::unique_ptr<Stage> stage,
                     std::shared_ptr<IScriptor> scriptor,
                     std::unique_ptr<LocalConfig> local_config,
                     std::unique_ptr<GlobalConfig> global_config,
                     std::unique_ptr<FramePresenter> presenter,
                     std::shared_ptr<WindowTitle> window_title,
                     std::shared_ptr<ImageResources> images,
                     std::unique_ptr<Memory> memory)
    : gexe_(std::move(gameexe)),
      local_config_(std::move(local_config)),
      global_config_(std::move(global_config)),
      stage_(std::move(stage)),
      system_(std::move(system)),
      memory_(memory ? std::move(memory) : std::make_unique<Memory>()),
      module_manager_(ModuleManager::CreatePrototype()),
      scriptor_(std::move(scriptor)),
      presenter_(std::move(presenter)),
      window_title_(std::move(window_title)),
      images_(std::move(images)),
      call_stack_(*memory_),
      savepoint_call_stack_(*memory_) {
  if (!gexe_ || !system_ || !stage_ || !local_config_ || !global_config_ ||
      !presenter_ || !window_title_ || !images_)
    throw std::invalid_argument(
        "RLMachine requires game configuration, system, and stage");

  name_mapping_ = NameMapping(GetGameexe());
  system_->machine_ = this;
  InitializeGraphics();
  auto backend = presenter_->backend();
  haikei_ = std::make_unique<Haikei>(
      presenter_->screen_size(),
      [backend](Size size) { return backend->CreateSurface(size); }, images_,
      system_->event().GetClock());

  renderer_ = std::make_shared<rlSceneRenderer>(*this);
  system_->text().BindGraphics(
      {.images = image_resources(),
       .load_emoji = [this] { return GetEmojiSurface(); },
       .request_refresh = [this] { ForceRefresh(); },
       .interface_hidden = [this] { return is_interface_hidden(); },
       .toggle_interface = [this] { ToggleInterfaceHidden(); },
       .renderer = renderer_});
  MarkSavepoint();
}

RLMachine::~RLMachine() {
  call_stack_.Clear();
  savepoint_call_stack_.Clear();
  system_->text().BindGraphics({});
  system_->machine_ = nullptr;
  stage_->Reset();
  system_->text().Reset();
  haikei_.reset();
  renderer_.reset();
}

void RLMachine::Update() {
  system_->event().ExecuteEventSystem();
  system_->text().ExecuteTextSystem();
  system_->sound().ExecuteSoundSystem();
  ExecuteGraphics();

  if (system_->IsQuitRequested() || IsHalted())
    return;

  if (const auto source = frame_renderer_.lock())
    RenderFrame(*source);
  else
    RenderFrame();
}

Stage& RLMachine::stage() { return *stage_; }

const Stage& RLMachine::stage() const { return *stage_; }

Haikei& RLMachine::haikei() { return *haikei_; }

const Haikei& RLMachine::haikei() const { return *haikei_; }

void RLMachine::MarkSavepoint() {
  savepoint_call_stack_ = call_stack_.Clone();
  savepoint_local_config_ = *local_config_;
  stage().TakeSavepointSnapshot();
  GetSystem().text().TakeSavepointSnapshot();
}

ScenarioConfig RLMachine::GetScenarioConfig() const {
  int current_scenario = call_stack_.Top()->pos.scenario_number;
  auto config = scriptor_->GetScenarioConfig(current_scenario);
  if (local_config_->dont_set_save_point) {
    config.enable_message_savepoint = false;
    config.enable_seentop_savepoint = false;
    config.enable_selcom_savepoint = false;
  }
  return config;
}

void RLMachine::SetMarkSavepoints(const int in) {
  local_config_->dont_set_save_point = !in;
}

std::shared_ptr<Instruction> RLMachine::ReadInstruction() const {
  if (IsHalted())
    return std::make_shared<Instruction>(std::monostate());

  const auto top_frame = call_stack_.Top();
  if (top_frame == nullptr) {
    logger(Severity::Error) << "Stack underflow";
    return std::make_shared<Instruction>(std::monostate());
  }

  if (top_frame->frame_type == StackFrame::TYPE_LONGOP)
    return std::make_shared<Instruction>(std::monostate());

  return scriptor_->ResolveInstruction(top_frame->pos);
}

void RLMachine::ExecuteLongop() {
  lop::AdvanceLongOperation(call_stack_, GetSystem().ShouldFastForward());
}

void RLMachine::ExecuteInstruction(std::shared_ptr<Instruction> instruction) {
  try {
    // write trace log
    static DomainLogger tracer("TRACER");
    tracer(Severity::None) << std::format("({:0>4d}:{:d}) ", SceneNumber(),
                                          line_)
                           << std::visit(InstructionToString(&module_manager_),
                                         *instruction);

    // execute the instruction
    std::visit(*this, *instruction);
  } catch (rlvm::UnimplementedOpcode& e) {
    static DomainLogger logger("Unimplemented");
    logger(Severity::None) << std::format("({:0>4d}:{:d}) ", SceneNumber(),
                                          line_)
                           << e.FormatCommand() << e.FormatParameters();
  } catch (rlvm::Exception& e) {
    static DomainLogger logger;
    auto rec = logger(Severity::Error);
    rec << std::format("({:0>4d}:{:d}) ", SceneNumber(), line_);

    // We specialcase rlvm::Exception because we might have the name of the
    // opcode.
    if (e.operation()) {
      rec << "[" << e.operation()->Name() << "]";
    }

    rec << ":  " << e.what();
  } catch (std::exception& e) {
    auto rec = logger(Severity::Error);
    rec << std::format("({:0>4d}:{:d}) ", SceneNumber(), line_);
    rec << e.what();
  }
}

void RLMachine::AdvanceIP() {
  if (replaying_graphics_stack())
    return;

  const auto it = call_stack_.FindTopRealFrame();
  if (it != nullptr) {
    it->pos = scriptor_->Next(it->pos);

    if (!scriptor_->HasNext(it->pos))
      halted_ = true;
  }
}

void RLMachine::RevertIP() {
  auto& pos = call_stack_.Top()->pos;
  --pos.location_offset;
}

CallStack& RLMachine::GetCallStack() { return call_stack_; }

std::shared_ptr<IScriptor> RLMachine::GetScriptor() { return scriptor_; }

ScriptLocation RLMachine::Location() const {
  auto location = call_stack_.FindTopRealFrame()->pos;
  location.line_num = LineNumber();
  return location;
}

Gameexe& RLMachine::GetGameexe() { return system_->gameexe(); }

Stopwatch& RLMachine::GetTimer(int layer, int idx) {
  static DomainLogger logger("RLTimer");
  if (layer < 0 || layer >= 2 || idx < 0 || idx >= 255) {
    auto rec = logger(Severity::Warn);
    rec << "Invalid key provided when requesting timer. ";
    rec << "(layer=" << layer << " ,idx=" << idx << ')';
  }

  const auto key = std::make_pair(layer, idx);
  static std::shared_ptr<Clock> clock = std::make_shared<Clock>();
  if (!rltimer_.contains(key)) {
    Stopwatch timer(clock);
    timer.Apply(Stopwatch::Action::Run);
    rltimer_.emplace(key, std::move(timer));
  }
  return rltimer_.find(key)->second;
}

FrameCounter* RLMachine::GetFrameCounter(int layer, int idx) {
  static DomainLogger logger("FrameCounter");
  if (layer < 0 || layer >= 2 || idx < 0 || idx >= 255) {
    auto rec = logger(Severity::Warn);
    rec << "Invalid key provided when requesting frame counter. ";
    rec << "(layer=" << layer << " ,idx=" << idx << ')';
  }

  auto it = frame_counter_.find(std::make_pair(layer, idx));
  return it == frame_counter_.end() ? nullptr : &it->second;
}

void RLMachine::SetFrameCounter(int layer, int idx, FrameCounter counter) {
  static DomainLogger logger("FrameCounter");
  if (layer < 0 || layer >= 2 || idx < 0 || idx >= 255) {
    auto rec = logger(Severity::Warn);
    rec << "Invalid key provided when requesting frame counter. ";
    rec << "(layer=" << layer << " ,idx=" << idx << ')';
  }

  frame_counter_.insert_or_assign(std::make_pair(layer, idx),
                                  std::move(counter));
}

void RLMachine::ClearFrameCounter(int layer, int idx) {
  static DomainLogger logger("FrameCounter");
  if (layer < 0 || layer >= 2 || idx < 0 || idx >= 255) {
    auto rec = logger(Severity::Warn);
    rec << "Invalid key provided when clearing frame counter. ";
    rec << "(layer=" << layer << " ,idx=" << idx << ')';
  }

  frame_counter_.erase(std::make_pair(layer, idx));
}

void RLMachine::PushLongOperation(
    std::shared_ptr<lop::LongOperation> long_operation) {
  const auto top_frame = call_stack_.Top();
  auto pos = top_frame->pos;
  pos.location_offset--;  // location associated with this longop is previous
                          // location of the instruction pointer.
  call_stack_.Push(StackFrame(
      pos, std::make_shared<lop::RealliveTask>(
               std::move(long_operation), GetSystem().event().GetClock())));
}

void RLMachine::Reset() {
  call_stack_.Clear();
  savepoint_call_stack_.Clear();
  stage().Reset();
  haikei().Reset();
  screen_update_mode_ = SCREENUPDATEMODE_AUTOMATIC;
  rendered_custom_frame_ = false;
  ResetGraphics();
  GetSystem().Reset();
  ForceRefresh();
}

void RLMachine::LocalReset() {
  savepoint_call_stack_.Clear();
  memory_->PartialReset(LocalMemory());
  *local_config_ = MakeRealliveLocalConfig(GetGameexe());
  savepoint_local_config_ = *local_config_;
  stage().Reset();
  haikei().Reset();
  screen_update_mode_ = SCREENUPDATEMODE_AUTOMATIC;
  rendered_custom_frame_ = false;
  ResetGraphics();
  GetSystem().Reset();
  ForceRefresh();
}

std::shared_ptr<lop::RealliveTask> RLMachine::CurrentLongOperation() const {
  auto top = call_stack_.Top();
  if (top && top->frame_type == StackFrame::TYPE_LONGOP)
    return top->long_op;

  return nullptr;
}

int RLMachine::SceneNumber() const {
  return call_stack_.Top()->pos.scenario_number;
}

int RLMachine::GetTextEncoding() const {
  auto scenario_number = call_stack_.Top()->pos.scenario_number;
  return scriptor_->GetScenarioConfig(scenario_number).text_encoding;
}

bool RLMachine::DllLoaded(const std::string& name) {
  for (auto const& dll : loaded_dlls_) {
    if (dll.second->GetDLLName() == name)
      return true;
  }

  return false;
}

void RLMachine::LoadDLL(int slot, const std::string& name) {
  RealLiveDLL* dll = RealLiveDLL::BuildDLLNamed(*this, name);
  if (dll) {
    loaded_dlls_.emplace(slot, std::unique_ptr<RealLiveDLL>(dll));
  } else {
    std::ostringstream oss;
    oss << "Can't load emulated DLL named '" << name << "'";
    throw std::runtime_error(oss.str());
  }
}

void RLMachine::UnloadDLL(int slot) { loaded_dlls_.erase(slot); }

int RLMachine::CallDLL(int slot,
                       int one,
                       int two,
                       int three,
                       int four,
                       int five) {
  DLLMap::iterator it = loaded_dlls_.find(slot);
  if (it != loaded_dlls_.end()) {
    return it->second->CallDLL(*this, one, two, three, four, five);
  } else {
    std::ostringstream oss;
    oss << "Attempt to callDLL(" << one << ", " << two << ", " << three << ", "
        << four << ", " << five << ") on slot " << slot
        << " when no DLL is loaded there!";
    throw std::runtime_error(oss.str());
  }
}

void RLMachine::Halt() { halted_ = true; }

void RLMachine::AddLineAction(const int seen,
                              const int line,
                              std::function<void(void)> function) {
  if (!on_line_actions_)
    on_line_actions_.reset(new ActionMap);

  (*on_line_actions_)[std::make_pair(seen, line)] = function;
}

// -----------------------------------------------------------------------

void RLMachine::PerformTextout(std::string cp932str) {
  std::string name_parsed_text;
  try {
    // Name parser. Takes a raw, local machine encoded string and replaces name
    // variable placeholders with the names from Memory.
    MemoryHandle& memory = GetMemory();
    const char* cur = cp932str.c_str();

    const char LOWER_BYTE_FULLWIDTH_ASTERISK = 0x96;
    const char LOWER_BYTE_FULLWIDTH_PERCENT = 0x93;

    while (*cur) {
      if (cur[0] == 0x81 && (cur[1] == LOWER_BYTE_FULLWIDTH_ASTERISK ||
                             cur[1] == LOWER_BYTE_FULLWIDTH_PERCENT)) {
        char type = cur[1];
        cur += 2;

        string strindex;
        if (ReadFullwidthLatinLetter(cur, strindex)) {
          // Try to read a second character. We don't care if it fails.
          ReadFullwidthLatinLetter(cur, strindex);
        } else {
          // It was "as-is" character, not a name reference.
          name_parsed_text += (char)0x81;
          name_parsed_text += type;
          continue;
        }

        int index = ConvertLetterIndexToInt(strindex);
        if (type == LOWER_BYTE_FULLWIDTH_ASTERISK)
          name_parsed_text += memory.Read(StrBank::global_name, index);
        else
          name_parsed_text += memory.Read(StrBank::local_name, index);
      } else {
        CopyOneShiftJisCharacter(cur, name_parsed_text);
      }
    }
  } catch (std::exception&) {
    // WEIRD: Sometimes rldev (and the official compiler?) will generate
    // strings that aren't valid shift_jis. Fall back while I figure out how
    // to handle this.
    name_parsed_text = cp932str;
  }

  std::string utf8str = cp932toUTF8(name_parsed_text, GetTextEncoding());
  // Display UTF-8 characters
  auto ptr = std::make_shared<lop::TextoutLongOperation>(*this, utf8str);

  if (GetSystem().ShouldFastForward() ||
      EffectiveMessageNowait(GetLocalConfig(), GetGlobalConfig())) {
    ptr->set_no_wait();
  }

  PushLongOperation(ptr);
}

// -----------------------------------------------------------------------

void RLMachine::operator()(std::monostate) {}

void RLMachine::operator()(Kidoku k) {
  // Check to see if we mark savepoints on textout
  if (GetScenarioConfig().enable_message_savepoint &&
      system_->text().GetCurrentPage().number_of_chars_on_page() == 0) {
    MarkSavepoint();
  }

  // Mark if we've previously read this piece of text.
  system_->text().SetKidokuRead(
      kidoku_table_.HasBeenRead(SceneNumber(), k.num));

  // Record the kidoku pair in global memory.
  kidoku_table_.RecordKidoku(SceneNumber(), k.num);
}

void RLMachine::operator()(Line l) {
  line_ = l.num;

  if (on_line_actions_) {
    ActionMap::iterator it =
        on_line_actions_->find(std::make_pair(SceneNumber(), line_));
    if (it != on_line_actions_->end()) {
      it->second();
    }
  }
}

void RLMachine::operator()(rlCommand cmd) {
  auto f = cmd.cmd;

  auto op = module_manager_.Dispatch(*f);
  if (op == nullptr) {  // unimplemented opcode
    throw rlvm::UnimplementedOpcode("", *f);
  }

  try {
    op->DispatchFunction(*this, *f);
  } catch (rlvm::Exception& e) {
    e.setOperation(op);
    throw e;
  }
}

void RLMachine::operator()(rlExpression e) { e.Execute(*this); }

void RLMachine::operator()(Textout t) { PerformTextout(std::move(t.text)); }

void RLMachine::operator()(End) { Halt(); }

// ------------------------------------------------------------------------------
void RLMachine::ReplayGraphicsStackCommand() {
  Stage& stage = this->stage();
  std::deque<std::string> stack;
  stack.swap(stage.graphics_stack);

  set_replaying_graphics_stack(true);
  {
    try {
      for (auto const& command : stack) {
        if (command != "") {
          // Parse the string as a chunk of Reallive bytecode.
          libreallive::Parser parser;
          auto element = parser.ParseBytecode(command.c_str(),
                                              command.c_str() + command.size());
          if (auto command =
                  std::dynamic_pointer_cast<libreallive::CommandElement>(
                      element)) {
            operator()(rlCommand(command.get()));
          }
        }
      }
    } catch (std::exception& e) {
      std::cerr << "Error while replaying graphics stack: " << e.what()
                << std::endl;
    }
  }
  set_replaying_graphics_stack(false);
}

// -----------------------------------------------------------------------
// Graphics
// -----------------------------------------------------------------------

static std::optional<std::pair<int, int>> parse_range(std::string_view sv) {
  // Accepts "N" or "A:B" (whitespace allowed). Returns nullopt if invalid.
  sv = trim_sv(sv);
  if (auto pos = sv.find(':'); pos == std::string_view::npos) {
    int v;
    if (!parse_int(sv, v))
      return std::nullopt;
    return std::make_pair(v, v);
  } else {
    int a, b;
    if (!parse_int(sv.substr(0, pos), a))
      return std::nullopt;
    if (!parse_int(sv.substr(pos + 1), b))
      return std::nullopt;
    return std::make_pair(a, b);
  }
}
RLMachine::GraphicsObjectSettings::GraphicsObjectSettings(Gameexe& gameexe) {
  static constexpr std::vector<int> empty_ivec;

  objects_in_a_layer = gameexe("OBJECT_MAX").Int().value_or(256);

  // First we populate everything with the special value
  position = std::vector<unsigned char>(objects_in_a_layer, 0);

  data.emplace_back(gameexe("OBJECT.999").IntVec().value_or(empty_ivec));

  // Read the #OBJECT.xxx entries from the Gameexe
  for (auto it : gameexe.Filter("OBJECT.")) {
    std::string_view key = it.key();
    size_t dot_pos = key.find('.');
    if (dot_pos == std::string_view::npos || dot_pos + 1 >= key.size())
      continue;

    std::string_view s = key.substr(dot_pos + 1);
    int lo, hi;
    if (auto maybe = parse_range(s); maybe && maybe->first <= maybe->second)
      std::tie(lo, hi) = *maybe;
    else
      continue;

    for (int obj_num : std::views::iota(lo, hi + 1)) {
      if (obj_num != 999 && obj_num < objects_in_a_layer) {
        position[obj_num] = data.size();
        data.emplace_back(it.IntVec().value_or(empty_ivec));
      }
    }
  }
}

// -----------------------------------------------------------------------

const ObjectSettings& RLMachine::GraphicsObjectSettings::GetObjectSettingsFor(
    int obj_num) const {
  return data.at(position[obj_num]);
}

void RLMachine::InitializeGraphics() {
  cg_table_ = CreateCGMTable(GetGameexe());
  graphics_object_settings_ =
      std::make_unique<GraphicsObjectSettings>(GetGameexe());
  window_title_->LoadReallive(GetGameexe());
  window_title_->Apply();
  presenter().backend()->ShowSystemCursor(!ShouldUseCustomCursor());
}

void RLMachine::ForceRefresh() {
  screen_needs_refresh_ = true;
  if (screen_update_mode_ == SCREENUPDATEMODE_MANUAL)
    GetSystem().set_force_wait(true);
}

void RLMachine::Resize(Size size) {
  presenter().Resize(size, GetGlobalConfig().screen_size_mode == 1);
  ForceRefresh();
}

void RLMachine::ToggleInterfaceHidden() {
  interface_hidden_ = !interface_hidden_;
}

void RLMachine::ExecuteGraphics() {
  renderer_->ExecuteFrame();
  if (mouse_cursor_)
    mouse_cursor_->Execute();
}

void RLMachine::SetFrameRenderer(std::weak_ptr<DrawCallback> source) {
  frame_renderer_ = std::move(source);
}

void RLMachine::RenderFrame(const DrawCallback& draw) {
  if (rendered_custom_frame_ && !draw)
    ForceRefresh();
  FrameRenderOptions options;
  switch (screen_update_mode_) {
    case SCREENUPDATEMODE_AUTOMATIC:
    case SCREENUPDATEMODE_SEMIAUTOMATIC:
      break;
    case SCREENUPDATEMODE_MANUAL:
      options.redraw_scene = bool(draw) || screen_needs_refresh_;
      options.cache_scene = true;
      break;
    default:
      throw std::runtime_error("RLMachine: Invalid screen update mode " +
                               std::to_string(screen_update_mode_));
  }
  auto cursor = [this] {
    if (ShouldUseCustomCursor() && GetSystem().event().mouse_inside_window())
      if (auto current = GetCurrentCursor())
        current->RenderHotspotAt(cursor_pos_);
  };
  presenter().RenderFrame(
      options, draw ? draw : [this] { renderer_->RenderScene(); }, cursor,
      renderer_->GetScreenOrigin());
  rendered_custom_frame_ = bool(draw);
  if (!draw && options.redraw_scene)
    screen_needs_refresh_ = false;
}

std::shared_ptr<SDLSurface> RLMachine::RenderToSurface() {
  return presenter().RenderToSurface([this] { renderer_->RenderScene(); },
                                     renderer_->GetScreenOrigin());
}

void RLMachine::ResetGraphics() {
  frame_renderer_.reset();
  images_->ClearPreloadedImages();
  mouse_cursor_.reset();
  presenter().backend()->ShowSystemCursor(!ShouldUseCustomCursor());
  window_title_->subtitle_.clear();
  window_title_->Apply();
  interface_hidden_ = false;
}

void RLMachine::ApplyGraphicsConfiguration() {
  Resize(presenter().screen_size());
  mouse_cursor_.reset();
  presenter().backend()->ShowSystemCursor(!ShouldUseCustomCursor());
  window_title_->subtitle_visible_ = GetGlobalConfig().reallive.display_subtitle;
  window_title_->Apply();
  ForceRefresh();
}

void RLMachine::ApplyConfiguration() {
  ApplyGraphicsConfiguration();
  GetSystem().ApplyConfiguration();
}

std::shared_ptr<SDLSurface> RLMachine::GetSurfaceNamed(
    const std::string& name) {
  return images_->GetSurfaceNamed(name);
}

int RLMachine::ShouldUseCustomCursor() {
  return GetGlobalConfig().reallive.use_custom_mouse_cursor &&
         GetGameexe()("MOUSE_CURSOR", GetLocalConfig().cursor_no, "NAME")
                 .Str()
                 .value_or("") != "";
}

std::shared_ptr<SDLSurface> RLMachine::GetEmojiSurface() {
  for (auto it : GetGameexe().Filter("E_MOJI.")) {
    // Try to interpret each key as a filename.
    std::string file_name = it.Str().value_or("");
    std::shared_ptr<SDLSurface> surface = GetSurfaceNamed(file_name);
    if (surface)
      return surface;
  }

  return nullptr;
}

std::shared_ptr<SDLSurface> RLMachine::GetSurfaceNamedAndMarkViewed(
    const std::string& short_filename) {
  // Record that we viewed this CG.
  cg_table().SetViewed(short_filename);

  // Set the intZ[] flag
  int flag = cg_table().GetFlag(short_filename);
  if (flag != -1) {
    GetMemory().Write(
        libreallive::IntMemRef(libreallive::INTZ_LOCATION, 0, flag), 1);
  }

  return GetSurfaceNamed(short_filename);
}

std::shared_ptr<MouseCursor> RLMachine::GetCurrentCursor() {
  if (!GetGlobalConfig().reallive.use_custom_mouse_cursor ||
      GetLocalConfig().cursor_disp_off)
    return std::shared_ptr<MouseCursor>();

  if (GetGlobalConfig().reallive.use_custom_mouse_cursor && !mouse_cursor_) {
    MouseCursorCache::iterator it =
        cursor_cache_.find(GetLocalConfig().cursor_no);
    if (it != cursor_cache_.end()) {
      mouse_cursor_ = it->second;
    } else {
      std::shared_ptr<SDLSurface> cursor_surface;
      GameexeInterpretObject cursor =
          GetGameexe()("MOUSE_CURSOR", GetLocalConfig().cursor_no);

      if (auto name = cursor("NAME").Str()) {
        int count = cursor("CONT").Int().value_or(1);
        int speed = cursor("SPEED").Int().value_or(800);

        cursor_surface = GetSurfaceNamed(*name);
        mouse_cursor_ = std::make_shared<MouseCursor>(
            GetSystem().event().GetClock(), cursor_surface, count, speed);
        cursor_cache_[GetLocalConfig().cursor_no] = mouse_cursor_;
      } else {
        mouse_cursor_.reset();
      }
    }
  }

  return mouse_cursor_;
}
