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

#include "core/selection.hpp"

#include "core/gameexe.hpp"
#include "core/input.hpp"
#include "systems/graphics_system.hpp"
#include "systems/sdl/sdl_surface.hpp"
#include "systems/system.hpp"
#include "systems/text_page.hpp"
#include "systems/text_system.hpp"
#include "systems/text_window.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

BtnSelTemplate BtnSelTemplate::ParseSiglus(Gameexe& gexe, int n) {
  if (n < 0 || n >= gexe("SELBTN.CNT").Int().value_or(16))
    throw std::runtime_error("Invalid Siglus selection template: " +
                             std::to_string(n));
  auto key = gexe("SELBTN", n);
  BtnSelTemplate c;
  c.base_file = key("BASE_FILE").Str().value_or("");
  c.filter_file = key("FILTER_FILE").Str().value_or("");
  const auto point = [&](const char* name, Point fallback = {}) {
    auto values = key(name).IntVec();
    return values && values->size() >= 2 ? Point((*values)[0], (*values)[1])
                                         : fallback;
  };
  c.base_pos = point("BASE_POS");
  c.rep_pos = point("REP_POS");
  c.text_pos = point("MOJI_POS");
  const Point align = point("ALIGN");
  c.x_align = align.x();
  c.y_align = align.y();
  const Point text_align = point("MOJI_ALIGN");
  c.text_x_align = text_align.x();
  c.text_y_align = text_align.y();
  c.max_rows = key("MAX_Y_CNT").Int().value_or(0);
  c.column_width = key("LINE_WIDTH").Int().value_or(100);
  auto size = key("MOJI_SIZE").IntVec();
  if (size && !size->empty())
    c.font_size = (*size)[0];
  if (size && size->size() > 1)
    c.x_spacing = (*size)[1];
  if (size && size->size() > 2)
    c.y_spacing = (*size)[2];
  if (size && size->size() > 3)
    c.text_count = (*size)[3];
  c.color = key("MOJI_COLOR").Int().value_or(0);
  c.hit_color = key("MOJI_HIT_COLOR").Int().value_or(5);
  c.action = key("BTN_ACTION").Int().value_or(-1);

  const auto animation = [&](const char* name, int& type, int& time) {
    auto v = key(name).IntVec();
    if (v && !v->empty())
      type = (*v)[0];
    if (v && v->size() > 1)
      time = std::max((*v)[1], 0);
  };
  animation("OPEN_ANIME", c.open_type, c.open_time);
  animation("CLOSE_ANIME", c.close_type, c.close_time);
  animation("DECIDE_ANIME", c.decide_type, c.decide_time);

  return c;
}

BtnSelTemplate BtnSelTemplate::ParseReallive(Gameexe& gexe, int number) {
  auto key = gexe("SELBTN", number);
  BtnSelTemplate c;
  c.format = Format::Reallive;
  c.open_type = c.close_type = c.decide_type = 0;
  c.base_file = key("NAME").Str().value_or("");
  c.filter_file = key("BACK").Str().value_or("");
  const auto point = [&](const char* name) {
    auto v = key(name).IntVec();
    return v && v->size() >= 2 ? Point((*v)[0], (*v)[1]) : Point();
  };
  c.base_pos = point("BASEPOS");
  c.rep_pos = point("REPPOS");
  const Point center = point("CENTERING");
  c.center_x = center.x() != 0;
  c.center_y = center.y() != 0;
  c.x_align = c.center_x ? 1 : 0;
  c.y_align = c.center_y ? 1 : 0;
  c.font_size = key("MOJISIZE").Int().value_or(25);
  c.color = key("MOJIDEFAULTCOL").Int().value_or(0);
  c.hit_color = key("MOJISELECTCOL").Int().value_or(c.color);
  if (c.hit_color == c.color)
    c.hit_color = 1;  // CLANNAD
  if (c.hit_color == -1)
    c.hit_color = c.color;  // Little Busters
  c.frame_actions.emplace();
  const auto frame = [&](const char* name, ButtonState state) {
    if (auto v = key(name).IntVec(); v && v->size() >= 3)
      c.frame_actions->SetState(
          state, {.pattern = (*v)[0], .rep_pos = Point((*v)[1], (*v)[2])});
  };
  frame("NORMAL", ButtonState::Normal);
  frame("SELECT", ButtonState::Hit);
  frame("PUSH", ButtonState::Push);
  frame("DONTSEL", ButtonState::Disable);
  return c;
}

