// SPDX-License-Identifier: AGPL-3.0-or-later
#include "geometry/alignment.h"

#include <cassert>
#include <cmath>
#include <limits>
#include <vector>

namespace {

using signet::geometry::AlignmentIdentity;
using signet::geometry::AlignmentItem;
using signet::geometry::AlignmentMode;
using signet::geometry::AlignmentReference;
using signet::geometry::AlignmentTranslation;
using signet::geometry::AnchorCoordinateReference;
using signet::geometry::AxisAlignedBounds;
using signet::geometry::DistributionAxis;
using signet::geometry::SelectionBoundsReference;

bool close(const double left, const double right) {
  return std::abs(left - right) <= 1.0e-12;
}

const AlignmentTranslation& translation(
    const std::vector<AlignmentTranslation>& translations,
    const AlignmentIdentity identity) {
  for (const auto& value : translations) {
    if (value.identity == identity) {
      return value;
    }
  }
  assert(false);
  return translations.front();
}

void assertTranslation(
    const std::vector<AlignmentTranslation>& translations,
    const AlignmentIdentity identity,
    const double dx,
    const double dy) {
  const auto& value = translation(translations, identity);
  assert(close(value.dx, dx));
  assert(close(value.dy, dy));
}

void assertSorted(const std::vector<AlignmentTranslation>& translations) {
  for (std::size_t index = 1; index < translations.size(); ++index) {
    assert(translations[index - 1].identity < translations[index].identity);
  }
}

AlignmentItem item(
    const AlignmentIdentity identity,
    const double min_x,
    const double min_y,
    const double max_x,
    const double max_y) {
  return AlignmentItem{identity, AxisAlignedBounds{min_x, min_y, max_x, max_y}};
}

}  // namespace

