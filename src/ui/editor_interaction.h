// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "core/document.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace signet::ui::interaction {

// All points in this API are logical view/document units.  No Qt widget,
// device-pixel ratio, animation clock, or document mutation is involved.
struct Viewport final {
  double width{};
  double height{};
  core::Point pan_offset{};
  double zoom{1.0};
};

[[nodiscard]] std::optional<core::Point> documentToView(
    const Viewport& viewport,
    core::Point document_point) noexcept;
[[nodiscard]] std::optional<core::Point> viewToDocument(
    const Viewport& viewport,
    core::Point view_point) noexcept;

struct PrimitivePlacement final {
  core::Primitive primitive;
  core::Transform transform{};
};

[[nodiscard]] std::optional<PrimitivePlacement> placeCircle(
    core::Point center,
    core::Point drag_point) noexcept;
[[nodiscard]] std::optional<PrimitivePlacement> placeAxisAlignedRectangle(
    core::Point first_corner,
    core::Point opposite_corner) noexcept;

// The returned Arc uses a positive sweep for counterclockwise travel and a
// negative sweep for clockwise travel.  The interior point selects the
// correct minor/major branch; it is not merely a midpoint hint.
[[nodiscard]] std::optional<PrimitivePlacement> placeThreePointArc(
    core::Point start,
    core::Point interior,
    core::Point end) noexcept;

// API A: short_side_vector is the full short-side vector, so its length is
// the persisted GoldenRectangle::short_side (not a half-extent).
[[nodiscard]] std::optional<PrimitivePlacement> placeGoldenByCenterAndShortSideVector(
    core::Point center,
    core::Point short_side_vector) noexcept;

enum class GoldenEdgeSide : std::uint8_t {
  left_of_directed_edge,
  right_of_directed_edge,
};

// API B: the endpoints define a full short edge.  The side enum selects the
// side of the directed endpoint-to-endpoint edge into which the long side
// extends.
[[nodiscard]] std::optional<PrimitivePlacement> placeGoldenByShortEdge(
    core::Point short_edge_start,
    core::Point short_edge_end,
    GoldenEdgeSide side) noexcept;

struct SelectionItem final {
  core::NodeId node_id{};
  core::Primitive primitive;
  core::Transform transform{};
};

struct SelectionTransform final {
  core::NodeId node_id{};
  core::Transform transform{};

  friend bool operator==(const SelectionTransform&, const SelectionTransform&) = default;
};

// Results are always ordered by NodeId.  Duplicate NodeIds, invalid values,
// and inputs that would require an unsupported/sheared representation fail.
[[nodiscard]] std::optional<std::vector<SelectionTransform>> rotateSelection(
    std::span<const SelectionItem> items,
    core::Point pivot,
    double delta_degrees) noexcept;
[[nodiscard]] std::optional<std::vector<SelectionTransform>> resizeSelectionUniform(
    std::span<const SelectionItem> items,
    core::Point fixed_pivot,
    double scale_factor) noexcept;

// The fixed local point and moving local point must be opposite Rectangle
// corners.  The fixed point is kept at its original world position while the
// moving point is placed at moving_corner_world; the transform keeps its
// rotation and performs only axis-aligned local non-uniform scale.
[[nodiscard]] std::optional<core::Transform> resizeRectangleNonUniform(
    const core::Rectangle& rectangle,
    const core::Transform& original,
    core::Point fixed_opposite_local,
    core::Point moving_local,
    core::Point moving_corner_world) noexcept;

// Equivalent world-point form.  The fixed world point is explicit and is
// preserved exactly up to floating-point arithmetic.
[[nodiscard]] std::optional<core::Transform> resizeRectangleNonUniformFromFixedWorld(
    const core::Rectangle& rectangle,
    const core::Transform& original,
    core::Point fixed_opposite_world,
    core::Point moving_corner_world) noexcept;

// The fixed and moving points are centers of opposite Rectangle edges.  The
// edge's local axis is resized while the perpendicular axis remains fixed.
[[nodiscard]] std::optional<core::Transform> resizeRectangleEdgeNonUniform(
    const core::Rectangle& rectangle,
    const core::Transform& original,
    core::Point fixed_opposite_local,
    core::Point moving_local,
    core::Point moving_edge_world) noexcept;

struct SelectionBounds final {
  core::Point min{};
  core::Point max{};
};

[[nodiscard]] std::optional<SelectionBounds> boundsFromPoints(
    std::span<const core::Point> points) noexcept;

// The axis is intentionally limited to the two document axes.  vertical is
// the left/right reflection axis and horizontal is the top/bottom axis.
enum class ReflectionAxis : std::uint8_t {
  vertical,
  horizontal,
};

[[nodiscard]] std::optional<core::SymmetryAxis> symmetryAxisForSelection(
    std::span<const SelectionItem> items,
    ReflectionAxis axis) noexcept;

enum class ResizeHandle : std::uint8_t {
  top_left,
  top_center,
  top_right,
  middle_right,
  bottom_right,
  bottom_center,
  bottom_left,
  middle_left,
};

struct HandleLayout final {
  SelectionBounds bounds;
  std::array<core::Point, 8> resize_handles{};
  core::Point rotate_handle{};
  double handle_size_view{};
  double rotate_offset_view{};
};

// Bounds are in logical view coordinates (y grows downward).  Both size and
// offset are mandatory caller inputs; this function has no UI defaults.
[[nodiscard]] std::optional<HandleLayout> layoutSelectionHandlesInView(
    SelectionBounds view_bounds,
    double handle_size_view,
    double rotate_offset_view) noexcept;

}  // namespace signet::ui::interaction
