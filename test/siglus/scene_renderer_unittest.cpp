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

#include <gtest/gtest.h>

#include "core/mask.hpp"
#include "core/object.hpp"
#include "libsiglus/siglus_scene_renderer.hpp"
#include "mock_graphics_object_data.hpp"
#include "systems/sdl/sdl_surface.hpp"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace libsiglus {

class SiglusSceneRendererTest : public ::testing::Test {
 protected:
  struct Draw {
    const GraphicsObject* object;
    std::optional<ParentObjState> parent;
    std::optional<ObjectMask> mask;
  };

  GraphicsObject root;
  std::vector<Draw> draws;

  void SetUp() override { root.Param().SetVisible(1); }

  void Record(GraphicsObject& object) {
    auto drawer = std::make_unique<MockGraphicsObjectData>();
    drawer->SetMaskRenderCallback([this](const GraphicsObject& object,
                                         std::optional<ParentObjState> parent,
                                         std::optional<ObjectMask> mask) {
      draws.push_back({&object, std::move(parent), std::move(mask)});
    });
    object.SetDrawer(std::move(drawer));
  }

  GraphicsObject& Child(GraphicsObject& parent,
                        std::size_t index,
                        int order = 0,
                        int layer = 0) {
    parent.EnsureChildCapacity(index + 1);
    auto& child = parent.TouchChild(index);
    child.Param().SetVisible(1);
    child.Param().SetZOrder(order);
    child.Param().SetZLayer(layer);
    Record(child);
    return child;
  }

  void Render(const MaskList* masks = nullptr,
              std::optional<ParentObjState> parent = std::nullopt) {
    draws.clear();
    RenderSiglusObjectTree(root, masks, std::move(parent));
  }

  std::vector<const GraphicsObject*> RenderedObjects() const {
    std::vector<const GraphicsObject*> result;
    for (const auto& draw : draws)
      result.push_back(draw.object);
    return result;
  }
};

TEST_F(SiglusSceneRendererTest, AchievementBackgroundRendersBeforeLabels) {
  // The description precedes its background in storage; layers must place the
  // background first so both achievement labels remain visible.
  auto& description = Child(root, 0, 0, 200002);
  auto& background = Child(root, 1, 0, 200000);
  auto& heading = Child(root, 2, 0, 200002);

  Render();

  EXPECT_EQ(RenderedObjects(), (std::vector<const GraphicsObject*>{
                                   &background, &description, &heading}));
}

TEST_F(SiglusSceneRendererTest, OrderTakesPrecedenceOverLayer) {
  // Siblings have opposing order and layer values; the lower order must render
  // first even though its layer is higher.
  auto& higher_order = Child(root, 0, 2, -100);
  auto& lower_order = Child(root, 1, 1, 100);

  Render();

  EXPECT_EQ(RenderedObjects(),
            (std::vector<const GraphicsObject*>{&lower_order, &higher_order}));
}

TEST_F(SiglusSceneRendererTest, EqualSortersPreserveChildIndexOrder) {
  // Equal sibling sorters must retain the original index order after rendering.
  std::vector<const GraphicsObject*> expected;
  for (std::size_t i = 0; i < 24; ++i)
    expected.push_back(&Child(root, i, 3, 5));

  Render();

  EXPECT_EQ(RenderedObjects(), expected);
}

TEST_F(SiglusSceneRendererTest, DepthDoesNotBreakSorterTies) {
  // Equal order/layer values with reversed depths still render by child index.
  auto& first = Child(root, 0);
  auto& second = Child(root, 1);
  first.Param().SetZDepth(100);
  second.Param().SetZDepth(-100);

  Render();

  EXPECT_EQ(RenderedObjects(),
            (std::vector<const GraphicsObject*>{&first, &second}));
}

TEST_F(SiglusSceneRendererTest, SortsSiblingsWithinEachSubtree) {
  // Both sibling levels start reversed; each subtree must finish rendering in
  // its own layer order before the next root child renders.
  auto& later = Child(root, 0, 0, 2);
  auto& earlier = Child(root, 1, 0, 1);
  auto& later_grandchild = Child(earlier, 0, 0, 100);
  auto& earlier_grandchild = Child(earlier, 1, 0, -100);

  Render();

  EXPECT_EQ(RenderedObjects(),
            (std::vector<const GraphicsObject*>{&earlier, &earlier_grandchild,
                                                &later_grandchild, &later}));
}