static RGBColour Color(Gameexe& gexe, int index, RGBColour fallback) {
  auto v = gexe("COLOR_TABLE", index).IntVec();
  return v && v->size() >= 3 ? RGBColour((*v)[0], (*v)[1], (*v)[2]) : fallback;
}

std::shared_ptr<Selection> BuildSelection(System& system,
                                          SelectionRequest request,
                                          BtnSelTemplate config,
                                          std::shared_ptr<TextWindow> window) {
  const bool buttons = request.kind == SelectionRequest::Kind::Buttons;
  auto s = std::make_shared<Selection>(std::move(request), config);
  s->window = std::move(window);
  Gameexe& gexe = system.gameexe();
  TextSystem& text = system.text();
  GraphicsSystem& graphics = system.graphics();
  if (!buttons && !s->window)
    throw std::invalid_argument("Window selection requires a target window");
  const bool reallive = config.format == BtnSelTemplate::Format::Reallive;

  if (s->window) {
    // Window selections inherit message styling and honor any deferred clear
    // before measuring their options in the remaining text area.
    TextWindow& w = *s->window;
    if (w.message_state()->clear_ready) {
      w.ClearWin();
      text.NewPageOnWindow(s->window);
      w.message_state()->clear_ready = false;
    }
    s->sorter = w.sorter();
    config.font_size = w.font_size_in_pixels();
    config.x_spacing = w.Layout().x_spacing;
    config.y_spacing = w.Layout().y_spacing;
    if (!reallive) {
      config.color =
          gexe("MWND", w.window_number(), "MOJI_COLOR").Int().value_or(-1);
      if (config.color < 0)
        config.color = gexe("MWND.MOJI_COLOR").Int().value_or(0);
    }
    config.hit_color = config.color;
  } else {
    if (!config.base_file.empty())
      s->base = graphics.GetSurfaceNamed(config.base_file);
    if (!config.filter_file.empty())
      s->filter = graphics.GetSurfaceNamed(config.filter_file);
    const auto& actions = text.mwnd_config().button_actions();
    if (config.action >= 0 &&
        static_cast<std::size_t>(config.action) < actions.GetCount())
      s->button_action = actions.GetEntry(config.action);
    if (config.frame_actions)
      s->button_action = *config.frame_actions;
    if (reallive) {
      auto current = text.GetCurrentMwnd();
      s->filter_colour = current->GetColour();
      s->filter_mode = current->GetIsFilter();
    }
  }
  s->config = config;
  auto shadow = Color(
      gexe, reallive ? 255 : gexe("MWND.SHADOW_COLOR").Int().value_or(255),
      RGBColour::Black());
  const auto normal_color = reallive && s->window
                                ? s->window->GetFontColor()
                                : Color(gexe, config.color, RGBColour::White());
  const auto hit_color = reallive && s->window
                             ? normal_color
                             : Color(gexe, config.hit_color, normal_color);
  Size button_size(0, 0);
  for (std::size_t i = 0; i < s->items.size(); ++i) {
    auto& option = s->request.options[i];
    if (!reallive)
      option.text = text.InterpretName(option.text);
    auto& item = s->items[i];
    if (option.type == 0)
      continue;
    const auto color = option.color >= 0
                           ? Color(gexe, option.color, normal_color)
                           : normal_color;
    if (!option.text.empty()) {
      item.normal =
          text.RenderText(option.text, config.font_size, config.x_spacing,
                          config.y_spacing, color, &shadow, config.text_count);
      item.highlighted = text.RenderText(
          option.text, config.font_size, config.x_spacing, config.y_spacing,
          option.type == 2 ? color : hit_color, &shadow, config.text_count);
      // Surface storage includes a two-pixel shadow and trailing spacing.
      // Neither belongs to Siglus' logical text or selection rectangle.
      const Size storage = item.normal->GetSize();
      item.size = Size(std::max(0, storage.width() - 2 - config.x_spacing),
                       std::max(0, storage.height() - 2));
    } else
      item.size = Size(0, config.font_size);
    button_size = Size(std::max(button_size.width(), item.size.width()),
                       std::max(button_size.height(), item.size.height()));
  }
  if (buttons) {
    // A button image fixes the common hit size; text-only buttons use the
    // largest measured option instead.
    if (s->base)
      button_size =
          s->base->GetPattern(reallive ? s->button_action.normal.pattern : 0)
              .rect.size();
    if (reallive && s->filter)
      button_size = s->filter->GetSize();
    s->LayoutButtons(button_size, graphics.screen_size());
  } else {
    const auto& waku = text.mwnd_config().GetWaku(s->window->waku_set());
    const bool variable = waku.style != MwndConfig::WakuStyle::Fixed;
    if (s->window->Layout().insertion_x != 0)
      text.GetPageOnWindow(s->window).HardBrake();
    const int start_y = s->window->Layout().insertion_y;
    const Size size =
        s->LayoutWindow(s->window->GetTextWindowSize(), config.font_size,
                        s->window->line_height(), start_y, variable);
    if (variable)
      s->window->SetSelectionSize(size);
    s->window->SetVisible(true);
  }
  return s;
}

