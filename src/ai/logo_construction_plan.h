// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "core/document.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace signet::ai {

inline constexpr std::uint32_t kLogoConstructionPlanSchemaVersion = 1;
inline constexpr std::size_t kLogoConstructionPlanMaxBytes = 64U * 1024U;
inline constexpr std::size_t kLogoConstructionPlanMaxNodes = 64U;
inline constexpr std::size_t kLogoConstructionPlanMaxDepth = 32U;
inline constexpr double kLogoConstructionPlanMaxCoordinate = 1.0e6;

struct PlanDiagnostic final {
  std::string path;
  std::string message;

  friend bool operator==(const PlanDiagnostic&, const PlanDiagnostic&) = default;
};

struct PlanCoordinateSystem final {
  std::string unit{"logical"};
  std::string origin{"center"};
  std::string x_axis{"right"};
  std::string y_axis{"up"};
  double min_x{-100.0};
  double min_y{-100.0};
  double max_x{100.0};
  double max_y{100.0};
};

struct PlanPrimitive final {
  core::Primitive primitive;
  core::Transform transform{};
};

struct PlanBoolean final {
  core::BooleanOperation operation{core::BooleanOperation::unite};
  std::string left;
  std::string right;
};

struct PlanSymmetry final {
  std::string input;
  core::SymmetryAxis axis{};
};

using PlanNodeDefinition = std::variant<PlanPrimitive, PlanBoolean, PlanSymmetry>;

struct PlanNode final {
  std::string id;
  std::string name;
  PlanNodeDefinition definition;
};

struct LogoConstructionPlan final {
  std::uint32_t schema_version{kLogoConstructionPlanSchemaVersion};
  PlanCoordinateSystem coordinate_system;
  std::vector<PlanNode> nodes;
  std::vector<std::string> roots;
};

struct PlanParseResult final {
  std::optional<LogoConstructionPlan> plan;
  std::vector<PlanDiagnostic> diagnostics;

  [[nodiscard]] bool accepted() const noexcept { return plan.has_value(); }
  [[nodiscard]] explicit operator bool() const noexcept { return accepted(); }
};

[[nodiscard]] PlanParseResult parseLogoConstructionPlan(std::string_view json);
[[nodiscard]] std::vector<PlanDiagnostic> validateLogoConstructionPlan(
    const LogoConstructionPlan& plan);

}  // namespace signet::ai