TEST_F(SiglusSceneRendererTest, ParentDrawerRendersBeforeChildren) {
  // A drawable parent has a higher layer than its child; the parent must still
  // render first because sorting applies only among siblings.
  root.Param().SetZLayer(100);
  Record(root);
  auto& child = Child(root, 0, 0, -100);

  Render();

  EXPECT_EQ(RenderedObjects(),
            (std::vector<const GraphicsObject*>{&root, &child}));
}

TEST_F(SiglusSceneRendererTest, RenderingPreservesChildSlots) {
  // Reversed layers and an empty slot must keep their original object handles
  // and indices after rendering sorts its temporary traversal list.
  Child(root, 0, 0, 2);
  Child(root, 2, 0, 1);
  const auto original = root.GetChildren();

  Render();

  EXPECT_EQ(root.GetChildren(), original);
}

TEST_F(SiglusSceneRendererTest, ReevaluatesLayersOnEveryRender) {
  // A layer changes between frames; the next traversal must reflect the new
  // order without requiring a child-list rebuild.
  auto& first = Child(root, 0, 0, 2);
  auto& second = Child(root, 1, 0, 1);
  Render();
  EXPECT_EQ(RenderedObjects(),
            (std::vector<const GraphicsObject*>{&second, &first}));

  first.Param().SetZLayer(0);
  Render();

  EXPECT_EQ(RenderedObjects(),
            (std::vector<const GraphicsObject*>{&first, &second}));
}

TEST_F(SiglusSceneRendererTest, SkipsNullChildrenWithoutAllocatingThem) {
  // Sparse child storage has only its middle slot allocated; traversal must
  // render that child while leaving both empty slots untouched.
  root.EnsureChildCapacity(3);
  auto& child = Child(root, 1);

  Render();

  EXPECT_EQ(RenderedObjects(), (std::vector<const GraphicsObject*>{&child}));
  EXPECT_EQ(root.GetChild(0), nullptr);
  EXPECT_EQ(root.GetChild(2), nullptr);
}

TEST_F(SiglusSceneRendererTest, HiddenChildrenSuppressTheirSubtrees) {
  // A hidden child's descendant is visible; neither may render through the
  // hidden parent, while the visible sibling still renders.
  auto& hidden = Child(root, 0);
  Child(hidden, 0);
  hidden.Param().SetVisible(0);
  auto& visible = Child(root, 1);

  Render();

  EXPECT_EQ(RenderedObjects(), (std::vector<const GraphicsObject*>{&visible}));
}

TEST_F(SiglusSceneRendererTest, ButtonVisibilitySuppressesChildSubtrees) {
  // A child hidden by button state has a visible descendant; traversal must
  // suppress that entire subtree and retain the visible sibling.
  auto& hidden = Child(root, 0);
  Child(hidden, 0);
  hidden.Param().button.visible = false;
  auto& visible = Child(root, 1);

  Render();

  EXPECT_EQ(RenderedObjects(), (std::vector<const GraphicsObject*>{&visible}));
}

TEST_F(SiglusSceneRendererTest, SortedChildrenRetainInheritedTransform) {
  // A translated root has an external parent; sorted children must receive the
  // same combined parent translation regardless of their traversal order.
  root.Param().SetX(30);
  root.Param().SetY(40);
  Child(root, 0, 0, 2);
  Child(root, 1, 0, 1);
  ParentObjState parent;
  parent.render_state.pos_x = 100;
  parent.render_state.pos_y = 200;

  Render(nullptr, parent);

  ASSERT_EQ(draws.size(), 2);
  for (const auto& draw : draws) {
    ASSERT_TRUE(draw.parent);
    EXPECT_EQ(draw.parent->render_state.pos_x, 130);
    EXPECT_EQ(draw.parent->render_state.pos_y, 240);
  }
}

TEST_F(SiglusSceneRendererTest, SortedChildrenRetainInheritedMask) {
  // A masked root has children in reversed layer order; every rendered child
  // must retain the parent's mask surface and origin.
  MaskList masks(1);
  auto surface = std::make_shared<SDLSurface>(Size(1, 1));
  masks.At(0).Create("mask", surface);
  masks.At(0).Param().x = 7;
  masks.At(0).Param().y = 9;
  root.Param().mask_no = 0;
  Child(root, 0, 0, 2);
  Child(root, 1, 0, 1);

  Render(&masks);

  ASSERT_EQ(draws.size(), 2);
  for (const auto& draw : draws) {
    ASSERT_TRUE(draw.mask);
    EXPECT_EQ(draw.mask->surface, surface);
    EXPECT_EQ(draw.mask->origin, Point(7, 9));
  }
}

}  // namespace libsiglus