// ------------------------------------------------------------------------------
// class Selection
Selection::Selection(SelectionRequest r, BtnSelTemplate c)
    : request(std::move(r)),
      config(std::move(c)),
      items(request.options.size()) {}

void Selection::LayoutButtons(Size size, Size screen) {
  origin = config.base_pos;
  if (config.center_x)
    origin.set_x(screen.width() / 2);
  if (config.center_y)
    origin.set_y(screen.height() / 2);

  Point offset, minimum, maximum;
  bool placed = false;
  int column = 0, row = 0;
  for (std::size_t i = 0; i < items.size(); ++i) {
    // Off entries consume no slot. Disabled and hidden entries still advance
    // placement, while items retain the script's original indices.
    if (request.options[i].type == 0)
      continue;
    items[i].position = offset;
    items[i].size = size;
    minimum = Point(std::min(minimum.x(), offset.x()),
                    std::min(minimum.y(), offset.y()));
    maximum = Point(std::max(maximum.x(), offset.x() + size.width()),
                    std::max(maximum.y(), offset.y() + size.height()));
    placed = true;
    offset += config.rep_pos;
    if (config.max_rows > 0 && ++row >= config.max_rows) {
      offset = Point(++column * config.column_width, 0);
      row = 0;
    }
  }
  if (!placed)
    return;

  const Point align(config.x_align == 1   ? -(minimum.x() + maximum.x()) / 2
                    : config.x_align == 2 ? -maximum.x()
                                          : 0,
                    config.y_align == 1   ? -(minimum.y() + maximum.y()) / 2
                    : config.y_align == 2 ? -maximum.y()
                                          : 0);
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (request.options[i].type == 0)
      continue;
    auto& item = items[i];
    item.position += align;
    item.hit_rect = Rect(item.position, size);
  }
}

Size Selection::LayoutWindow(Size area,
                             int font,
                             int line,
                             int start_y,
                             bool variable) {
  // Fixed windows use column-major slots; variable windows grow vertically
  // using each option's measured height and the window's extra line spacing.
  const int rows =
      std::max(1, (area.height() - start_y + line - font) / std::max(line, 1));
  const int count =
      std::count_if(request.options.begin(), request.options.end(),
                    [](const auto& option) { return option.type != 0; });
  const int columns = std::max(1, (count + rows - 1) / rows);
  const int width = area.width() / columns;

  int slot = 0, y = start_y, max_width = 0;
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (request.options[i].type == 0)
      continue;
    auto& item = items[i];
    if (variable) {
      item.position = Point(0, y);
      y += item.size.height() + line - font;
      max_width = std::max(max_width, item.size.width());
    } else {
      item.position = Point(slot / rows * width, start_y + slot % rows * line);
      item.hit_rect = Rect(item.position, Size(width, line));
    }
    ++slot;
  }

  if (!variable)
    return area;

  for (auto& item : items) {
    item.hit_rect =
        Rect(item.position, Size(max_width, item.size.height() + line - font));
  }
  return Size(max_width, std::max(0, y - (line - font)));
}

int Selection::AnimationRows() const {
  // Staggering uses original option indices modulo this row count,
  // independently of how off/hidden options affect layout placement.
  const int count = static_cast<int>(items.size());
  return std::max(
      1, config.max_rows > 0 ? std::min(config.max_rows, count) : count);
}

