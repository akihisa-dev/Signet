// SPDX-License-Identifier: AGPL-3.0-or-later
#include "geometry/alignment.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <ranges>
#include <tuple>
#include <type_traits>

namespace signet::geometry {
namespace {

[[nodiscard]] bool finite(const double value) noexcept { return std::isfinite(value); }

[[nodiscard]] bool validBounds(const AxisAlignedBounds& bounds) noexcept {
  return finite(bounds.min_x) && finite(bounds.min_y) && finite(bounds.max_x) &&
         finite(bounds.max_y) && bounds.min_x <= bounds.max_x && bounds.min_y <= bounds.max_y;
}

[[nodiscard]] bool validItems(std::span<const AlignmentItem> items) {
  std::vector<AlignmentIdentity> identities;
  identities.reserve(items.size());
  for (const AlignmentItem& item : items) {
    if (!validBounds(item.bounds)) {
      return false;
    }
    identities.push_back(item.identity);
  }
  std::ranges::sort(identities);
  return std::ranges::adjacent_find(identities) == identities.end();
}

[[nodiscard]] bool validReference(const AlignmentReference& reference) noexcept {
  return std::visit(
      [](const auto& value) {
        using Reference = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Reference, SelectionBoundsReference>) {
          return validBounds(value.bounds);
        } else {
          return finite(value.coordinate);
        }
      },
      reference);
}

[[nodiscard]] long double center(const double minimum, const double maximum) noexcept {
  return static_cast<long double>(minimum) +
         (static_cast<long double>(maximum) - static_cast<long double>(minimum)) / 2.0L;
}

[[nodiscard]] bool finiteTranslation(const long double value) noexcept {
  const double converted = static_cast<double>(value);
  return finite(converted);
}

[[nodiscard]] std::optional<double> translation(const long double target, const double current) {
  const long double delta = target - static_cast<long double>(current);
  if (!std::isfinite(delta) || !finiteTranslation(delta)) {
    return std::nullopt;
  }
  return static_cast<double>(delta);
}

[[nodiscard]] std::optional<double> alignmentCoordinate(
    const AlignmentMode mode,
    const AlignmentReference& reference,
    const bool horizontal) {
  return std::visit(
      [mode, horizontal](const auto& value) -> std::optional<double> {
        using Reference = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Reference, AnchorCoordinateReference>) {
          if (!finite(value.coordinate)) {
            return std::nullopt;
          }
          return value.coordinate;
        } else {
          if (!validBounds(value.bounds)) {
            return std::nullopt;
          }
          if (horizontal) {
            switch (mode) {
              case AlignmentMode::left:
                return value.bounds.min_x;
              case AlignmentMode::horizontal_center:
                return static_cast<double>(center(value.bounds.min_x, value.bounds.max_x));
              case AlignmentMode::right:
                return value.bounds.max_x;
              default:
                return std::nullopt;
            }
          }
          switch (mode) {
            case AlignmentMode::top:
              return value.bounds.max_y;
            case AlignmentMode::vertical_center:
              return static_cast<double>(center(value.bounds.min_y, value.bounds.max_y));
            case AlignmentMode::bottom:
              return value.bounds.min_y;
            default:
              return std::nullopt;
          }
        }
      },
      reference);
}

[[nodiscard]] bool isHorizontalMode(const AlignmentMode mode) noexcept {
  return mode == AlignmentMode::left || mode == AlignmentMode::horizontal_center ||
         mode == AlignmentMode::right;
}

}  // namespace

