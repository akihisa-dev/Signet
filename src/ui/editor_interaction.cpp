// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/editor_interaction.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <ranges>
#include <type_traits>

namespace signet::ui::interaction {

namespace {

constexpr double kMinimumGeometrySize = 1.0e-9;
constexpr double kRelativeTolerance = 1.0e-10;
constexpr double kCollinearTolerance = 1.0e-12;
constexpr double kFullTurnDegrees = 360.0;

bool finite(const double value) noexcept { return std::isfinite(value); }

bool finite(const core::Point point) noexcept {
  return finite(point.x) && finite(point.y);
}

bool finite(const core::Transform& transform) noexcept {
  return finite(transform.translation) && finite(transform.rotation_degrees) &&
         finite(transform.scale) && transform.scale.x != 0.0 && transform.scale.y != 0.0;
}

bool sufficientlyLarge(const double value) noexcept {
  return finite(value) && value > kMinimumGeometrySize;
}

bool sufficientlyLarge(const core::Point value) noexcept {
  return finite(value) && std::hypot(value.x, value.y) > kMinimumGeometrySize;
}

bool closeEnough(const double left, const double right) noexcept {
  return std::abs(left - right) <=
         kRelativeTolerance * std::max({1.0, std::abs(left), std::abs(right)});
}

bool validViewport(const Viewport& viewport) noexcept {
  return finite(viewport.width) && finite(viewport.height) && viewport.width >= 0.0 &&
         viewport.height >= 0.0 && finite(viewport.pan_offset) && finite(viewport.zoom) &&
         viewport.zoom > 0.0;
}

core::Point rotateVector(const core::Point value, const double degrees) noexcept {
  const double radians = degrees * std::numbers::pi_v<double> / 180.0;
  const double cosine = std::cos(radians);
  const double sine = std::sin(radians);
  return {value.x * cosine - value.y * sine, value.x * sine + value.y * cosine};
}

std::optional<core::Point> applyTransform(
    const core::Point point,
    const core::Transform& transform) noexcept {
  if (!finite(point) || !finite(transform)) {
    return std::nullopt;
  }
  const core::Point scaled{point.x * transform.scale.x, point.y * transform.scale.y};
  const core::Point rotated = rotateVector(scaled, transform.rotation_degrees);
  const core::Point result{transform.translation.x + rotated.x,
                           transform.translation.y + rotated.y};
  return finite(result) ? std::optional<core::Point>(result) : std::nullopt;
}

std::optional<core::Point> inverseTransform(
    const core::Point point,
    const core::Transform& transform) noexcept {
  if (!finite(point) || !finite(transform)) {
    return std::nullopt;
  }
  const core::Point translated{point.x - transform.translation.x,
                               point.y - transform.translation.y};
  const core::Point unrotated = rotateVector(translated, -transform.rotation_degrees);
  const core::Point result{unrotated.x / transform.scale.x,
                           unrotated.y / transform.scale.y};
  return finite(result) ? std::optional<core::Point>(result) : std::nullopt;
}

double positiveAngleDelta(const double from, const double to) noexcept {
  double delta = std::fmod(to - from, 2.0 * std::numbers::pi_v<double>);
  if (delta < 0.0) {
    delta += 2.0 * std::numbers::pi_v<double>;
  }
  return delta;
}

bool validRectangleCorner(
    const core::Rectangle& rectangle,
    const core::Point point) noexcept {
  if (!finite(rectangle.width) || !finite(rectangle.height) ||
      !sufficientlyLarge(rectangle.width) || !sufficientlyLarge(rectangle.height) ||
      !finite(point)) {
    return false;
  }
  const double scale = std::max({1.0, rectangle.width, rectangle.height});
  const double tolerance = kRelativeTolerance * scale;
  const double half_width = rectangle.width / 2.0;
  const double half_height = rectangle.height / 2.0;
  return std::abs(std::abs(point.x) - half_width) <= tolerance &&
         std::abs(std::abs(point.y) - half_height) <= tolerance;
}

bool oppositeCorners(const core::Point first, const core::Point second) noexcept {
  return !closeEnough(first.x, second.x) && !closeEnough(first.y, second.y);
}

bool uniformScaleForPrimitive(
    const core::Primitive& primitive,
    const core::Transform& transform) noexcept {
  if (!finite(transform)) {
    return false;
  }
  return std::visit(
      [&transform](const auto& value) {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, core::Circle>) {
          return finite(value.radius) && value.radius > 0.0 &&
                 closeEnough(transform.scale.x, transform.scale.y);
        } else if constexpr (std::is_same_v<Value, core::Arc>) {
          return finite(value.radius) && value.radius > 0.0 &&
                 finite(value.start_degrees) && finite(value.sweep_degrees) &&
                 value.sweep_degrees != 0.0 && std::abs(value.sweep_degrees) <= 360.0 &&
                 closeEnough(transform.scale.x, transform.scale.y);
        }
        else if constexpr (std::is_same_v<Value, core::Rectangle>) {
          return finite(value.width) && finite(value.height) && value.width > 0.0 &&
                 value.height > 0.0;
        } else {
          return finite(value.short_side) && value.short_side > 0.0 &&
                 closeEnough(transform.scale.x, transform.scale.y);
        }
      },
      primitive);
}

std::optional<std::vector<SelectionItem>> orderedSelection(
    std::span<const SelectionItem> items) {
  if (items.empty()) {
    return std::nullopt;
  }
  std::vector<SelectionItem> ordered(items.begin(), items.end());
  std::ranges::sort(ordered, {}, &SelectionItem::node_id);
  for (std::size_t index = 0; index < ordered.size(); ++index) {
    if (index != 0 && ordered[index - 1].node_id == ordered[index].node_id) {
      return std::nullopt;
    }
    if (!uniformScaleForPrimitive(ordered[index].primitive, ordered[index].transform)) {
      return std::nullopt;
    }
  }
  return ordered;
}

std::optional<core::Transform> rectangleResizeFromLocalFixed(
    const core::Rectangle& rectangle,
    const core::Transform& original,
    const core::Point fixed_opposite_local,
    const core::Point moving_local,
    const core::Point moving_corner_world,
    const std::optional<core::Point> explicit_fixed_world) noexcept {
  if (!validRectangleCorner(rectangle, fixed_opposite_local) ||
      !validRectangleCorner(rectangle, moving_local) ||
      !oppositeCorners(fixed_opposite_local, moving_local) || !finite(original) ||
      !finite(moving_corner_world)) {
    return std::nullopt;
  }
  const auto fixed_world = explicit_fixed_world.has_value()
                               ? explicit_fixed_world
                               : applyTransform(fixed_opposite_local, original);
  if (!fixed_world.has_value() || !finite(*fixed_world)) {
    return std::nullopt;
  }
  const core::Point world_delta{moving_corner_world.x - fixed_world->x,
                                moving_corner_world.y - fixed_world->y};
  const core::Point local_delta = rotateVector(world_delta, -original.rotation_degrees);
  const core::Point source_delta{moving_local.x - fixed_opposite_local.x,
                                 moving_local.y - fixed_opposite_local.y};
  if (!sufficientlyLarge(source_delta) || !finite(local_delta)) {
    return std::nullopt;
  }
  const double scale_x = local_delta.x / source_delta.x;
  const double scale_y = local_delta.y / source_delta.y;
  if (!sufficientlyLarge(scale_x) || !sufficientlyLarge(scale_y)) {
    return std::nullopt;
  }
  const core::Point fixed_scaled{fixed_opposite_local.x * scale_x,
                                 fixed_opposite_local.y * scale_y};
  const core::Point fixed_rotated = rotateVector(fixed_scaled, original.rotation_degrees);
  const core::Transform result{
      {fixed_world->x - fixed_rotated.x, fixed_world->y - fixed_rotated.y},
      original.rotation_degrees,
      {scale_x, scale_y}};
  return finite(result) ? std::optional<core::Transform>(result) : std::nullopt;
}

}  // namespace