void Selection::Start() {
  elapsed_ = 0;
  phase_ =
      request.kind == SelectionRequest::Kind::Buttons && config.open_type > 0
          ? Phase::Opening
          : Phase::Waiting;
}

void Selection::Close() {
  elapsed_ = 0;
  phase_ =
      request.kind == SelectionRequest::Kind::Buttons && config.close_type > 0
          ? Phase::Closing
          : Phase::Finished;
  if (phase_ == Phase::Finished)
    capture.reset();
}

void Selection::Advance(int ms) {
  elapsed_ +=
      std::min(std::max(ms, 0), std::numeric_limits<int>::max() - elapsed_);
  const int open_end =
      config.open_time + (config.open_type >= 2 ? 50 * AnimationRows() : 0);
  // After an accepted choice's decide animation, only the chosen option
  // remains; close it with a fade. Cancellation keeps the configured exit.
  const int close_type =
      result && *result >= 0 && config.decide_type > 0 && config.close_type > 0
          ? 1
          : config.close_type;
  const int close_end =
      config.close_time + (close_type >= 2 ? 50 * AnimationRows() : 0);
  // Deliberately cross one phase boundary per update, starting the next
  // animation at zero so a long frame cannot skip its first presentation.
  if (phase_ == Phase::Opening && elapsed_ >= open_end) {
    phase_ = Phase::Waiting;
    elapsed_ = 0;
  } else if (phase_ == Phase::Deciding && elapsed_ >= config.decide_time) {
    Close();
  } else if (phase_ == Phase::Closing && elapsed_ >= close_end) {
    phase_ = Phase::Finished;
    capture.reset();
  }
}

bool Selection::CanInput() const {
  // Opening completion also needs the runtime's capture/start hook to run.
  return phase_ == Phase::Waiting && open_hook_done;
}

bool Selection::Choose(int index) {
  if (!CanInput() || index < -1 || index >= static_cast<int>(items.size()))
    return false;
  if (index == -1 ? !request.cancel_enabled : request.options[index].type != 1)
    return false;
  result = index;
  pressed.reset();
  elapsed_ = 0;
  if (request.kind == SelectionRequest::Kind::Buttons && index >= 0 &&
      config.decide_type > 0)
    phase_ = Phase::Deciding;
  else
    Close();
  return true;
}

bool Selection::CanResume() const {
  return result && (request.sync_type == 2 || phase_ == Phase::Finished ||
                    (request.sync_type == 1 && phase_ == Phase::Closing));
}

bool Selection::Visible() const {
  return phase_ != Phase::Prepared && phase_ != Phase::Finished;
}

Selection::Presentation Selection::GetPresentation(int i, Size screen) const {
  Presentation p;
  const auto progress = [&](int time, int duration) {
    return duration <= 0 ? (time >= 0 ? 1.0 : 0.0)
                         : std::clamp(double(time) / duration, 0.0, 1.0);
  };
  const int row = i % AnimationRows();
  if (phase_ == Phase::Opening || phase_ == Phase::Closing) {
    const bool open = phase_ == Phase::Opening;
    const int type = open ? config.open_type
                     : result && *result >= 0 && config.decide_type > 0 &&
                             config.close_type > 0
                         ? 1
                         : config.close_type;
    if (type == 0)
      return p;
    // Vertical type 2 enters in reverse row order; type 3 exits in reverse.
    // The same 50 ms row delays are included in Advance's phase durations.
    const int delay = type < 2
                          ? 0
                          : 50 * (((open && type == 2) || (!open && type == 3))
                                      ? AnimationRows() - row
                                      : row);
    const double t =
        progress(elapsed_ - delay, open ? config.open_time : config.close_time);
    p.alpha = static_cast<int>(255 * (open ? t : 1 - t));
    // Quadratic easing decelerates entry and accelerates exit. Type 6
    // alternates horizontal directions between rows.
    const double move = open ? (1 - t) * (1 - t) : t * t;
    const int sign = type == 2 || type == 4 ? -1
                     : type == 6 && row % 2 ? -1
                                            : 1;
    if (type == 2 || type == 3)
      p.offset = Point(0, static_cast<int>(sign * screen.height() * move));
    if (type >= 4)
      p.offset = Point(static_cast<int>(sign * screen.width() * move), 0);
    if (!open && result && *result >= 0 && config.decide_type > 0 &&
        i != *result)
      p.alpha = 0;
  } else if (phase_ == Phase::Deciding && result && *result >= 0 &&
             i != *result) {
    const double t = progress(elapsed_, config.decide_time);
    p.alpha = static_cast<int>(255 * (1 - t));
    if (config.decide_type == 2) {
      const Point d(items[*result].position - items[i].position);
      const double move = 1 - (1 - t) * (1 - t);
      p.offset =
          Point(static_cast<int>(d.x() * move), static_cast<int>(d.y() * move));
    }
  }
  return p;
}