int main() {
  using signet::geometry::align;
  using signet::geometry::distribute;

  const std::vector<AlignmentItem> items{
      item(30, 10.0, 20.0, 14.0, 26.0),
      item(10, -4.0, -8.0, 0.0, 0.0),
      item(20, 4.0, 5.0, 8.0, 15.0),
  };
  const auto original = items;
  const AlignmentReference selection =
      SelectionBoundsReference{AxisAlignedBounds{-20.0, -30.0, 20.0, 30.0}};

  const auto left = align(items, AlignmentMode::left, selection);
  assert(left.has_value());
  assertSorted(*left);
  assertTranslation(*left, 10, -16.0, 0.0);
  assertTranslation(*left, 20, -24.0, 0.0);
  assertTranslation(*left, 30, -30.0, 0.0);

  const auto center = align(items, AlignmentMode::horizontal_center, selection);
  assert(center.has_value());
  assertTranslation(*center, 10, 2.0, 0.0);
  assertTranslation(*center, 20, -6.0, 0.0);
  assertTranslation(*center, 30, -12.0, 0.0);

  const auto right = align(items, AlignmentMode::right, selection);
  assert(right.has_value());
  assertTranslation(*right, 10, 20.0, 0.0);
  assertTranslation(*right, 20, 12.0, 0.0);
  assertTranslation(*right, 30, 6.0, 0.0);

  const auto top = align(items, AlignmentMode::top, selection);
  assert(top.has_value());
  assertTranslation(*top, 10, 0.0, 30.0);
  assertTranslation(*top, 20, 0.0, 15.0);
  assertTranslation(*top, 30, 0.0, 4.0);

  const auto vertical_center = align(items, AlignmentMode::vertical_center, selection);
  assert(vertical_center.has_value());
  assertTranslation(*vertical_center, 10, 0.0, 4.0);
  assertTranslation(*vertical_center, 20, 0.0, -10.0);
  assertTranslation(*vertical_center, 30, 0.0, -23.0);

  const auto bottom = align(items, AlignmentMode::bottom, selection);
  assert(bottom.has_value());
  assertTranslation(*bottom, 10, 0.0, -22.0);
  assertTranslation(*bottom, 20, 0.0, -35.0);
  assertTranslation(*bottom, 30, 0.0, -50.0);

  const auto anchored = align(
      items,
      AlignmentMode::horizontal_center,
      AlignmentReference{AnchorCoordinateReference{100.0}});
  assert(anchored.has_value());
  assertTranslation(*anchored, 10, 102.0, 0.0);
  assertTranslation(*anchored, 20, 94.0, 0.0);
  assertTranslation(*anchored, 30, 88.0, 0.0);
  assert(items == original);

  const std::vector<AlignmentItem> evenly_spaced{
      item(30, 20.0, -2.0, 25.0, 2.0),
      item(10, 0.0, 0.0, 10.0, 4.0),
      item(20, 13.0, 10.0, 15.0, 16.0),
      item(40, 40.0, -6.0, 50.0, -1.0),
  };
  const auto evenly_spaced_original = evenly_spaced;
  const auto horizontal = distribute(evenly_spaced, DistributionAxis::horizontal);
  assert(horizontal.has_value());
  assertSorted(*horizontal);
  assertTranslation(*horizontal, 10, 0.0, 0.0);
  assertTranslation(*horizontal, 20, 14.0 / 3.0, 0.0);
  assertTranslation(*horizontal, 30, 22.0 / 3.0, 0.0);
  assertTranslation(*horizontal, 40, 0.0, 0.0);

  const auto vertical = distribute(evenly_spaced, DistributionAxis::vertical);
  assert(vertical.has_value());
  assertTranslation(*vertical, 10, 0.0, 5.0);
  assertTranslation(*vertical, 20, 0.0, 0.0);
  assertTranslation(*vertical, 30, 0.0, 2.0);
  assertTranslation(*vertical, 40, 0.0, 0.0);
  assert(evenly_spaced == evenly_spaced_original);

  const std::vector<AlignmentItem> shuffled{
      evenly_spaced[2], evenly_spaced[0], evenly_spaced[3], evenly_spaced[1],
  };
  assert(distribute(shuffled, DistributionAxis::horizontal) == horizontal);

  const std::vector<AlignmentItem> equal_coordinate{
      item(30, 0.0, 0.0, 1.0, 1.0),
      item(10, 0.0, 0.0, 2.0, 2.0),
      item(20, 5.0, 0.0, 6.0, 1.0),
  };
  const auto tie = distribute(equal_coordinate, DistributionAxis::horizontal);
  assert(tie.has_value());
  assertTranslation(*tie, 10, 0.0, 0.0);
  assertTranslation(*tie, 20, 0.0, 0.0);
  assertTranslation(*tie, 30, 3.0, 0.0);

  const std::vector<AlignmentItem> overlap{
      item(1, 0.0, 0.0, 10.0, 1.0),
      item(2, 5.0, 0.0, 15.0, 1.0),
      item(3, 8.0, 0.0, 20.0, 1.0),
  };
  const auto negative_gap = distribute(overlap, DistributionAxis::horizontal);
  assert(negative_gap.has_value());
  assertTranslation(*negative_gap, 1, 0.0, 0.0);
  assertTranslation(*negative_gap, 2, -1.0, 0.0);
  assertTranslation(*negative_gap, 3, 0.0, 0.0);

  const std::vector<AlignmentItem> empty;
  const auto empty_alignment = align(empty, AlignmentMode::left, selection);
  assert(!empty_alignment.has_value());
  const std::vector<AlignmentItem> one{item(1, 0.0, 0.0, 1.0, 1.0)};
  assert(align(one, AlignmentMode::left, selection).has_value());
  assert(!distribute(empty, DistributionAxis::horizontal).has_value());
  assert(!distribute(one, DistributionAxis::horizontal).has_value());
  const std::vector<AlignmentItem> two{one.front(), item(2, 2.0, 0.0, 3.0, 1.0)};
  assert(!distribute(two, DistributionAxis::horizontal).has_value());

  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double infinity = std::numeric_limits<double>::infinity();
  const std::vector<AlignmentItem> invalid_nan{item(1, nan, 0.0, 1.0, 1.0)};
  assert(!align(invalid_nan, AlignmentMode::left, selection).has_value());
  const std::vector<AlignmentItem> invalid_size{item(1, 0.0, 0.0, -1.0, 1.0)};
  assert(!align(invalid_size, AlignmentMode::left, selection).has_value());
  assert(!align(
              one,
              AlignmentMode::left,
              AlignmentReference{AnchorCoordinateReference{infinity}})
              .has_value());
  assert(!align(
              one,
              AlignmentMode::left,
              AlignmentReference{SelectionBoundsReference{AxisAlignedBounds{0.0, 0.0, -1.0, 1.0}}})
              .has_value());
  const std::vector<AlignmentItem> duplicate_identity{
      item(1, 0.0, 0.0, 1.0, 1.0), item(1, 2.0, 0.0, 3.0, 1.0)};
  assert(!align(duplicate_identity, AlignmentMode::left, selection).has_value());
  assert(!align(one, static_cast<AlignmentMode>(255), selection).has_value());
  const std::vector<AlignmentItem> invalid_axis_items{
      item(1, 0.0, 0.0, 1.0, 1.0), item(2, 2.0, 0.0, 3.0, 1.0),
      item(3, 4.0, 0.0, 5.0, 1.0)};
  assert(!distribute(invalid_axis_items, static_cast<DistributionAxis>(255)).has_value());

  const std::vector<AlignmentItem> huge{
      item(1, 1.0e300, 0.0, 1.0e300 + 1.0e290, 1.0),
      item(2, -1.0e300, 0.0, -1.0e300 + 1.0e290, 1.0),
      item(3, 0.0, 0.0, 1.0e290, 1.0),
  };
  const auto huge_alignment = align(
      huge,
      AlignmentMode::horizontal_center,
      AlignmentReference{AnchorCoordinateReference{0.0}});
  assert(huge_alignment.has_value());
  for (const auto& value : *huge_alignment) {
    assert(std::isfinite(value.dx));
    assert(std::isfinite(value.dy));
  }
}