std::optional<core::Point> documentToView(
    const Viewport& viewport,
    const core::Point document_point) noexcept {
  if (!validViewport(viewport) || !finite(document_point)) {
    return std::nullopt;
  }
  const core::Point result{
      viewport.width / 2.0 + viewport.pan_offset.x + document_point.x * viewport.zoom,
      viewport.height / 2.0 + viewport.pan_offset.y - document_point.y * viewport.zoom};
  return finite(result) ? std::optional<core::Point>(result) : std::nullopt;
}

std::optional<core::Point> viewToDocument(
    const Viewport& viewport,
    const core::Point view_point) noexcept {
  if (!validViewport(viewport) || !finite(view_point)) {
    return std::nullopt;
  }
  const core::Point result{
      (view_point.x - viewport.width / 2.0 - viewport.pan_offset.x) / viewport.zoom,
      (viewport.height / 2.0 + viewport.pan_offset.y - view_point.y) / viewport.zoom};
  return finite(result) ? std::optional<core::Point>(result) : std::nullopt;
}

std::optional<PrimitivePlacement> placeCircle(
    const core::Point center,
    const core::Point drag_point) noexcept {
  if (!finite(center) || !finite(drag_point)) {
    return std::nullopt;
  }
  const double radius = std::hypot(drag_point.x - center.x, drag_point.y - center.y);
  if (!sufficientlyLarge(radius)) {
    return std::nullopt;
  }
  return PrimitivePlacement{core::Circle{radius}, core::Transform{center}};
}

