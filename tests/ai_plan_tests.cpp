// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ai/logo_construction_plan.h"
#include "ai/plan_compiler.h"

#include <cassert>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace {

const char* validPlan() {
  return R"JSON({
    "schema_version": 1,
    "coordinate_system": {
      "unit": "logical", "origin": "center", "x_axis": "right", "y_axis": "up",
      "bounds": [-100, -100, 100, 100]
    },
    "nodes": [
      {"id":"outer", "name":"Outer", "kind":"primitive",
       "primitive":{"type":"circle", "radius":40},
       "transform":{"translation":{"x":0,"y":0},"rotation_degrees":0,"scale":{"x":1,"y":1}}},
      {"id":"inner", "name":"Inner", "kind":"primitive",
       "primitive":{"type":"circle", "radius":15},
       "transform":{"translation":{"x":0,"y":0},"rotation_degrees":0,"scale":{"x":1,"y":1}}},
      {"id":"logo", "name":"Logo", "kind":"boolean", "operation":"subtract",
       "left":"outer", "right":"inner"}
    ],
    "roots": ["logo"]
  })JSON";
}

bool hasDiagnostic(const std::vector<signet::ai::PlanDiagnostic>& diagnostics,
                   const std::string& text) {
  return std::ranges::any_of(diagnostics, [&text](const auto& diagnostic) {
    return diagnostic.path.find(text) != std::string::npos ||
           diagnostic.message.find(text) != std::string::npos;
  });
}

void testValidParseAndCompile() {
  const auto parsed = signet::ai::parseLogoConstructionPlan(validPlan());
  assert(parsed);
  assert(parsed.plan->nodes.size() == 3);
  signet::core::Document base("AI preview");
  const auto compiled = signet::ai::PlanCompiler::preview(*parsed.plan, base);
  assert(compiled);
  assert(base.nodes().empty());
  assert(compiled.document->nodes().size() == 3);
  assert(compiled.node_ids.size() == 3);
}

