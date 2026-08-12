// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace signet::geometry {

// Bounds are expressed in document coordinates.  Zero width or height is
// valid; a negative extent is not.  The document coordinate system is
// y-up, so top is max_y and bottom is min_y.
struct AxisAlignedBounds final {
  double min_x{};
  double min_y{};
  double max_x{};
  double max_y{};

  friend bool operator==(const AxisAlignedBounds&, const AxisAlignedBounds&) = default;
};

using AlignmentIdentity = std::uint64_t;

struct AlignmentItem final {
  AlignmentIdentity identity{};
  AxisAlignedBounds bounds{};

  friend bool operator==(const AlignmentItem&, const AlignmentItem&) = default;
};

struct AlignmentTranslation final {
  AlignmentIdentity identity{};
  double dx{};
  double dy{};

  friend bool operator==(const AlignmentTranslation&, const AlignmentTranslation&) = default;
};

struct SelectionBoundsReference final {
  // The caller supplies the exact selection reference; no UI selection or
  // other implicit default is consulted.
  AxisAlignedBounds bounds{};
};

struct AnchorCoordinateReference final {
  // For horizontal modes this is an x coordinate; for vertical modes it is
  // a y coordinate.  The caller chooses the axis by choosing the mode.
  double coordinate{};
};

using AlignmentReference = std::variant<SelectionBoundsReference, AnchorCoordinateReference>;

enum class AlignmentMode : std::uint8_t {
  left,
  horizontal_center,
  right,
  top,
  vertical_center,
  bottom,
};

enum class DistributionAxis : std::uint8_t {
  horizontal,
  vertical,
};

// A result is absent when an item, identity, reference, mode, or computed
// translation is invalid.  Results are sorted by identity, so they do not
// depend on input order.  Alignment accepts one or more items; distribution
// requires at least three items because it fixes the two outer items and
// places at least one item between them.
[[nodiscard]] std::optional<std::vector<AlignmentTranslation>> align(
    std::span<const AlignmentItem> items,
    AlignmentMode mode,
    const AlignmentReference& reference);

// Distribution fixes the outer items in deterministic axis-coordinate order.
// The gap is allowed to be negative when bounds overlap; no implicit minimum
// gap or overlap correction is applied.
[[nodiscard]] std::optional<std::vector<AlignmentTranslation>> distribute(
    std::span<const AlignmentItem> items,
    DistributionAxis axis);

}  // namespace signet::geometry