std::optional<PrimitivePlacement> placeAxisAlignedRectangle(
    const core::Point first_corner,
    const core::Point opposite_corner) noexcept {
  if (!finite(first_corner) || !finite(opposite_corner)) {
    return std::nullopt;
  }
  const double width = std::abs(opposite_corner.x - first_corner.x);
  const double height = std::abs(opposite_corner.y - first_corner.y);
  if (!sufficientlyLarge(width) || !sufficientlyLarge(height)) {
    return std::nullopt;
  }
  const core::Point center{first_corner.x / 2.0 + opposite_corner.x / 2.0,
                           first_corner.y / 2.0 + opposite_corner.y / 2.0};
  if (!finite(center)) {
    return std::nullopt;
  }
  return PrimitivePlacement{core::Rectangle{width, height}, core::Transform{center}};
}

std::optional<PrimitivePlacement> placeThreePointArc(
    const core::Point start,
    const core::Point interior,
    const core::Point end) noexcept {
  if (!finite(start) || !finite(interior) || !finite(end)) {
    return std::nullopt;
  }
  const double coordinate_scale = std::max({std::abs(start.x), std::abs(start.y),
                                             std::abs(interior.x), std::abs(interior.y),
                                             std::abs(end.x), std::abs(end.y)});
  if (!sufficientlyLarge(coordinate_scale) || !finite(coordinate_scale)) {
    return std::nullopt;
  }
  const auto normalized = [coordinate_scale](const core::Point point) {
    return core::Point{point.x / coordinate_scale, point.y / coordinate_scale};
  };
  const core::Point source = normalized(start);
  const core::Point middle = normalized(interior);
  const core::Point target = normalized(end);
  if (!sufficientlyLarge(core::Point{middle.x - source.x, middle.y - source.y}) ||
      !sufficientlyLarge(core::Point{target.x - source.x, target.y - source.y})) {
    return std::nullopt;
  }
  const double determinant =
      2.0 * (source.x * (middle.y - target.y) + middle.x * (target.y - source.y) +
             target.x * (source.y - middle.y));
  if (!finite(determinant) || std::abs(determinant) <= kCollinearTolerance) {
    return std::nullopt;
  }
  const double source_squared = source.x * source.x + source.y * source.y;
  const double middle_squared = middle.x * middle.x + middle.y * middle.y;
  const double target_squared = target.x * target.x + target.y * target.y;
  const core::Point center_normalized{
      (source_squared * (middle.y - target.y) + middle_squared * (target.y - source.y) +
       target_squared * (source.y - middle.y)) /
          determinant,
      (source_squared * (target.x - middle.x) + middle_squared * (source.x - target.x) +
       target_squared * (middle.x - source.x)) /
          determinant};
  const double radius_normalized = std::hypot(
      source.x - center_normalized.x, source.y - center_normalized.y);
  const core::Point center{center_normalized.x * coordinate_scale,
                           center_normalized.y * coordinate_scale};
  const double radius = radius_normalized * coordinate_scale;
  if (!finite(center) || !sufficientlyLarge(radius)) {
    return std::nullopt;
  }

  const double start_angle = std::atan2(source.y - center_normalized.y,
                                        source.x - center_normalized.x);
  const double interior_angle = std::atan2(middle.y - center_normalized.y,
                                           middle.x - center_normalized.x);
  const double target_angle = std::atan2(target.y - center_normalized.y,
                                         target.x - center_normalized.x);
  const double ccw_sweep = positiveAngleDelta(start_angle, target_angle);
  const double ccw_interior = positiveAngleDelta(start_angle, interior_angle);
  if (!finite(ccw_sweep) || !finite(ccw_interior) || ccw_sweep <= kCollinearTolerance) {
    return std::nullopt;
  }
  const bool counterclockwise = ccw_interior <= ccw_sweep + kCollinearTolerance;
  const double sweep_radians = counterclockwise ? ccw_sweep : ccw_sweep - 2.0 * std::numbers::pi_v<double>;
  const double start_degrees = start_angle * 180.0 / std::numbers::pi_v<double>;
  const double sweep_degrees = sweep_radians * 180.0 / std::numbers::pi_v<double>;
  if (!finite(start_degrees) || !finite(sweep_degrees) ||
      std::abs(sweep_degrees) <= kCollinearTolerance ||
      std::abs(sweep_degrees) >= kFullTurnDegrees) {
    return std::nullopt;
  }
  return PrimitivePlacement{
      core::Arc{radius, start_degrees, sweep_degrees},
      core::Transform{center}};
}