std::shared_ptr<Selection> Selection::Clone() const {
  // Stage wipes need the exact visual phase and shared surfaces. Input and
  // VM waiting stay with the runtime's original session.
  auto clone = std::make_shared<Selection>(*this);
  clone->focused.reset();
  clone->pressed.reset();
  return clone;
}

Point Selection::Origin() const {
  return window ? window->GetTextSurfaceRect().origin() : origin;
}

// ------------------------------------------------------------------------------
// struct SelState
SelState::SelState()
    : pending_(std::make_unique<lop::PendingCoroutineTasks>()) {}
SelState::~SelState() = default;

void SelState::Render(const Selection& s, System& sys) {
  TextSystem& text = sys.text();
  GraphicsSystem& graphics = sys.graphics();
  if (!s.Visible() || !text.system_visible() || graphics.is_interface_hidden())
    return;
  if (s.window && !s.window->IsVisible())
    return;

  const bool buttons = s.request.kind == SelectionRequest::Kind::Buttons;
  for (std::size_t i = 0; i < s.items.size(); ++i) {
    const auto& option = s.request.options[i];
    if (option.type != 1 && option.type != 2)
      continue;
    const auto& item = s.items[i];
    auto animation = s.GetPresentation(i, graphics.screen_size());
    const auto state =
        option.type == 2 ? ButtonState::Disable
        : s.pressed == static_cast<int>(i) || s.result == static_cast<int>(i)
            ? ButtonState::Push
        : s.focused == static_cast<int>(i) ? ButtonState::Hit
                                           : ButtonState::Normal;
    const auto action =
        buttons ? s.button_action.GetState(state) : ButtonActionTable::State{};
    const Point pos =
        s.Origin() + item.position + animation.offset + action.rep_pos;
    const int alpha = animation.alpha * action.rep_tr / 255;
    const bool reallive = s.config.format == BtnSelTemplate::Format::Reallive;
    if (buttons && reallive && s.filter) {
      auto color = s.filter_colour;
      color.set_alpha(color.a() * alpha / 255);
      s.filter->RenderToScreenAsColorMask(
          s.filter->GetRect(), Rect(pos, item.size), color, s.filter_mode);
    }
    if (buttons && s.base) {
      const auto& pattern = s.base->GetPattern(action.pattern);
      s.base->RenderToScreen(
          pattern.rect, Rect(pos, reallive ? item.size : pattern.rect.size()),
          alpha, action.rep_bright, action.rep_dark);
    }
    if (buttons && !reallive && s.filter) {
      const auto color = sys.global_config().filter_color;
      s.filter->RenderToScreenWithColourAddition(
          s.filter->GetRect(), Rect(pos, item.size),
          RGBColour(color.r(), color.g(), color.b()),
          animation.alpha * color.a() / 255);
    }
    auto text = state == ButtonState::Hit || state == ButtonState::Push
                    ? item.highlighted
                    : item.normal;
    if (!text)
      continue;
    Point text_pos = pos;
    if (buttons && reallive) {
      text_pos = text->GetSize().CenteredIn(Rect(pos, item.size)).origin();
    } else if (buttons) {
      text_pos += s.config.text_pos;
      const Size storage = text->GetSize();
      const Size size(std::max(0, storage.width() - 2 - s.config.x_spacing),
                      std::max(0, storage.height() - 2));
      text_pos -= Point(s.config.text_x_align == 1   ? size.width() / 2
                        : s.config.text_x_align == 2 ? size.width()
                                                     : 0,
                        s.config.text_y_align == 1   ? size.height() / 2
                        : s.config.text_y_align == 2 ? size.height()
                                                     : 0);
    }
    const int dark =
        buttons || state == ButtonState::Hit || state == ButtonState::Push ? 0
                                                                           : 64;
    text->RenderToScreen(text->GetRect(), Rect(text_pos, text->GetSize()),
                         animation.alpha, 0, dark);
  }
}

