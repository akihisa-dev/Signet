// SPDX-License-Identifier: AGPL-3.0-or-later
#include "geometry/snap.h"

#include <cassert>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

int main() {
  using signet::geometry::SnapCandidate;
  using signet::geometry::SnapPoint;
  using signet::geometry::snapAngle;
  using signet::geometry::snapToCandidates;
  using signet::geometry::snapToGrid;

  const SnapPoint point{2.49, -1.51};
  const auto grid = snapToGrid(point, 1.0, SnapPoint{0.5, -0.5}, 1.0);
  assert(grid.has_value());
  assert(*grid == (SnapPoint{2.5, -1.5}));
  assert(!snapToGrid(point, 1.0, SnapPoint{}, 0.009).has_value());
  assert(!snapToGrid(point, 0.0, SnapPoint{}, 1.0).has_value());
  assert(!snapToGrid(point, -1.0, SnapPoint{}, 1.0).has_value());
  assert(!snapToGrid(point, std::numeric_limits<double>::infinity(), SnapPoint{}, 1.0).has_value());
  assert(!snapToGrid(point, 1.0, SnapPoint{}, -1.0).has_value());
  assert(!snapToGrid(SnapPoint{std::numeric_limits<double>::quiet_NaN(), 0.0}, 1.0,
                     SnapPoint{}, 1.0).has_value());

  const std::vector<SnapCandidate> candidates{
      {30, SnapPoint{1.0, 0.0}}, {10, SnapPoint{-1.0, 0.0}}, {20, SnapPoint{0.0, 1.0}}};
  const auto selected = snapToCandidates(SnapPoint{0.0, 0.0}, candidates, 1.0);
  assert(selected.has_value() && selected->identity == 10);
  assert(!snapToCandidates(SnapPoint{}, candidates, 0.999).has_value());
  const std::vector<SnapCandidate> invalid{{1, SnapPoint{std::numeric_limits<double>::infinity(), 0.0}}};
  assert(!snapToCandidates(SnapPoint{}, invalid, 1.0).has_value());
  const std::vector<SnapCandidate> reordered{
      {20, SnapPoint{0.0, 1.0}}, {30, SnapPoint{1.0, 0.0}}, {10, SnapPoint{-1.0, 0.0}}};
  assert(snapToCandidates(SnapPoint{}, reordered, 1.0)->identity == 10);

  const double pi = std::numbers::pi_v<double>;
  assert(std::abs(*snapAngle(0.49 * pi, 0.5 * pi, 0.011 * pi) - 0.5 * pi) < 1.0e-12);
  assert(std::abs(*snapAngle(-0.49 * pi, 0.5 * pi, 0.011 * pi) + 0.5 * pi) < 1.0e-12);
  assert(std::abs(*snapAngle(2.0 * pi - 0.01, 0.5 * pi, 0.02)) < 0.02);
  assert(std::abs(*snapAngle(-pi + 0.001, pi, 0.002) + pi) < 1.0e-12);
  assert(std::abs(*snapAngle(pi - 0.001, pi, 0.002) + pi) < 1.0e-12);
  assert(!snapAngle(0.2, 0.5, 0.1).has_value());
  assert(!snapAngle(0.2, 0.0, 1.0).has_value());
  assert(!snapAngle(0.2, 0.5, std::numeric_limits<double>::infinity()).has_value());
  assert(!snapAngle(std::numeric_limits<double>::infinity(), 0.5, 1.0).has_value());
}