void testSchemaAndSemanticRejection() {
  const std::string unknown = std::string(validPlan()).replace(
      std::string(validPlan()).find("\"roots\""), 0, "\"extra\":1,");
  assert(!signet::ai::parseLogoConstructionPlan(unknown));

  const auto missing_transform = signet::ai::parseLogoConstructionPlan(R"JSON({
    "schema_version":1,
    "coordinate_system":{"unit":"logical","origin":"center","x_axis":"right","y_axis":"up","bounds":[-100,-100,100,100]},
    "nodes":[{"id":"circle","name":"Circle","kind":"primitive","primitive":{"type":"circle","radius":10}}],
    "roots":["circle"]
  })JSON");
  assert(!missing_transform);
  assert(hasDiagnostic(missing_transform.diagnostics, "transform"));

  const std::string unknown_primitive = std::string(validPlan()).replace(
      std::string(validPlan()).find("\"radius\":40"), std::string("\"radius\":40").size(),
      "\"radius\":40,\"extra\":1");
  const auto unknown_primitive_result =
      signet::ai::parseLogoConstructionPlan(unknown_primitive);
  assert(!unknown_primitive_result);
  assert(hasDiagnostic(unknown_primitive_result.diagnostics, "unknown field"));

  const std::string dangling = std::string(validPlan()).replace(
      std::string(validPlan()).find("\"right\":\"inner\""), 18,
      "\"right\":\"missing\"");
  assert(!signet::ai::parseLogoConstructionPlan(dangling));

  const std::string open_arc = R"JSON({
    "schema_version":1,
    "coordinate_system":{"unit":"logical","origin":"center","x_axis":"right","y_axis":"up","bounds":[-100,-100,100,100]},
    "nodes":[
      {"id":"a","name":"Arc","kind":"primitive","primitive":{"type":"arc","radius":10,"start_degrees":0,"sweep_degrees":90},"transform":{"translation":{"x":0,"y":0},"rotation_degrees":0,"scale":{"x":1,"y":1}}},
      {"id":"b","name":"Circle","kind":"primitive","primitive":{"type":"circle","radius":10},"transform":{"translation":{"x":0,"y":0},"rotation_degrees":0,"scale":{"x":1,"y":1}}},
      {"id":"x","name":"X","kind":"boolean","operation":"unite","left":"a","right":"b"}],
    "roots":["x"]
  })JSON";
  assert(!signet::ai::parseLogoConstructionPlan(open_arc));

  const std::string duplicate = std::string(validPlan()).replace(
      std::string(validPlan()).find("\"id\":\"inner\""), std::string("\"id\":\"inner\"").size(),
      "\"id\":\"outer\"");
  const auto duplicate_result = signet::ai::parseLogoConstructionPlan(duplicate);
  assert(!duplicate_result);
  assert(hasDiagnostic(duplicate_result.diagnostics, "duplicate node identifier"));

  const std::string cycle = std::string(validPlan()).replace(
      std::string(validPlan()).find("\"left\":\"outer\""), std::string("\"left\":\"outer\"").size(),
      "\"left\":\"logo\"");
  const auto cycle_result = signet::ai::parseLogoConstructionPlan(cycle);
  assert(!cycle_result);
  assert(hasDiagnostic(cycle_result.diagnostics, "cycle"));

  const std::string unsupported_symmetry = R"JSON({
    "schema_version":1,
    "coordinate_system":{"unit":"logical","origin":"center","x_axis":"right","y_axis":"up","bounds":[-100,-100,100,100]},
    "nodes":[
      {"id":"a","name":"A","kind":"primitive","primitive":{"type":"circle","radius":10},"transform":{"translation":{"x":0,"y":0},"rotation_degrees":0,"scale":{"x":1,"y":1}}},
      {"id":"b","name":"B","kind":"primitive","primitive":{"type":"circle","radius":5},"transform":{"translation":{"x":0,"y":0},"rotation_degrees":0,"scale":{"x":1,"y":1}}},
      {"id":"x","name":"X","kind":"boolean","operation":"unite","left":"a","right":"b"},
      {"id":"mirror","name":"Mirror","kind":"symmetry","input":"x","axis":{"origin":{"x":0,"y":0},"direction":{"x":1,"y":0}}}],
    "roots":["mirror"]
  })JSON";
  const auto unsupported_result =
      signet::ai::parseLogoConstructionPlan(unsupported_symmetry);
  assert(!unsupported_result);
  assert(hasDiagnostic(unsupported_result.diagnostics, "Boolean result is unsupported"));

  const std::string out_of_range = std::string(validPlan()).replace(
      std::string(validPlan()).find("\"radius\":40"), std::string("\"radius\":40").size(),
      "\"radius\":1000001");
  const auto out_of_range_result = signet::ai::parseLogoConstructionPlan(out_of_range);
  assert(!out_of_range_result);
  assert(hasDiagnostic(out_of_range_result.diagnostics, "bounded"));

  auto invalid_operation_plan = *signet::ai::parseLogoConstructionPlan(validPlan()).plan;
  auto& invalid_operation =
      std::get<signet::ai::PlanBoolean>(invalid_operation_plan.nodes.back().definition);
  invalid_operation.operation = static_cast<signet::core::BooleanOperation>(255);
  const auto invalid_operation_diagnostics =
      signet::ai::validateLogoConstructionPlan(invalid_operation_plan);
  assert(hasDiagnostic(invalid_operation_diagnostics, "unsupported Boolean operation"));

  const std::string oversized(signet::ai::kLogoConstructionPlanMaxBytes + 1U, 'x');
  const auto oversized_result = signet::ai::parseLogoConstructionPlan(oversized);
  assert(!oversized_result);
  assert(hasDiagnostic(oversized_result.diagnostics, "size"));

  signet::ai::LogoConstructionPlan too_many_nodes;
  too_many_nodes.nodes.reserve(signet::ai::kLogoConstructionPlanMaxNodes + 1U);
  for (std::size_t index = 0;
       index < signet::ai::kLogoConstructionPlanMaxNodes + 1U;
       ++index) {
    too_many_nodes.nodes.push_back(signet::ai::PlanNode{
        "node-" + std::to_string(index), "Node", signet::ai::PlanPrimitive{
            signet::core::Circle{1.0}, {}}});
  }
  too_many_nodes.roots = {too_many_nodes.nodes.front().id};
  const auto node_limit_diagnostics =
      signet::ai::validateLogoConstructionPlan(too_many_nodes);
  assert(hasDiagnostic(node_limit_diagnostics, "node count"));

  signet::ai::LogoConstructionPlan too_deep;
  too_deep.nodes.push_back(signet::ai::PlanNode{
      "root", "Root", signet::ai::PlanPrimitive{signet::core::Circle{1.0}, {}}});
  std::string previous = "root";
  for (std::size_t index = 0; index < signet::ai::kLogoConstructionPlanMaxDepth + 1U; ++index) {
    const std::string id = "sym-" + std::to_string(index);
    too_deep.nodes.push_back(signet::ai::PlanNode{
        id,
        "Symmetry",
        signet::ai::PlanSymmetry{previous, signet::core::SymmetryAxis{}}});
    previous = id;
  }
  too_deep.roots = {previous};
  const auto depth_limit_diagnostics = signet::ai::validateLogoConstructionPlan(too_deep);
  assert(hasDiagnostic(depth_limit_diagnostics, "depth"));
}

