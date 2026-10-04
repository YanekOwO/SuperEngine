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

#include "core/object_internal/object_parameter.hpp"
#include "core/render_geometry.hpp"

#include <concepts>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class GraphicsObjectData;
class ObjectMutator;
class GraphicsObject;

struct ParentObjState {
  RenderState render_state = RenderState::Id();
  std::optional<Rect> clip = std::nullopt;
  float alpha = 1.f;
  float bright = 0.f, dark = 0.f;

  inline float EffectiveAlpha(float a) const { return a * alpha; }
  inline float EffectiveDark(float d) const {
    return 1.f - (1.f - d) * (1.f - dark);
  }
  inline float EffectiveBright(float b) const {
    return 1.f - (1.f - b) * (1.f - bright);
  }

  static ParentObjState BuildFrom(const GraphicsObject& parent);
};

// Describes an graphical object on the screen.
class GraphicsObject {
 public:
  GraphicsObject();
  ~GraphicsObject();

  // Use this->Clone() instead.
  GraphicsObject(const GraphicsObject& obj) = delete;
  GraphicsObject& operator=(const GraphicsObject& obj) = delete;

  GraphicsObject(GraphicsObject&& rhs);
  GraphicsObject& operator=(GraphicsObject&& rhs);

  GraphicsObject Clone() const;

  inline ObjectParameter& Param() { return param_; }
  inline ObjectParameter const& Param() const { return param_; }

  int PixelWidth() const;
  int PixelHeight() const;
  const std::string& FilePath() const;
  void SetFilePath(std::string path);
  void ClearFilePath();

  // Drawer (aka. graphics object data)
  inline bool HasDrawer() const { return object_data_.operator bool(); }
  GraphicsObjectData& GetDrawer();
  const GraphicsObjectData& GetDrawer() const;
  template <std::derived_from<GraphicsObjectData> T = GraphicsObjectData>
  inline const T* GetDrawer() const {
    if constexpr (std::same_as<T, GraphicsObjectData>)
      return object_data_.get();
    else
      return dynamic_cast<const T*>(object_data_.get());
  }
  template <std::derived_from<GraphicsObjectData> T = GraphicsObjectData>
  inline T* GetDrawer() {
    if constexpr (std::same_as<T, GraphicsObjectData>)
      return object_data_.get();
    else
      return dynamic_cast<T*>(object_data_.get());
  }
  void SetDrawer(std::unique_ptr<GraphicsObjectData> obj);

  // Children objects
  inline bool HasChildren() const { return !child_.empty(); }
  std::vector<std::unique_ptr<GraphicsObject>>& GetChildren();
  const std::vector<std::unique_ptr<GraphicsObject>>& GetChildren() const;
  GraphicsObject* GetChild(std::size_t idx);
  const GraphicsObject* GetChild(std::size_t idx) const;
  GraphicsObject& TouchChild(std::size_t idx);
  void SetChild(std::size_t idx, GraphicsObject&& obj);
  void ResetChildren(std::size_t count);
  void EnsureChildCapacity(std::size_t count);

  // Frees the object data. Corresponds to objFree, but is also invoked by
  // other commands.
  void FreeObjectData();

  // Resets/reinitializes all the object parameters without deleting the
  // loaded graphics object data.
  void InitializeParams();

  // Both frees the object data and initializes parameters.
  void FreeDataAndInitializeParams();

  // Called each pass through the gameloop to see if this object needs
  // to force a redraw, or something.
  void Execute();
  void ExecuteMutators();

  // Adds a mutator to the list of active mutators. GraphicsSystem takes
  // ownership of the passed in object. Callers are responsible for deciding
  // whether a matching operation may be added.
  void AddObjectMutator(ObjectMutator mutator);

  // Returns true if a mutator matching the following parameters is currently
  // running.
  bool IsMutatorRunningMatching(int repno, const std::string& name);

  // Ends all mutators that match the given parameters.
  void EndObjectMutatorMatching(int repno,
                                const std::string& name,
                                int speedup);

  void EndAllMutators();
  std::size_t CountMutators() const;

 private:
  // Class to manage the actual implementation data
  ObjectParameter param_;

  // The actual data used to render the object
  std::unique_ptr<GraphicsObjectData> object_data_;

  std::string file_path_;

  // Tasks that run every tick. Used to mutate object parameters over time
  // (and how we check from a blocking LongOperation if the mutation is
  // ongoing).
  //
  // I think R23 mentioned that these were called "Parameter Events" in the
  // RLMAX SDK.
  std::vector<ObjectMutator> object_mutators_;

  std::vector<std::unique_ptr<GraphicsObject>> child_;
};