std::optional<PrimitivePlacement> placeGoldenByCenterAndShortSideVector(
    const core::Point center,
    const core::Point short_side_vector) noexcept {
  if (!finite(center) || !finite(short_side_vector)) {
    return std::nullopt;
  }
  const double short_side = std::hypot(short_side_vector.x, short_side_vector.y);
  if (!sufficientlyLarge(short_side)) {
    return std::nullopt;
  }
  const double rotation = std::atan2(short_side_vector.y, short_side_vector.x) *
                              180.0 / std::numbers::pi_v<double> -
                          90.0;
  if (!finite(rotation)) {
    return std::nullopt;
  }
  return PrimitivePlacement{
      core::GoldenRectangle{short_side}, core::Transform{center, rotation, {1.0, 1.0}}};
}

std::optional<PrimitivePlacement> placeGoldenByShortEdge(
    const core::Point short_edge_start,
    const core::Point short_edge_end,
    const GoldenEdgeSide side) noexcept {
  if (!finite(short_edge_start) || !finite(short_edge_end)) {
    return std::nullopt;
  }
  const core::Point edge{short_edge_end.x - short_edge_start.x,
                         short_edge_end.y - short_edge_start.y};
  const double short_side = std::hypot(edge.x, edge.y);
  if (!sufficientlyLarge(short_side)) {
    return std::nullopt;
  }
  const core::Point left_normal{-edge.y / short_side, edge.x / short_side};
  const double side_value = side == GoldenEdgeSide::left_of_directed_edge ? 1.0 : -1.0;
  const core::Point long_direction{left_normal.x * side_value, left_normal.y * side_value};
  const double long_side = short_side * std::numbers::phi_v<double>;
  const core::Point edge_midpoint{short_edge_start.x / 2.0 + short_edge_end.x / 2.0,
                                  short_edge_start.y / 2.0 + short_edge_end.y / 2.0};
  const core::Point center{edge_midpoint.x + long_direction.x * long_side / 2.0,
                           edge_midpoint.y + long_direction.y * long_side / 2.0};
  const double rotation = std::atan2(long_direction.y, long_direction.x) *
                          180.0 / std::numbers::pi_v<double>;
  if (!finite(center) || !finite(rotation)) {
    return std::nullopt;
  }
  return PrimitivePlacement{
      core::GoldenRectangle{short_side}, core::Transform{center, rotation, {1.0, 1.0}}};
}

