// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace signet::geometry {

// A library-independent point used by the pure snapping helpers.
struct SnapPoint final {
  double x{};
  double y{};

  friend bool operator==(const SnapPoint&, const SnapPoint&) = default;
};

struct SnapCandidate final {
  std::uint64_t identity{};
  SnapPoint point{};
};

// All distances use the same units as the supplied points.  A result is
// absent when validation fails or when no snap is within the requested
// threshold; inputs are never modified.
[[nodiscard]] std::optional<SnapPoint> snapToGrid(
    SnapPoint point, double spacing, SnapPoint origin, double max_distance);

// Candidate identities are caller-owned and are returned unchanged.  Ties
// prefer the smaller identity, then lexicographically smaller coordinates.
[[nodiscard]] std::optional<SnapCandidate> snapToCandidates(
    SnapPoint point, std::span<const SnapCandidate> candidates, double max_distance);

// Angles and angular distances are radians.  The result is normalized to
// [-pi, pi).  Equal angular distances prefer the numerically lower multiple
// of increment before normalization.
[[nodiscard]] std::optional<double> snapAngle(
    double angle, double increment, double max_angular_distance);

}  // namespace signet::geometry