std::optional<std::vector<AlignmentTranslation>> align(
    const std::span<const AlignmentItem> items,
    const AlignmentMode mode,
    const AlignmentReference& reference) {
  if (items.empty() || !validItems(items) || !validReference(reference)) {
    return std::nullopt;
  }

  const bool horizontal = isHorizontalMode(mode);
  const auto target = alignmentCoordinate(mode, reference, horizontal);
  if (!target.has_value()) {
    return std::nullopt;
  }

  std::vector<AlignmentTranslation> result;
  result.reserve(items.size());
  for (const AlignmentItem& item : items) {
    const long double current = [&] {
      if (horizontal) {
        if (mode == AlignmentMode::horizontal_center) {
          return center(item.bounds.min_x, item.bounds.max_x);
        }
        return mode == AlignmentMode::left ? static_cast<long double>(item.bounds.min_x)
                                           : static_cast<long double>(item.bounds.max_x);
      }
      if (mode == AlignmentMode::vertical_center) {
        return center(item.bounds.min_y, item.bounds.max_y);
      }
      return mode == AlignmentMode::top ? static_cast<long double>(item.bounds.max_y)
                                        : static_cast<long double>(item.bounds.min_y);
    }();
    const auto delta = translation(static_cast<long double>(*target), static_cast<double>(current));
    if (!delta.has_value()) {
      return std::nullopt;
    }
    result.push_back(AlignmentTranslation{
        item.identity,
        horizontal ? *delta : 0.0,
        horizontal ? 0.0 : *delta,
    });
  }
  std::ranges::sort(result, {}, &AlignmentTranslation::identity);
  return result;
}

std::optional<std::vector<AlignmentTranslation>> distribute(
    const std::span<const AlignmentItem> items,
    const DistributionAxis axis) {
  if (items.size() < 3 || !validItems(items)) {
    return std::nullopt;
  }

  const bool horizontal = axis == DistributionAxis::horizontal;
  if (axis != DistributionAxis::horizontal && axis != DistributionAxis::vertical) {
    return std::nullopt;
  }

  std::vector<const AlignmentItem*> ordered;
  ordered.reserve(items.size());
  for (const AlignmentItem& item : items) {
    ordered.push_back(&item);
  }
  std::ranges::sort(ordered, [horizontal](const AlignmentItem* left, const AlignmentItem* right) {
    const auto left_key = horizontal
                              ? std::tie(left->bounds.min_x, left->identity)
                              : std::tie(left->bounds.min_y, left->identity);
    const auto right_key = horizontal
                               ? std::tie(right->bounds.min_x, right->identity)
                               : std::tie(right->bounds.min_y, right->identity);
    return left_key < right_key;
  });

  const auto start = [horizontal](const AlignmentItem& item) {
    return horizontal ? static_cast<long double>(item.bounds.min_x)
                      : static_cast<long double>(item.bounds.min_y);
  };
  const auto end = [horizontal](const AlignmentItem& item) {
    return horizontal ? static_cast<long double>(item.bounds.max_x)
                      : static_cast<long double>(item.bounds.max_y);
  };
  const long double first_end = end(*ordered.front());
  const long double last_start = start(*ordered.back());
  long double middle_extent = 0.0L;
  for (std::size_t index = 1; index + 1 < ordered.size(); ++index) {
    middle_extent += end(*ordered[index]) - start(*ordered[index]);
  }
  const long double gap =
      (last_start - first_end - middle_extent) /
      static_cast<long double>(ordered.size() - 1);
  if (!std::isfinite(gap)) {
    return std::nullopt;
  }

  std::vector<AlignmentTranslation> result;
  result.reserve(items.size());
  result.push_back(AlignmentTranslation{ordered.front()->identity, 0.0, 0.0});
  long double cursor = first_end;
  for (std::size_t index = 1; index + 1 < ordered.size(); ++index) {
    const long double target = cursor + gap;
    const auto delta = translation(target, static_cast<double>(start(*ordered[index])));
    if (!delta.has_value()) {
      return std::nullopt;
    }
    result.push_back(AlignmentTranslation{
        ordered[index]->identity,
        horizontal ? *delta : 0.0,
        horizontal ? 0.0 : *delta,
    });
    cursor = target + (end(*ordered[index]) - start(*ordered[index]));
    if (!std::isfinite(cursor)) {
      return std::nullopt;
    }
  }
  result.push_back(AlignmentTranslation{ordered.back()->identity, 0.0, 0.0});
  std::ranges::sort(result, {}, &AlignmentTranslation::identity);
  return result;
}

}  // namespace signet::geometry