std::optional<std::vector<SelectionTransform>> rotateSelection(
    const std::span<const SelectionItem> items,
    const core::Point pivot,
    const double delta_degrees) noexcept {
  if (!finite(pivot) || !finite(delta_degrees)) {
    return std::nullopt;
  }
  const auto ordered = orderedSelection(items);
  if (!ordered.has_value()) {
    return std::nullopt;
  }
  std::vector<SelectionTransform> result;
  result.reserve(ordered->size());
  for (const auto& item : *ordered) {
    const core::Point relative{item.transform.translation.x - pivot.x,
                               item.transform.translation.y - pivot.y};
    const core::Point rotated = rotateVector(relative, delta_degrees);
    const core::Transform transform{
        {pivot.x + rotated.x, pivot.y + rotated.y},
        item.transform.rotation_degrees + delta_degrees,
        item.transform.scale};
    if (!finite(transform)) {
      return std::nullopt;
    }
    result.push_back({item.node_id, transform});
  }
  return result;
}

std::optional<std::vector<SelectionTransform>> resizeSelectionUniform(
    const std::span<const SelectionItem> items,
    const core::Point fixed_pivot,
    const double scale_factor) noexcept {
  if (!finite(fixed_pivot) || !sufficientlyLarge(scale_factor)) {
    return std::nullopt;
  }
  const auto ordered = orderedSelection(items);
  if (!ordered.has_value()) {
    return std::nullopt;
  }
  std::vector<SelectionTransform> result;
  result.reserve(ordered->size());
  for (const auto& item : *ordered) {
    const core::Point relative{item.transform.translation.x - fixed_pivot.x,
                               item.transform.translation.y - fixed_pivot.y};
    const core::Point scaled{relative.x * scale_factor, relative.y * scale_factor};
    const core::Transform transform{
        {fixed_pivot.x + scaled.x, fixed_pivot.y + scaled.y},
        item.transform.rotation_degrees,
        {item.transform.scale.x * scale_factor, item.transform.scale.y * scale_factor}};
    if (!finite(transform)) {
      return std::nullopt;
    }
    result.push_back({item.node_id, transform});
  }
  return result;
}

std::optional<core::Transform> resizeRectangleNonUniform(
    const core::Rectangle& rectangle,
    const core::Transform& original,
    const core::Point fixed_opposite_local,
    const core::Point moving_local,
    const core::Point moving_corner_world) noexcept {
  return rectangleResizeFromLocalFixed(
      rectangle, original, fixed_opposite_local, moving_local, moving_corner_world, std::nullopt);
}

std::optional<core::Transform> resizeRectangleNonUniformFromFixedWorld(
    const core::Rectangle& rectangle,
    const core::Transform& original,
    const core::Point fixed_opposite_world,
    const core::Point moving_corner_world) noexcept {
  const auto fixed_local = inverseTransform(fixed_opposite_world, original);
  if (!fixed_local.has_value()) {
    return std::nullopt;
  }
  const double half_width = rectangle.width / 2.0;
  const double half_height = rectangle.height / 2.0;
  if (!finite(rectangle.width) || !finite(rectangle.height) ||
      !sufficientlyLarge(rectangle.width) || !sufficientlyLarge(rectangle.height) ||
      !validRectangleCorner(rectangle, *fixed_local)) {
    return std::nullopt;
  }
  const core::Point moving_local{
      closeEnough(fixed_local->x, half_width) ? -half_width : half_width,
      closeEnough(fixed_local->y, half_height) ? -half_height : half_height};
  return rectangleResizeFromLocalFixed(
      rectangle, original, *fixed_local, moving_local, moving_corner_world,
      fixed_opposite_world);
}