void testPrimitiveBooleanAndSymmetryCoverage() {
  const auto parsed = signet::ai::parseLogoConstructionPlan(R"JSON({
    "schema_version":1,
    "coordinate_system":{"unit":"logical","origin":"center","x_axis":"right","y_axis":"up","bounds":[-100,-100,100,100]},
    "nodes":[
      {"id":"circle","name":"Circle","kind":"primitive","primitive":{"type":"circle","radius":10},"transform":{"translation":{"x":0,"y":0},"rotation_degrees":0,"scale":{"x":1,"y":1}}},
      {"id":"rectangle","name":"Rectangle","kind":"primitive","primitive":{"type":"rectangle","width":20,"height":20},"transform":{"translation":{"x":30,"y":0},"rotation_degrees":0,"scale":{"x":1,"y":1}}},
      {"id":"golden","name":"Golden","kind":"primitive","primitive":{"type":"golden_rectangle","short_side":10},"transform":{"translation":{"x":-30,"y":0},"rotation_degrees":0,"scale":{"x":1,"y":1}}},
      {"id":"arc","name":"Arc","kind":"primitive","primitive":{"type":"arc","radius":10,"start_degrees":0,"sweep_degrees":360},"transform":{"translation":{"x":0,"y":30},"rotation_degrees":0,"scale":{"x":1,"y":1}}},
      {"id":"sym","name":"Symmetry","kind":"symmetry","input":"circle","axis":{"origin":{"x":0,"y":0},"direction":{"x":1,"y":0}}},
      {"id":"unite","name":"Unite","kind":"boolean","operation":"unite","left":"circle","right":"rectangle"},
      {"id":"intersect","name":"Intersect","kind":"boolean","operation":"intersect","left":"circle","right":"sym"},
      {"id":"subtract","name":"Subtract","kind":"boolean","operation":"subtract","left":"circle","right":"golden"},
      {"id":"xor","name":"Xor","kind":"boolean","operation":"exclusive_or","left":"circle","right":"arc"}],
    "roots":["unite","intersect","subtract","xor","golden"]
  })JSON");
  assert(parsed);
  signet::core::Document base("Coverage");
  const auto compiled = signet::ai::PlanCompiler::preview(*parsed.plan, base);
  assert(compiled);
  assert(base.nodes().empty());
  assert(compiled.document->nodes().size() == parsed.plan->nodes.size());
}

void testAtomicApplyAndRevision() {
  const auto parsed = signet::ai::parseLogoConstructionPlan(validPlan());
  assert(parsed);
  signet::core::DocumentHistory history(signet::core::Document("AI apply"));
  assert(history.revision() == 0);
  const auto applied = signet::ai::PlanCompiler::apply(*parsed.plan, history);
  assert(applied);
  assert(history.document().nodes().size() == 3);
  assert(history.revision() == 1);
  const auto first_id = history.document().nodes().back().id;
  assert(history.undo());
  assert(history.document().nodes().empty());
  assert(history.revision() == 2);
  assert(history.redo());
  assert(history.document().nodes().back().id == first_id);
  assert(history.revision() == 3);
  const auto applied_again = signet::ai::PlanCompiler::apply(*parsed.plan, history);
  assert(applied_again);
  assert(history.document().nodes().size() == 6);
  assert(history.document().nodes().back().id > first_id);
  assert(history.revision() == 4);

  // A rejected plan must not mutate the document, consume IDs, or advance
  // history.  The next successful plan gets the same fresh high-water ID.
  const auto before_failure = history.document();
  const auto before_failure_revision = history.revision();
  auto bad_plan = *parsed.plan;
  auto& bad_primitive = std::get<signet::ai::PlanPrimitive>(bad_plan.nodes.front().definition);
  bad_primitive.transform.scale = {2.0, 1.0};
  const auto rejected = signet::ai::PlanCompiler::apply(bad_plan, history);
  assert(!rejected);
  assert(history.document().nodes().size() == before_failure.nodes().size());
  assert(history.document().nodes().back().id == before_failure.nodes().back().id);
  assert(history.revision() == before_failure_revision);

  const auto applied_after_failure = signet::ai::PlanCompiler::apply(*parsed.plan, history);
  assert(applied_after_failure);
  assert(applied_after_failure.node_ids.front().second > first_id);
}

}  // namespace

int main() {
  testValidParseAndCompile();
  testSchemaAndSemanticRejection();
  testPrimitiveBooleanAndSymmetryCoverage();
  testAtomicApplyAndRevision();
}
