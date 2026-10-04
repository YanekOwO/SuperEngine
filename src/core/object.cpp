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

#include "core/object.hpp"

#include "core/object_internal/animator.hpp"
#include "core/object_internal/objdrawer.hpp"
#include "core/object_internal/object_mutator.hpp"
#include "log/core.hpp"
#include "log/domain_logger.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

ParentObjState ParentObjState::BuildFrom(const GraphicsObject& parent) {
  ParentObjState ret;
  const ObjectParameter& param = parent.Param();
  Point position(param.x() + param.GetXAdjustmentSum(),
                 param.y() + param.GetYAdjustmentSum());
  if (param.GetButtonUsingOverides())
    position += param.GetButtonOffsetOverride();

  ret.render_state = RenderState::Build(param, position);
  if (param.has_own_clip_rect())
    ret.clip = param.own_clip_rect();
  ret.alpha = param.GetNormalizedAlpha();
  ret.bright = param.GetNormalizedBright();
  ret.dark = param.GetNormalizedDark();
  return ret;
}

// -----------------------------------------------------------------------
// GraphicsObject
// -----------------------------------------------------------------------
GraphicsObject::GraphicsObject() = default;
GraphicsObject::~GraphicsObject() = default;

static DomainLogger logger("GraphicsObject");

GraphicsObject GraphicsObject::Clone() const {
  GraphicsObject result;
  result.param_ = param_;
  result.file_path_ = file_path_;
  if (object_data_) {
    result.object_data_ = object_data_->Clone();
  }

  result.object_mutators_.reserve(object_mutators_.size());
  for (const auto& it : object_mutators_)
    result.object_mutators_.emplace_back(it.DeepCopy());

  result.child_.resize(child_.size());
  for (std::size_t i = 0; i < child_.size(); ++i) {
    if (child_[i])
      result.child_[i] = std::make_unique<GraphicsObject>(child_[i]->Clone());
  }

  return result;
}

GraphicsObject::GraphicsObject(GraphicsObject&& rhs)
    : param_(rhs.param_),
      object_data_(nullptr),
      file_path_(std::move(rhs.file_path_)),
      object_mutators_(),
      child_(std::move(rhs.child_)) {
  if (rhs.object_data_) {
    object_data_ = std::move(rhs.object_data_);
  }
  object_mutators_ = std::move(rhs.object_mutators_);

  rhs.param_ = ObjectParameter();
  rhs.file_path_.clear();
}

GraphicsObject& GraphicsObject::operator=(GraphicsObject&& rhs) {
  param_ = rhs.param_;
  file_path_ = std::move(rhs.file_path_);
  if (rhs.object_data_) {
    object_data_ = std::move(rhs.object_data_);
  } else {
    object_data_ = nullptr;
  }

  object_mutators_ = std::move(rhs.object_mutators_);
  child_ = std::move(rhs.child_);

  rhs.param_ = ObjectParameter();
  rhs.file_path_.clear();

  return *this;
}

int GraphicsObject::PixelWidth() const {
  // Calculate out the pixel width of the current object taking in the
  // width() scaling.
  if (HasDrawer())
    return object_data_->PixelWidth(*this);
  else
    return 0;
}

int GraphicsObject::PixelHeight() const {
  if (HasDrawer())
    return object_data_->PixelHeight(*this);
  else
    return 0;
}

GraphicsObjectData& GraphicsObject::GetDrawer() {
  if (object_data_) {
    return *object_data_;
  } else {
    throw std::runtime_error("null object data");
  }
}

const GraphicsObjectData& GraphicsObject::GetDrawer() const {
  if (object_data_) {
    return *object_data_;
  } else {
    throw std::runtime_error("null object data");
  }
}

const std::string& GraphicsObject::FilePath() const { return file_path_; }

void GraphicsObject::SetFilePath(std::string path) {
  file_path_ = std::move(path);
}

void GraphicsObject::ClearFilePath() { file_path_.clear(); }

void GraphicsObject::SetDrawer(std::unique_ptr<GraphicsObjectData> obj) {
  child_.clear();
  object_data_ = std::move(obj);
}

std::vector<std::unique_ptr<GraphicsObject>>& GraphicsObject::GetChildren() {
  return child_;
}

const std::vector<std::unique_ptr<GraphicsObject>>&
GraphicsObject::GetChildren() const {
  return child_;
}

GraphicsObject* GraphicsObject::GetChild(std::size_t idx) {
  if (idx >= child_.size())
    return nullptr;
  return child_[idx].get();
}

