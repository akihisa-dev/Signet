// SPDX-License-Identifier: AGPL-3.0-or-later
#include "geometry/snap.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <tuple>

namespace signet::geometry {
namespace {

[[nodiscard]] bool finitePoint(const SnapPoint point) noexcept {
  return std::isfinite(point.x) && std::isfinite(point.y);
}

[[nodiscard]] bool validDistance(const double distance) noexcept {
  return std::isfinite(distance) && distance >= 0.0;
}

[[nodiscard]] double distanceBetween(const SnapPoint left, const SnapPoint right) noexcept {
  return std::hypot(left.x - right.x, left.y - right.y);
}

[[nodiscard]] double normalizeAngle(const double angle) noexcept {
  const double period = 2.0 * std::numbers::pi_v<double>;
  double normalized = std::remainder(angle, period);
  if (normalized >= std::numbers::pi_v<double>) {
    normalized -= period;
  }
  return normalized;
}

}  // namespace

std::optional<SnapPoint> snapToGrid(
    const SnapPoint point, const double spacing, const SnapPoint origin,
    const double max_distance) {
  if (!finitePoint(point) || !finitePoint(origin) ||
      !std::isfinite(spacing) || spacing <= 0.0 || !validDistance(max_distance)) {
    return std::nullopt;
  }

  const double x_offset = point.x - origin.x;
  const double y_offset = point.y - origin.y;
  if (!std::isfinite(x_offset) || !std::isfinite(y_offset)) {
    return std::nullopt;
  }
  const SnapPoint snapped{
      origin.x + std::round(x_offset / spacing) * spacing,
      origin.y + std::round(y_offset / spacing) * spacing,
  };
  if (!finitePoint(snapped) || distanceBetween(point, snapped) > max_distance) {
    return std::nullopt;
  }
  return snapped;
}

std::optional<SnapCandidate> snapToCandidates(
    const SnapPoint point, const std::span<const SnapCandidate> candidates,
    const double max_distance) {
  if (!finitePoint(point) || !validDistance(max_distance)) {
    return std::nullopt;
  }

  std::optional<SnapCandidate> best;
  double best_distance = std::numeric_limits<double>::infinity();
  for (const SnapCandidate& candidate : candidates) {
    if (!finitePoint(candidate.point)) {
      return std::nullopt;
    }
    const double distance = distanceBetween(point, candidate.point);
    if (distance > max_distance) {
      continue;
    }
    const bool better = !best || distance < best_distance ||
                        (distance == best_distance &&
                         std::tie(candidate.identity, candidate.point.x, candidate.point.y) <
                             std::tie(best->identity, best->point.x, best->point.y));
    if (better) {
      best = candidate;
      best_distance = distance;
    }
  }
  return best;
}

std::optional<double> snapAngle(
    const double angle, const double increment, const double max_angular_distance) {
  if (!std::isfinite(angle) || !std::isfinite(increment) || increment <= 0.0 ||
      !validDistance(max_angular_distance)) {
    return std::nullopt;
  }

  const double normalized_angle = normalizeAngle(angle);
  double remainder = std::remainder(normalized_angle, increment);
  double snapped = normalized_angle - remainder;
  // At exactly half an increment, choose the lower (numerically smaller)
  // multiple.  This is independent of the caller's input representation.
  if (std::abs(remainder) * 2.0 == increment && remainder < 0.0) {
    snapped -= increment;
  }
  if (!std::isfinite(snapped)) {
    return std::nullopt;
  }
  const double angular_distance = std::abs(std::remainder(normalized_angle - snapped,
                                                           2.0 * std::numbers::pi_v<double>));
  if (angular_distance > max_angular_distance) {
    return std::nullopt;
  }
  return normalizeAngle(snapped);
}

}  // namespace signet::geometry
