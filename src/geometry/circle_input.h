// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

namespace signet::geometry {

// Library-independent circle data shared by geometry consumers.
struct CircleInput final {
  double center_x{};
  double center_y{};
  double radius{1.0};

  friend bool operator==(const CircleInput&, const CircleInput&) = default;
};

}  // namespace signet::geometry