std::optional<core::Transform> resizeRectangleEdgeNonUniform(
    const core::Rectangle& rectangle,
    const core::Transform& original,
    const core::Point fixed_opposite_local,
    const core::Point moving_local,
    const core::Point moving_edge_world) noexcept {
  if (!finite(rectangle.width) || !finite(rectangle.height) ||
      !sufficientlyLarge(rectangle.width) || !sufficientlyLarge(rectangle.height) ||
      !finite(original) || !finite(fixed_opposite_local) || !finite(moving_local) ||
      !finite(moving_edge_world)) {
    return std::nullopt;
  }
  const double half_width = rectangle.width / 2.0;
  const double half_height = rectangle.height / 2.0;
  const double tolerance = kRelativeTolerance * std::max({1.0, rectangle.width, rectangle.height});
  const bool fixed_vertical = std::abs(std::abs(fixed_opposite_local.x) - half_width) <= tolerance &&
                              std::abs(fixed_opposite_local.y) <= tolerance;
  const bool fixed_horizontal = std::abs(fixed_opposite_local.x) <= tolerance &&
                                std::abs(std::abs(fixed_opposite_local.y) - half_height) <= tolerance;
  const bool moving_vertical = std::abs(std::abs(moving_local.x) - half_width) <= tolerance &&
                               std::abs(moving_local.y) <= tolerance;
  const bool moving_horizontal = std::abs(moving_local.x) <= tolerance &&
                                 std::abs(std::abs(moving_local.y) - half_height) <= tolerance;
  if ((!fixed_vertical && !fixed_horizontal) ||
      (!moving_vertical && !moving_horizontal) ||
      (fixed_vertical != moving_vertical)) {
    return std::nullopt;
  }
  const auto fixed_world = applyTransform(fixed_opposite_local, original);
  if (!fixed_world.has_value()) {
    return std::nullopt;
  }
  const core::Point world_delta{moving_edge_world.x - fixed_world->x,
                                moving_edge_world.y - fixed_world->y};
  const core::Point local_delta = rotateVector(world_delta, -original.rotation_degrees);
  const double source_delta = fixed_vertical
                                  ? moving_local.x - fixed_opposite_local.x
                                  : moving_local.y - fixed_opposite_local.y;
  const double target_delta = fixed_vertical ? local_delta.x : local_delta.y;
  if (!sufficientlyLarge(std::abs(source_delta)) || !finite(target_delta)) {
    return std::nullopt;
  }
  const double axis_scale = target_delta / source_delta;
  if (!sufficientlyLarge(axis_scale)) {
    return std::nullopt;
  }
  const double scale_x = fixed_vertical ? axis_scale : original.scale.x;
  const double scale_y = fixed_vertical ? original.scale.y : axis_scale;
  if (!sufficientlyLarge(scale_x) || !sufficientlyLarge(scale_y)) {
    return std::nullopt;
  }
  const core::Point fixed_scaled{fixed_opposite_local.x * scale_x,
                                 fixed_opposite_local.y * scale_y};
  const core::Point fixed_rotated = rotateVector(fixed_scaled, original.rotation_degrees);
  const core::Transform result{
      {fixed_world->x - fixed_rotated.x, fixed_world->y - fixed_rotated.y},
      original.rotation_degrees,
      {scale_x, scale_y}};
  return finite(result) ? std::optional<core::Transform>(result) : std::nullopt;
}

std::optional<SelectionBounds> boundsFromPoints(
    const std::span<const core::Point> points) noexcept {
  if (points.empty()) {
    return std::nullopt;
  }
  SelectionBounds result{points.front(), points.front()};
  if (!finite(result.min)) {
    return std::nullopt;
  }
  for (const core::Point point : points) {
    if (!finite(point)) {
      return std::nullopt;
    }
    result.min.x = std::min(result.min.x, point.x);
    result.min.y = std::min(result.min.y, point.y);
    result.max.x = std::max(result.max.x, point.x);
    result.max.y = std::max(result.max.y, point.y);
  }
  return result;
}