const GraphicsObject* GraphicsObject::GetChild(std::size_t idx) const {
  if (idx >= child_.size())
    return nullptr;
  return child_[idx].get();
}

GraphicsObject& GraphicsObject::TouchChild(std::size_t idx) {
  if (idx >= child_.size())
    throw std::out_of_range("GraphicsObject child index out of range");
  if (!child_[idx])
    child_[idx] = std::make_unique<GraphicsObject>();
  return *child_[idx];
}

void GraphicsObject::SetChild(std::size_t idx, GraphicsObject&& obj) {
  EnsureChildCapacity(idx + 1);
  child_[idx] = std::make_unique<GraphicsObject>(std::move(obj));
}

void GraphicsObject::ResetChildren(std::size_t count) {
  object_data_.reset();
  file_path_.clear();
  child_.clear();
  child_.resize(count);
}

void GraphicsObject::EnsureChildCapacity(std::size_t count) {
  if (child_.size() < count)
    child_.resize(count);
}

void GraphicsObject::AddObjectMutator(ObjectMutator mutator) {
  object_mutators_.emplace_back(std::move(mutator));
}

bool GraphicsObject::IsMutatorRunningMatching(int repno,
                                              const std::string& name) {
  return std::any_of(
      object_mutators_.cbegin(), object_mutators_.cend(),
      [&](const auto& it) { return it.OperationMatches(repno, name); });
}

void GraphicsObject::EndObjectMutatorMatching(int repno,
                                              const std::string& name,
                                              int speedup) {
  if (speedup == 0) {
    std::erase_if(object_mutators_, [&](auto& it) {
      if (!it.OperationMatches(repno, name))
        return false;
      it.SetToEnd(this->Param());
      return true;
    });
  } else if (speedup == 1) {
    // This is explicitly a noop.
  } else {
    logger(Severity::Warn) << "We only do immediate endings in "
                           << "EndObjectMutatorMatching(). Unsupported speedup "
                           << speedup;
  }
}

void GraphicsObject::Render(std::optional<ParentObjState> parent,
                            const ObjectMaskResolver* mask_resolver,
                            std::optional<ObjectMask> inherited_mask) {
  if (!Param().visible())
    return;

  std::optional<ObjectMask> mask = std::move(inherited_mask);
  if (mask_resolver && Param().mask_no >= 0) {
    mask = std::invoke(*mask_resolver, Param().mask_no);
  }

  if (object_data_)
    object_data_->Render(*this, parent, mask);

  if (!child_.empty()) {
    if (parent) {
      logger(Severity::Warn) << "Nested parents are not supported yet.";
    }

    const ParentObjState child_parent = ParentObjState::BuildFrom(*this);
    for (auto& it : child_) {
      if (!it)
        continue;
      it->Render(child_parent, mask_resolver, mask);
    }
  }
}

void GraphicsObject::FreeObjectData() {
  object_data_.reset();
  file_path_.clear();
  object_mutators_.clear();
  child_.clear();
}

void GraphicsObject::InitializeParams() {
  param_ = ObjectParameter();
  object_mutators_.clear();
}

void GraphicsObject::FreeDataAndInitializeParams() {
  object_data_.reset();
  file_path_.clear();
  param_ = ObjectParameter();
  object_mutators_.clear();
  child_.clear();
}

void GraphicsObject::Execute() {
  if (object_data_) {
    object_data_->Execute();

    auto should_delete = [](GraphicsObjectData* it) -> bool {
      auto animator = it->GetAnimator();
      if (animator == nullptr)
        return false;
      return it->GetAnimator()->IsFinished() &&
             it->GetAnimator()->GetAfterAction() == AFTER_CLEAR;
    };
    if (should_delete(object_data_.get())) {
      object_data_ = nullptr;
      file_path_.clear();
    }
  }

  for (auto& it : child_) {
    if (!it)
      continue;
    it->Execute();
  }
}

void GraphicsObject::ExecuteMutators() {
  std::erase_if(object_mutators_,
                [&](auto& it) { return it.Update(this->Param()); });
}

void GraphicsObject::EndAllMutators() {
  std::for_each(object_mutators_.begin(), object_mutators_.end(),
                [this](auto& it) { it.SetToEnd(Param()); });
  object_mutators_.clear();
}

std::size_t GraphicsObject::CountMutators() const {
  return object_mutators_.size();
}
