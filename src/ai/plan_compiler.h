// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "ai/logo_construction_plan.h"

#include <utility>

namespace signet::ai {

struct PlanCompilationResult final {
  std::optional<core::Document> document;
  std::vector<PlanDiagnostic> diagnostics;
  std::vector<std::pair<std::string, core::NodeId>> node_ids;

  [[nodiscard]] bool accepted() const noexcept { return document.has_value(); }
  [[nodiscard]] explicit operator bool() const noexcept { return accepted(); }
};

struct PlanApplyResult final {
  bool accepted{false};
  std::vector<PlanDiagnostic> diagnostics;
  std::vector<std::pair<std::string, core::NodeId>> node_ids;

  [[nodiscard]] explicit operator bool() const noexcept { return accepted; }
};

class PlanCompiler final {
 public:
  // Compile against a copy of the supplied document.  This is the preview
  // boundary: the caller's document remains unchanged until apply() is
  // explicitly requested.
  [[nodiscard]] static PlanCompilationResult preview(
      const LogoConstructionPlan& plan,
      const core::Document& base) {
    return compile(plan, base);
  }
  [[nodiscard]] static PlanCompilationResult compile(
      const LogoConstructionPlan& plan,
      const core::Document& base);
  [[nodiscard]] static PlanApplyResult apply(
      const LogoConstructionPlan& plan,
      core::DocumentHistory& history);
};

}  // namespace signet::ai