std::optional<core::SymmetryAxis> symmetryAxisForSelection(
    const std::span<const SelectionItem> items,
    const ReflectionAxis axis) noexcept {
  if (items.empty()) {
    return std::nullopt;
  }
  std::vector<core::Point> points;
  for (const auto& item : items) {
    if (!finite(item.transform)) {
      return std::nullopt;
    }
    const auto local_points = std::visit(
        [](const auto& value) {
          using Primitive = std::decay_t<decltype(value)>;
          std::vector<core::Point> result;
          if constexpr (std::is_same_v<Primitive, core::Circle>) {
            result = {{-value.radius, 0.0}, {value.radius, 0.0},
                      {0.0, -value.radius}, {0.0, value.radius}};
          } else if constexpr (std::is_same_v<Primitive, core::Rectangle>) {
            result = {{-value.width / 2.0, -value.height / 2.0},
                      {value.width / 2.0, -value.height / 2.0},
                      {value.width / 2.0, value.height / 2.0},
                      {-value.width / 2.0, value.height / 2.0}};
          } else if constexpr (std::is_same_v<Primitive, core::GoldenRectangle>) {
            const double half_width = value.longSide() / 2.0;
            const double half_height = value.short_side / 2.0;
            result = {{-half_width, -half_height}, {half_width, -half_height},
                      {half_width, half_height}, {-half_width, half_height}};
          } else {
            constexpr int sample_count = 720;
            result.reserve(sample_count + 1);
            for (int index = 0; index <= sample_count; ++index) {
              const double fraction = static_cast<double>(index) / sample_count;
              const double degrees = value.start_degrees + value.sweep_degrees * fraction;
              const double radians = degrees * std::numbers::pi_v<double> / 180.0;
              result.push_back({value.radius * std::cos(radians),
                                value.radius * std::sin(radians)});
            }
          }
          return result;
        },
        item.primitive);
    for (const auto local : local_points) {
      const auto world = applyTransform(local, item.transform);
      if (!world.has_value()) {
        return std::nullopt;
      }
      points.push_back(*world);
    }
  }
  const auto bounds = boundsFromPoints(points);
  if (!bounds.has_value()) {
    return std::nullopt;
  }
  const core::Point origin{
      bounds->min.x / 2.0 + bounds->max.x / 2.0,
      bounds->min.y / 2.0 + bounds->max.y / 2.0};
  const core::Point direction = axis == ReflectionAxis::vertical
                                    ? core::Point{0.0, 1.0}
                                    : core::Point{1.0, 0.0};
  return finite(origin) ? std::optional<core::SymmetryAxis>(
                              core::SymmetryAxis{origin, direction})
                        : std::nullopt;
}

std::optional<HandleLayout> layoutSelectionHandlesInView(
    const SelectionBounds view_bounds,
    const double handle_size_view,
    const double rotate_offset_view) noexcept {
  if (!finite(view_bounds.min) || !finite(view_bounds.max) ||
      view_bounds.min.x > view_bounds.max.x || view_bounds.min.y > view_bounds.max.y ||
      !finite(handle_size_view) || handle_size_view <= 0.0 ||
      !finite(rotate_offset_view) || rotate_offset_view < 0.0) {
    return std::nullopt;
  }
  const double center_x = view_bounds.min.x / 2.0 + view_bounds.max.x / 2.0;
  const double center_y = view_bounds.min.y / 2.0 + view_bounds.max.y / 2.0;
  if (!finite(center_x) || !finite(center_y)) {
    return std::nullopt;
  }
  const std::array<core::Point, 8> handles{
      core::Point{view_bounds.min.x, view_bounds.min.y},
      core::Point{center_x, view_bounds.min.y},
      core::Point{view_bounds.max.x, view_bounds.min.y},
      core::Point{view_bounds.max.x, center_y},
      core::Point{view_bounds.max.x, view_bounds.max.y},
      core::Point{center_x, view_bounds.max.y},
      core::Point{view_bounds.min.x, view_bounds.max.y},
      core::Point{view_bounds.min.x, center_y}};
  const core::Point rotate_handle{center_x, view_bounds.min.y - rotate_offset_view};
  if (!finite(rotate_handle)) {
    return std::nullopt;
  }
  return HandleLayout{view_bounds, handles, rotate_handle, handle_size_view, rotate_offset_view};
}

}  // namespace signet::ui::interaction
