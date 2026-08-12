// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/editor_interaction.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <numbers>
#include <span>
#include <vector>

namespace {

using signet::core::Point;
using signet::core::Transform;
using namespace signet::ui::interaction;

bool close(const double left, const double right, const double epsilon = 1.0e-9) {
  return std::abs(left - right) <= epsilon * std::max({1.0, std::abs(left), std::abs(right)});
}

void assertPoint(const Point actual, const Point expected) {
  assert(close(actual.x, expected.x));
  assert(close(actual.y, expected.y));
}

void assertTransform(const Transform& actual, const Transform& expected) {
  assertPoint(actual.translation, expected.translation);
  assert(close(actual.rotation_degrees, expected.rotation_degrees));
  assertPoint(actual.scale, expected.scale);
}

void testCoordinateRoundTrip() {
  const Viewport viewport{800.0, 600.0, {13.0, -17.0}, 2.5};
  const Point document{12.25, -8.5};
  const auto view = documentToView(viewport, document);
  assert(view.has_value());
  assertPoint(*view, {viewport.width / 2.0 + viewport.pan_offset.x + document.x * viewport.zoom,
                       viewport.height / 2.0 + viewport.pan_offset.y - document.y * viewport.zoom});
  const auto round_trip = viewToDocument(viewport, *view);
  assert(round_trip.has_value());
  assertPoint(*round_trip, document);
  assert(!documentToView(Viewport{800.0, 600.0, {}, 0.0}, document).has_value());
}

void testPlacements() {
  const auto circle = placeCircle({1.0, 2.0}, {4.0, 6.0});
  assert(circle.has_value());
  assert(std::get<signet::core::Circle>(circle->primitive).radius == 5.0);
  assertTransform(circle->transform, Transform{{1.0, 2.0}});
  assert(!placeCircle({0.0, 0.0}, {1.0e-12, 0.0}).has_value());
  assert(!placeCircle({std::numeric_limits<double>::infinity(), 0.0}, {1.0, 0.0}).has_value());

  const auto rectangle = placeAxisAlignedRectangle({4.0, 3.0}, {-2.0, -1.0});
  assert(rectangle.has_value());
  const auto rectangle_value = std::get<signet::core::Rectangle>(rectangle->primitive);
  assert(close(rectangle_value.width, 6.0));
  assert(close(rectangle_value.height, 4.0));
  assertTransform(rectangle->transform, Transform{{1.0, 1.0}});
  assert(!placeAxisAlignedRectangle({0.0, 0.0}, {1.0e-12, 2.0}).has_value());

  const auto ccw_minor = placeThreePointArc({1.0, 0.0}, {0.0, 1.0}, {-1.0, 0.0});
  const auto cw_minor = placeThreePointArc({1.0, 0.0}, {0.0, -1.0}, {-1.0, 0.0});
  assert(ccw_minor.has_value() && cw_minor.has_value());
  assert(std::get<signet::core::Arc>(ccw_minor->primitive).sweep_degrees > 0.0);
  assert(std::get<signet::core::Arc>(cw_minor->primitive).sweep_degrees < 0.0);
  assert(close(std::abs(std::get<signet::core::Arc>(ccw_minor->primitive).sweep_degrees), 180.0));

  const auto cw_major = placeThreePointArc({1.0, 0.0}, {0.0, -1.0}, {0.0, 1.0});
  const auto ccw_major = placeThreePointArc({1.0, 0.0}, {-1.0, 0.0}, {0.0, -1.0});
  assert(cw_major.has_value() && ccw_major.has_value());
  assert(std::get<signet::core::Arc>(cw_major->primitive).sweep_degrees < -180.0);
  assert(std::get<signet::core::Arc>(ccw_major->primitive).sweep_degrees > 180.0);
  assert(!placeThreePointArc({0.0, 0.0}, {1.0, 1.0}, {2.0, 2.0}).has_value());
  assert(!placeThreePointArc({0.0, 0.0}, {0.0, 0.0}, {1.0, 0.0}).has_value());
  assert(!placeThreePointArc({1.0e-150, 0.0}, {0.0, 1.0e-150}, {-1.0e-150, 0.0}).has_value());
  const auto huge_arc = placeThreePointArc({1.0e150, 0.0}, {0.0, 1.0e150}, {-1.0e150, 0.0});
  assert(huge_arc.has_value());
  assert(close(std::get<signet::core::Arc>(huge_arc->primitive).radius, 1.0e150));

  const auto golden_a = placeGoldenByCenterAndShortSideVector({3.0, 4.0}, {0.0, 2.0});
  assert(golden_a.has_value());
  assert(close(std::get<signet::core::GoldenRectangle>(golden_a->primitive).short_side, 2.0));
  assert(close(golden_a->transform.rotation_degrees, 0.0));
  assertPoint(golden_a->transform.translation, {3.0, 4.0});
  const auto golden_b = placeGoldenByShortEdge({0.0, -1.0}, {0.0, 1.0},
                                                GoldenEdgeSide::left_of_directed_edge);
  assert(golden_b.has_value());
  assertPoint(golden_b->transform.translation,
              {-std::numbers::phi_v<double>, 0.0});
  assert(!placeGoldenByShortEdge({0.0, 0.0}, {1.0e-12, 0.0},
                                 GoldenEdgeSide::right_of_directed_edge).has_value());
}

std::vector<SelectionItem> selectionItems() {
  return {
      {20, signet::core::Rectangle{4.0, 2.0}, Transform{{10.0, 0.0}, 15.0}},
      {10, signet::core::Circle{2.0}, Transform{{0.0, 10.0}}},
  };
}

void testSelectionTransforms() {
  auto items = selectionItems();
  const auto original = items;
  const auto rotated = rotateSelection(items, {0.0, 0.0}, 90.0);
  assert(rotated.has_value() && rotated->size() == 2);
  assert((*rotated)[0].node_id == 10 && (*rotated)[1].node_id == 20);
  assertPoint((*rotated)[0].transform.translation, {-10.0, 0.0});
  assert(close((*rotated)[0].transform.rotation_degrees, 90.0));
  assertPoint((*rotated)[1].transform.translation, {0.0, 10.0});
  assert(close((*rotated)[1].transform.rotation_degrees, 105.0));
  assert(items[0].node_id == original[0].node_id);
  assertPoint(items[0].transform.translation, original[0].transform.translation);

  const auto resized = resizeSelectionUniform(items, {0.0, 0.0}, 2.0);
  assert(resized.has_value());
  assert((*resized)[0].node_id == 10);
  assertPoint((*resized)[0].transform.translation, {0.0, 20.0});
  assertPoint((*resized)[1].transform.translation, {20.0, 0.0});
  assertPoint((*resized)[1].transform.scale, {2.0, 2.0});

  auto duplicate = items;
  duplicate.push_back(duplicate.front());
  assert(!rotateSelection(duplicate, {}, 10.0).has_value());
  std::ranges::reverse(items);
  const auto reverse_order = rotateSelection(items, {}, 90.0);
  assert(reverse_order.has_value() && *reverse_order == *rotated);
}

void testSymmetryAxes() {
  const std::vector<SelectionItem> items{
      {20, signet::core::Rectangle{4.0, 2.0}, Transform{{10.0, 0.0}}},
      {10, signet::core::Circle{2.0}, Transform{{0.0, 10.0}}},
  };
  const auto vertical = symmetryAxisForSelection(items, ReflectionAxis::vertical);
  assert(vertical.has_value());
  assertPoint(vertical->origin, {5.0, 5.5});
  assertPoint(vertical->direction, {0.0, 1.0});
  const auto horizontal = symmetryAxisForSelection(items, ReflectionAxis::horizontal);
  assert(horizontal.has_value());
  assertPoint(horizontal->origin, {5.0, 5.5});
  assertPoint(horizontal->direction, {1.0, 0.0});
  assert(!symmetryAxisForSelection(std::span<const SelectionItem>{}, ReflectionAxis::vertical)
              .has_value());
}

void testRectangleResize() {
  const signet::core::Rectangle rectangle{4.0, 2.0};
  const Transform identity{};
  const auto resized = resizeRectangleNonUniform(
      rectangle, identity, {-2.0, -1.0}, {2.0, 1.0}, {6.0, 5.0});
  assert(resized.has_value());
  assertTransform(*resized, Transform{{2.0, 2.0}, 0.0, {2.0, 3.0}});
  const auto fixed_world = resizeRectangleNonUniformFromFixedWorld(
      rectangle, identity, {-2.0, -1.0}, {6.0, 5.0});
  assert(fixed_world.has_value());
  assertTransform(*fixed_world, *resized);

  const Transform rotated{{10.0, 20.0}, 90.0, {1.0, 1.0}};
  const auto rotated_resize = resizeRectangleNonUniform(
      rectangle, rotated, {-2.0, -1.0}, {2.0, 1.0}, {5.0, 26.0});
  assert(rotated_resize.has_value());
  assert(close(rotated_resize->scale.x, 2.0));
  assert(close(rotated_resize->scale.y, 3.0));
  assert(close(rotated_resize->rotation_degrees, 90.0));
  const auto axis_aligned_resize = resizeRectangleNonUniform(
      rectangle, identity, {-2.0, -1.0}, {2.0, 1.0}, {6.0, 4.0});
  assert(axis_aligned_resize.has_value());
  assertPoint(axis_aligned_resize->scale, {2.0, 2.5});
  assert(!resizeRectangleNonUniform(rectangle, identity, {0.0, -1.0}, {2.0, 1.0},
                                    {6.0, 5.0})
              .has_value());
}

void testBoundsAndHandles() {
  const std::vector<Point> points{{5.0, 9.0}, {-2.0, 4.0}, {7.0, 12.0}};
  const auto bounds = boundsFromPoints(points);
  assert(bounds.has_value());
  assertPoint(bounds->min, {-2.0, 4.0});
  assertPoint(bounds->max, {7.0, 12.0});
  assert(!boundsFromPoints(std::span<const Point>{}).has_value());
  const auto layout = layoutSelectionHandlesInView(*bounds, 14.0, 9.0);
  assert(layout.has_value());
  assertPoint(layout->resize_handles[static_cast<std::size_t>(ResizeHandle::top_left)],
              {-2.0, 4.0});
  assertPoint(layout->resize_handles[static_cast<std::size_t>(ResizeHandle::top_center)],
              {2.5, 4.0});
  assertPoint(layout->resize_handles[static_cast<std::size_t>(ResizeHandle::bottom_right)],
              {7.0, 12.0});
  assertPoint(layout->resize_handles[static_cast<std::size_t>(ResizeHandle::middle_left)],
              {-2.0, 8.0});
  assertPoint(layout->rotate_handle, {2.5, -5.0});
  assert(close(layout->handle_size_view, 14.0));
  assert(close(layout->rotate_offset_view, 9.0));
  assert(!layoutSelectionHandlesInView(*bounds, 0.0, 9.0).has_value());
}

}  // namespace

int main() {
  testCoordinateRoundTrip();
  testPlacements();
  testSelectionTransforms();
  testSymmetryAxes();
  testRectangleResize();
  testBoundsAndHandles();
}