bool SelState::Choose(int index) {
  if (!active_ || !active_->Choose(index))
    return false;
  last_message_ =
      index < 0 ? "（キャンセル）" : active_->request.options[index].text;
  return true;
}

SelState::InputResult SelState::HandleEvent(std::shared_ptr<Event> event) {
  InputResult feedback;
  auto s = active_;
  if (!event || !s || !s->Visible())
    return feedback;
  const auto decide = [&](int index) {
    if (!Choose(index))
      return;
    feedback.choice = index;
  };
  auto move_focus = [&](int direction) {
    if (!s || !s->CanInput() || s->items.empty())
      return;
    const int count = s->items.size();
    int pos = s->focused.value_or(direction > 0 ? -1 : 0);
    for (int i = 0; i < count; ++i) {
      pos = (pos + direction + count) % count;
      if (s->request.options[pos].type == 1) {
        if (s->focused != pos)
          feedback.focus_changed = true;
        s->focused = pos;
        return;
      }
    }
  };
  auto hit = [&](const Point& point) -> std::optional<int> {
    if (!s)
      return std::nullopt;
    for (std::size_t i = 0; i < s->items.size(); ++i)
      if (s->request.options[i].type == 1 &&
          s->items[i].hit_rect.Contains(Point(point - s->Origin())))
        return static_cast<int>(i);
    return std::nullopt;
  };

  const bool ready = s->CanInput();
  // Consume decision/navigation input throughout visible animations, while
  // only registering presses during Waiting after the opening hook.
  bool consume = false;
  Point& mouse = mouse_;
  bool& cancel_pressed = cancel_pressed_;
  if (const auto* motion = std::get_if<MouseMotion>(event.get())) {
    mouse = motion->pos;
    if (ready) {
      auto hitid = hit(mouse);
      if (hitid && s->focused != hitid)
        feedback.focus_changed = true;
      s->focused = hitid;
    }
  } else if (const auto* down = std::get_if<MouseDown>(event.get())) {
    consume =
        down->button == MouseButton::LEFT || down->button == MouseButton::RIGHT;
    if (ready && down->button == MouseButton::LEFT)
      s->pressed = hit(mouse);
    if (ready && down->button == MouseButton::RIGHT)
      cancel_pressed = true;
  } else if (const auto* up = std::get_if<MouseUp>(event.get())) {
    consume =
        up->button == MouseButton::LEFT || up->button == MouseButton::RIGHT;
    if (ready && up->button == MouseButton::LEFT && s->pressed &&
        s->pressed == hit(mouse))
      decide(*s->pressed);
    if (ready && up->button == MouseButton::RIGHT && cancel_pressed)
      decide(-1);
    s->pressed.reset();
    cancel_pressed = false;
  } else if (const auto* down = std::get_if<KeyDown>(event.get())) {
    const auto k = down->code;
    consume = IsDecideKey(k) || IsCancelKey(k) || k == KeyCode::UP ||
              k == KeyCode::DOWN || k == KeyCode::LEFT || k == KeyCode::RIGHT;
    if (consume && ready && held_keys_.insert(k).second) {
      if (k == KeyCode::UP || k == KeyCode::LEFT)
        move_focus(-1);
      if (k == KeyCode::DOWN || k == KeyCode::RIGHT)
        move_focus(1);
    }
  } else if (const auto* up = std::get_if<KeyUp>(event.get())) {
    const auto k = up->code;
    consume = IsDecideKey(k) || IsCancelKey(k) || k == KeyCode::UP ||
              k == KeyCode::DOWN || k == KeyCode::LEFT || k == KeyCode::RIGHT;
    if (held_keys_.erase(k) && ready) {
      if (IsDecideKey(k) && s->focused)
        decide(*s->focused);
      if (IsCancelKey(k))
        decide(-1);
    }
  }

  if (consume)
    *event = std::monostate();
  return feedback;
}
