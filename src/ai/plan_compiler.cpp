// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ai/plan_compiler.h"

#include "geometry/document_evaluator.h"

#include <algorithm>
#include <functional>
#include <type_traits>
#include <unordered_map>

namespace signet::ai {

namespace {

void addDiagnostic(
    std::vector<PlanDiagnostic>& diagnostics,
    std::string path,
    std::string message) {
  diagnostics.push_back(PlanDiagnostic{std::move(path), std::move(message)});
}

std::string diagnosticsSummary(const std::vector<PlanDiagnostic>& diagnostics) {
  if (diagnostics.empty()) {
    return {};
  }
  return diagnostics.front().path + ": " + diagnostics.front().message;
}

const geometry::EvaluatedBoolean* evaluatedBoolean(
    const geometry::DocumentEvaluationSnapshot& snapshot,
    const core::NodeId id) {
  const auto found = std::ranges::find(snapshot.booleans, id, &geometry::EvaluatedBoolean::node_id);
  return found == snapshot.booleans.end() ? nullptr : &*found;
}

bool rootEvaluated(
    const geometry::DocumentEvaluationSnapshot& snapshot,
    const core::NodeId id,
    std::vector<PlanDiagnostic>& diagnostics,
    const std::string& path) {
  const bool has_geometry =
      std::ranges::any_of(snapshot.circles, [id](const auto& value) { return value.node_id == id; }) ||
      std::ranges::any_of(snapshot.curve_sets, [id](const auto& value) { return value.node_id == id; });
  if (has_geometry) {
    return true;
  }
  if (const auto* boolean = evaluatedBoolean(snapshot, id); boolean != nullptr) {
    if (boolean->regions.empty()) {
      addDiagnostic(diagnostics, path, "root Boolean result is empty");
      return false;
    }
    return true;
  }
  const auto split = std::ranges::find(snapshot.splits, id, &geometry::EvaluatedSplit::node_id);
  if (split != snapshot.splits.end()) {
    if (split->cells.empty()) {
      addDiagnostic(diagnostics, path, "root Split result is empty");
      return false;
    }
    return true;
  }
  const auto selection = std::ranges::find(
      snapshot.region_selections, id, &geometry::EvaluatedRegionSelection::node_id);
  if (selection != snapshot.region_selections.end()) {
    if (selection->cells.empty()) {
      addDiagnostic(diagnostics, path, "root RegionSelection result is empty");
      return false;
    }
    return true;
  }
  const auto filter = std::ranges::find(snapshot.region_filters, id,
                                        &geometry::EvaluatedRegionFilter::node_id);
  if (filter != snapshot.region_filters.end()) {
    if (filter->cells.empty()) {
      addDiagnostic(diagnostics, path, "root RegionFilter result is empty");
      return false;
    }
    return true;
  }
  addDiagnostic(diagnostics, path, "root did not produce evaluated geometry");
  return false;
}

}  // namespace

PlanCompilationResult PlanCompiler::compile(
    const LogoConstructionPlan& plan,
    const core::Document& base) {
  PlanCompilationResult result;
  result.diagnostics = validateLogoConstructionPlan(plan);
  if (!result.diagnostics.empty()) {
    return result;
  }

  core::Document document = base;
  std::unordered_map<std::string, core::NodeId> ids;
  ids.reserve(plan.nodes.size());
  std::vector<std::size_t> order;
  order.reserve(plan.nodes.size());
  std::unordered_map<std::string, std::size_t> indices;
  indices.reserve(plan.nodes.size());
  for (std::size_t index = 0; index < plan.nodes.size(); ++index) {
    indices.emplace(plan.nodes[index].id, index);
  }
  std::vector<std::uint8_t> state(plan.nodes.size(), 0U);
  std::function<bool(std::size_t)> visit = [&](const std::size_t index) {
    if (state[index] == 1U) {
      addDiagnostic(result.diagnostics, "nodes[" + std::to_string(index) + "]", "cycle detected");
      return false;
    }
    if (state[index] == 2U) {
      return true;
    }
    state[index] = 1U;
    const auto visitReference = [&](const std::string& id) {
      const auto found = indices.find(id);
      return found != indices.end() && visit(found->second);
    };
    bool valid = std::visit(
        [&visitReference](const auto& definition) {
          using Definition = std::decay_t<decltype(definition)>;
          if constexpr (std::is_same_v<Definition, PlanPrimitive>) {
            return true;
          } else if constexpr (std::is_same_v<Definition, PlanBoolean>) {
            return visitReference(definition.left) && visitReference(definition.right);
          } else {
            return visitReference(definition.input);
          }
        },
        plan.nodes[index].definition);
    state[index] = 2U;
    if (valid) {
      order.push_back(index);
    }
    return valid;
  };
  for (std::size_t index = 0; index < plan.nodes.size(); ++index) {
    visit(index);
  }
  if (!result.diagnostics.empty()) {
    return result;
  }

  for (const auto index : order) {
    const auto& node = plan.nodes[index];
    try {
      const auto id = std::visit(
          [&document, &ids, &node](const auto& definition) -> core::NodeId {
            using Definition = std::decay_t<decltype(definition)>;
            if constexpr (std::is_same_v<Definition, PlanPrimitive>) {
              return document.addPrimitive(node.name, definition.primitive, definition.transform);
            } else if constexpr (std::is_same_v<Definition, PlanBoolean>) {
              return document.addBoolean(node.name, definition.operation, ids.at(definition.left),
                                         ids.at(definition.right));
            } else {
              return document.addSymmetry(node.name, ids.at(definition.input), definition.axis);
            }
          },
          node.definition);
      ids.emplace(node.id, id);
      result.node_ids.emplace_back(node.id, id);
    } catch (const std::exception& error) {
      addDiagnostic(result.diagnostics, "nodes[" + std::to_string(index) + "]", error.what());
      return result;
    }
  }

  const auto snapshot = geometry::DocumentEvaluator::evaluate(document);
  for (const auto& diagnostic : snapshot.diagnostics) {
    addDiagnostic(result.diagnostics, "document.node." + std::to_string(diagnostic.node_id),
                  diagnostic.reason);
  }
  if (!result.diagnostics.empty()) {
    return result;
  }
  for (std::size_t index = 0; index < plan.roots.size(); ++index) {
    const auto found = ids.find(plan.roots[index]);
    if (found == ids.end()) {
      addDiagnostic(result.diagnostics, "roots[" + std::to_string(index) + "]",
                    "root reference is not compiled");
      continue;
    }
    rootEvaluated(snapshot, found->second, result.diagnostics,
                  "roots[" + std::to_string(index) + "]");
  }
  if (result.diagnostics.empty()) {
    result.document = std::move(document);
  }
  return result;
}

PlanApplyResult PlanCompiler::apply(
    const LogoConstructionPlan& plan,
    core::DocumentHistory& history) {
  PlanApplyResult result;
  const auto apply_result = history.applyAtomic([&](core::Document& document) {
    const auto compiled = compile(plan, document);
    if (!compiled) {
      result.diagnostics = compiled.diagnostics;
      return core::AtomicApplyResult{false, diagnosticsSummary(compiled.diagnostics)};
    }
    document = *compiled.document;
    result.node_ids = compiled.node_ids;
    return core::AtomicApplyResult{true, {}};
  });
  result.accepted = apply_result.accepted;
  if (!result.accepted && result.diagnostics.empty() && !apply_result.reason.empty()) {
    result.diagnostics.push_back(PlanDiagnostic{"apply", apply_result.reason});
  }
  return result;
}

}  // namespace signet::ai
