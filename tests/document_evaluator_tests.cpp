// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/document.h"
#include "geometry/document_evaluator.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {

bool close(const double left, const double right) {
  return std::abs(left - right) <= 1.0e-12;
}

void assertCircle(
    const signet::geometry::EvaluatedCircle& evaluated,
    const signet::core::NodeId node_id,
    const double center_x,
    const double center_y,
    const double radius) {
  assert(evaluated.node_id == node_id);
  assert(close(evaluated.circle.center_x, center_x));
  assert(close(evaluated.circle.center_y, center_y));
  assert(close(evaluated.circle.radius, radius));
}

const signet::geometry::EvaluatedCurveSet& curveSet(
    const signet::geometry::DocumentEvaluationSnapshot& snapshot,
    const signet::core::NodeId node_id) {
  for (const auto& set : snapshot.curve_sets) {
    if (set.node_id == node_id) {
      return set;
    }
  }
  assert(false);
  return snapshot.curve_sets.front();
}

const signet::geometry::EvaluatedBoolean& booleanResult(
    const signet::geometry::DocumentEvaluationSnapshot& snapshot,
    const signet::core::NodeId node_id) {
  for (const auto& result : snapshot.booleans) {
    if (result.node_id == node_id) {
      return result;
    }
  }
  assert(false);
  return snapshot.booleans.front();
}

const signet::geometry::EvaluatedSplit& splitResult(
    const signet::geometry::DocumentEvaluationSnapshot& snapshot,
    const signet::core::NodeId node_id) {
  for (const auto& result : snapshot.splits) {
    if (result.node_id == node_id) {
      return result;
    }
  }
  assert(false);
  return snapshot.splits.front();
}

const signet::geometry::EvaluatedRegionSelection& selectionResult(
    const signet::geometry::DocumentEvaluationSnapshot& snapshot,
    const signet::core::NodeId node_id) {
  for (const auto& result : snapshot.region_selections) {
    if (result.node_id == node_id) {
      return result;
    }
  }
  assert(false);
  return snapshot.region_selections.front();
}

const signet::geometry::EvaluatedRegionFilter& filterResult(
    const signet::geometry::DocumentEvaluationSnapshot& snapshot,
    const signet::core::NodeId node_id) {
  for (const auto& result : snapshot.region_filters) {
    if (result.node_id == node_id) {
      return result;
    }
  }
  assert(false);
  return snapshot.region_filters.front();
}

void assertSegment(
    const signet::geometry::SegmentInput& segment,
    const double source_x,
    const double source_y,
    const double target_x,
    const double target_y) {
  assert(close(segment.source_x, source_x));
  assert(close(segment.source_y, source_y));
  assert(close(segment.target_x, target_x));
  assert(close(segment.target_y, target_y));
}

void assertPoint(
    const double x,
    const double y,
    const double expected_x,
    const double expected_y) {
  assert(close(x, expected_x));
  assert(close(y, expected_y));
}

bool hasDiagnostic(
    const signet::geometry::DocumentEvaluationSnapshot& snapshot,
    const signet::core::NodeId node_id,
    const std::string& text) {
  for (const auto& diagnostic : snapshot.diagnostics) {
    if (diagnostic.node_id == node_id && diagnostic.reason.find(text) != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::vector<signet::geometry::EvaluatedCircle> circles(
    const signet::geometry::DocumentEvaluationSnapshot& snapshot) {
  return snapshot.circles;
}

enum class MatrixPrimitive {
  circle,
  rectangle,
  golden_rectangle,
  full_circle_arc,
};

signet::core::Primitive matrixPrimitive(const MatrixPrimitive primitive) {
  using namespace signet::core;
  switch (primitive) {
    case MatrixPrimitive::circle:
      return Circle{4.0};
    case MatrixPrimitive::rectangle:
      return Rectangle{8.0, 6.0};
    case MatrixPrimitive::golden_rectangle:
      return GoldenRectangle{3.0};
    case MatrixPrimitive::full_circle_arc:
      return Arc{4.0, 37.0, -360.0};
  }
  return Circle{1.0};
}

void assertBooleanSnapshotsEqual(
    const signet::geometry::DocumentEvaluationSnapshot& left,
    const signet::geometry::DocumentEvaluationSnapshot& right) {
  assert(left.booleans.size() == right.booleans.size());
  for (const auto& left_result : left.booleans) {
    const auto& right_result = booleanResult(right, left_result.node_id);
    assert(left_result.operation == right_result.operation);
    assert(left_result.regions == right_result.regions);
  }
}

void assertBooleanRegionsHealthy(
    const signet::geometry::EvaluatedBoolean& result,
    const std::size_t operand_count,
    const std::size_t source_curve_count) {
  for (const auto& region : result.regions) {
    assert(region.bounded);
    assert(region.selected);
    assert(region.face_id != 0);
    assert(region.operand_membership.size() == operand_count);
    assert(region.outer_boundary.closed);
    assert(!region.outer_boundary.edges.empty());
    for (const auto& edge : region.outer_boundary.edges) {
      for (const auto source_index : edge.source_curve_indices) {
        assert(source_index < source_curve_count);
      }
    }
    for (const auto& hole : region.holes) {
      assert(hole.closed);
      assert(!hole.edges.empty());
    }
  }
}

template <typename Predicate>
void assertBooleanMembership(
    const signet::geometry::EvaluatedBoolean& result,
    const std::size_t operand_count,
    Predicate predicate) {
  assert(!result.regions.empty());
  for (const auto& region : result.regions) {
    assert(region.operand_membership.size() == operand_count);
    assert(predicate(region.operand_membership));
  }
}

}  // namespace

int main() {
  using namespace signet::core;
  using signet::geometry::DocumentEvaluator;

  Document document("Evaluation");
  const NodeId first = document.addPrimitive("First", Circle{2.0}, Transform{Point{3.0, 4.0}});
  const NodeId second = document.addPrimitive("Second", Circle{5.0}, Transform{Point{-6.0, 7.0}});

  const auto initial = DocumentEvaluator::evaluate(document);
  assert(initial.circles.size() == 2);
  assert(initial.diagnostics.empty());
  assertCircle(initial.circles[0], first, 3.0, 4.0, 2.0);
  assertCircle(initial.circles[1], second, -6.0, 7.0, 5.0);

  const Transform transformed{Point{10.0, -8.0}, 137.0, Point{-3.0, 3.0}};
  assert(document.setTransform(first, transformed));
  const auto transformed_snapshot = DocumentEvaluator::evaluate(document);
  assert(transformed_snapshot.circles.size() == 2);
  assertCircle(transformed_snapshot.circles[0], first, 10.0, -8.0, 6.0);
  assertCircle(transformed_snapshot.circles[1], second, -6.0, 7.0, 5.0);

  assert(document.setTransform(second, Transform{Point{-2.0, 1.0}, -22.0, Point{2.0, 2.0}}));
  const auto positive_scale_snapshot = DocumentEvaluator::evaluate(document);
  assertCircle(positive_scale_snapshot.circles[1], second, -2.0, 1.0, 10.0);

  const NodeId non_uniform = document.addPrimitive(
      "Ellipse candidate", Circle{4.0}, Transform{Point{}, 0.0, Point{-2.0, 3.0}});
  const NodeId rectangle = document.addPrimitive("Rectangle", Rectangle{8.0, 6.0});
  const NodeId arc = document.addPrimitive("Arc", Arc{4.0, 30.0, 90.0});
  const NodeId boolean = document.addBoolean("Union", BooleanOperation::unite, first, second);
  const NodeId nested_boolean = document.addBoolean("Nested", BooleanOperation::intersect, boolean, first);
  const NodeId non_circle_boolean = document.addBoolean("Rectangle union", BooleanOperation::unite, rectangle, first);
  const NodeId reflection = document.addSymmetry("Reflection", first, Point{}, Point{1.0, 0.0});
  const NodeId reflection_boolean =
      document.addSymmetry("Boolean reflection", boolean, Point{}, Point{1.0, 0.0});
  const NodeId split = document.addSplit("Split", first, Point{}, Point{0.0, 1.0});
  const NodeId selection = document.addRegionSelection(
      "Selection",
      split,
      {RegionKey{
          split,
          {RegionExpressionTerm{RegionExpressionTerm::Kind::leaf, first}},
          RegionCutterSide::negative,
          {}}});
  const NodeId reflection_split =
      document.addSymmetry("Split reflection", split, Point{}, Point{1.0, 0.0});
  const NodeId reflection_selection =
      document.addSymmetry("Selection reflection", selection, Point{}, Point{1.0, 0.0});
  const auto unsupported = DocumentEvaluator::evaluate(document);
  assert(unsupported.circles.size() == 3);
  assertCircle(unsupported.circles[2], reflection, 10.0, 8.0, 6.0);
  assert(unsupported.booleans.size() == 3);
  assert(booleanResult(unsupported, boolean).regions.size() == 3);
  assert(!booleanResult(unsupported, nested_boolean).regions.empty());
  assert(booleanResult(unsupported, non_circle_boolean).regions.size() == 2);
  assert(hasDiagnostic(unsupported, non_uniform, "non-uniform"));
  assert(unsupported.curve_sets.size() == 2);
  assert(curveSet(unsupported, rectangle).curves.size() == 4);
  assert(std::holds_alternative<signet::geometry::SegmentInput>(
      curveSet(unsupported, rectangle).curves.front()));
  assert(std::holds_alternative<signet::geometry::ArcInput>(
      curveSet(unsupported, arc).curves.front()));
  assert(!hasDiagnostic(unsupported, rectangle, "Rectangle"));
  assert(!hasDiagnostic(unsupported, arc, "Arc"));
  assert(!hasDiagnostic(unsupported, boolean, "requires direct"));
  assert(!hasDiagnostic(unsupported, nested_boolean, "nested Boolean"));
  assert(!hasDiagnostic(unsupported, non_circle_boolean, "direct Circle"));
  assert(!hasDiagnostic(unsupported, reflection, "Reflection"));
  assert(hasDiagnostic(unsupported, reflection_boolean, "Boolean"));
  assert(unsupported.splits.size() == 1);
  assert(splitResult(unsupported, split).status == signet::geometry::SplitStatus::nonintersection);
  assert(splitResult(unsupported, split).cells.size() == 1);
  assert(splitResult(unsupported, split).cells.front().key.split_node_id == split);
  assert(hasDiagnostic(unsupported, split, "does not intersect"));
  assert(hasDiagnostic(unsupported, selection, "Region selection"));
  assert(hasDiagnostic(unsupported, reflection_split, "Split"));
  assert(hasDiagnostic(unsupported, reflection_selection, "Region selection"));

  const auto before = circles(unsupported);
  const auto before_curve_sets = unsupported.curve_sets;
  const auto before_nodes = document.nodes();
  static_cast<void>(DocumentEvaluator::evaluate(document));
  assert(circles(DocumentEvaluator::evaluate(document)) == before);
  assert(DocumentEvaluator::evaluate(document).curve_sets == before_curve_sets);
  assert(document.nodes().size() == before_nodes.size());
  for (std::size_t index = 0; index < before_nodes.size(); ++index) {
    assert(document.nodes()[index].id == before_nodes[index].id);
    assert(document.nodes()[index].name == before_nodes[index].name);
    assert(document.nodes()[index].visible == before_nodes[index].visible);
    assert(document.nodes()[index].definition.index() == before_nodes[index].definition.index());
  }

  bool rejected_invalid_axis = false;
  try {
    Document invalid_axis("Invalid reflection axis");
    const NodeId invalid_axis_source = invalid_axis.addPrimitive("Source", Circle{1.0});
    static_cast<void>(invalid_axis.addSymmetry(
        "Invalid", invalid_axis_source, Point{}, Point{0.0, 0.0}));
  } catch (const std::invalid_argument&) {
    rejected_invalid_axis = true;
  }
  assert(rejected_invalid_axis);

  Document reflections("Reflection evaluation");
  const NodeId reflection_circle = reflections.addPrimitive(
      "Circle", Circle{2.0}, Transform{Point{4.0, 3.0}});
  const NodeId horizontal = reflections.addSymmetry(
      "Horizontal offset", reflection_circle, Point{0.0, 1.0}, Point{1.0, 0.0});
  const NodeId vertical = reflections.addSymmetry(
      "Vertical offset", reflection_circle, Point{2.0, 0.0}, Point{0.0, 1.0});
  const NodeId diagonal = reflections.addSymmetry(
      "Diagonal", reflection_circle, Point{1.0, 1.0}, Point{1.0, 1.0});
  const NodeId twice = reflections.addSymmetry(
      "Double reflection", horizontal, Point{0.0, 1.0}, Point{1.0, 0.0});
  const auto circle_reflections = DocumentEvaluator::evaluate(reflections);
  assert(circle_reflections.circles.size() == 5);
  assertCircle(circle_reflections.circles[0], reflection_circle, 4.0, 3.0, 2.0);
  assertCircle(circle_reflections.circles[1], horizontal, 4.0, -1.0, 2.0);
  assertCircle(circle_reflections.circles[2], vertical, 0.0, 3.0, 2.0);
  assertCircle(circle_reflections.circles[3], diagonal, 3.0, 4.0, 2.0);
  assertCircle(circle_reflections.circles[4], twice, 4.0, 3.0, 2.0);
  assert(circle_reflections.diagnostics.empty());

  Document reflected_primitives("Reflected primitive evaluation");
  const NodeId source_rectangle = reflected_primitives.addPrimitive(
      "Rectangle", Rectangle{8.0, 6.0});
  const NodeId reflected_rectangle_node = reflected_primitives.addSymmetry(
      "Rectangle reflection", source_rectangle, Point{2.0, 0.0}, Point{0.0, 1.0});
  const NodeId source_golden = reflected_primitives.addPrimitive(
      "Golden", GoldenRectangle{2.0});
  const NodeId reflected_golden = reflected_primitives.addSymmetry(
      "Golden reflection", source_golden, Point{}, Point{1.0, 1.0});
  const NodeId source_positive_arc = reflected_primitives.addPrimitive(
      "Positive arc", Arc{10.0, 0.0, 90.0});
  const NodeId reflected_positive_arc = reflected_primitives.addSymmetry(
      "Positive arc reflection", source_positive_arc, Point{}, Point{1.0, 0.0});
  const NodeId source_negative_arc = reflected_primitives.addPrimitive(
      "Negative arc", Arc{10.0, 0.0, -90.0});
  const NodeId reflected_negative_arc = reflected_primitives.addSymmetry(
      "Negative arc reflection", source_negative_arc, Point{}, Point{1.0, 0.0});
  const NodeId source_full_arc = reflected_primitives.addPrimitive(
      "Full arc", Arc{10.0, 37.0, -360.0});
  const NodeId reflected_full_arc = reflected_primitives.addSymmetry(
      "Full arc reflection", source_full_arc, Point{3.0, 4.0}, Point{1.0, 0.0});

  const auto primitive_reflections = DocumentEvaluator::evaluate(reflected_primitives);
  assert(primitive_reflections.diagnostics.empty());
  assert(curveSet(primitive_reflections, source_rectangle).node_id == source_rectangle);
  const auto& reflected_rectangle_curves =
      curveSet(primitive_reflections, reflected_rectangle_node).curves;
  assert(reflected_rectangle_curves.size() == 4);
  assertSegment(
      std::get<signet::geometry::SegmentInput>(reflected_rectangle_curves[0]),
      8.0,
      -3.0,
      0.0,
      -3.0);
  assert(curveSet(primitive_reflections, source_golden).curves.size() == 4);
  assert(curveSet(primitive_reflections, reflected_golden).curves.size() == 4);

  const auto& reflected_positive = std::get<signet::geometry::ArcInput>(
      curveSet(primitive_reflections, reflected_positive_arc).curves.front());
  assertPoint(reflected_positive.source_x, reflected_positive.source_y, 10.0, 0.0);
  assertPoint(reflected_positive.interior_x, reflected_positive.interior_y, std::sqrt(50.0), -std::sqrt(50.0));
  assertPoint(reflected_positive.target_x, reflected_positive.target_y, 0.0, -10.0);
  const auto& reflected_negative = std::get<signet::geometry::ArcInput>(
      curveSet(primitive_reflections, reflected_negative_arc).curves.front());
  assertPoint(reflected_negative.interior_x, reflected_negative.interior_y, std::sqrt(50.0), std::sqrt(50.0));
  assertPoint(reflected_negative.target_x, reflected_negative.target_y, 0.0, 10.0);
  const auto& reflected_full = curveSet(primitive_reflections, reflected_full_arc).curves;
  assert(reflected_full.size() == 1);
  assert(std::get<signet::geometry::CircleInput>(reflected_full.front()) ==
         (signet::geometry::CircleInput{0.0, 8.0, 10.0}));

  DocumentHistory reflection_history(Document("Reflection history"));
  const NodeId reflection_history_source = reflection_history.addPrimitive(
      "Source", Circle{3.0}, Transform{Point{2.0, 1.0}});
  const NodeId reflection_history_node = reflection_history.addSymmetry(
      "Reflection", reflection_history_source, Point{}, Point{0.0, 1.0});
  const auto reflection_history_initial =
      DocumentEvaluator::evaluate(reflection_history.document());
  assertCircle(
      reflection_history_initial.circles[0], reflection_history_source, 2.0, 1.0, 3.0);
  assertCircle(
      reflection_history_initial.circles[1], reflection_history_node, -2.0, 1.0, 3.0);
  assert(reflection_history.setTransform(
      reflection_history_source, Transform{Point{4.0, 5.0}, 27.0, Point{2.0, 2.0}}));
  const auto reflection_history_transformed =
      DocumentEvaluator::evaluate(reflection_history.document());
  assertCircle(
      reflection_history_transformed.circles[0], reflection_history_source, 4.0, 5.0, 6.0);
  assertCircle(
      reflection_history_transformed.circles[1], reflection_history_node, -4.0, 5.0, 6.0);
  assert(reflection_history.undo());
  const auto reflection_history_undo =
      DocumentEvaluator::evaluate(reflection_history.document());
  assert(reflection_history_undo.circles == reflection_history_initial.circles);
  assert(reflection_history.redo());
  const auto reflection_history_redo =
      DocumentEvaluator::evaluate(reflection_history.document());
  assert(reflection_history_redo.circles == reflection_history_transformed.circles);

  DocumentHistory history(Document("History evaluation"));
  const NodeId history_circle = history.addPrimitive("Circle", Circle{3.0});
  const NodeId history_golden = history.addPrimitive("Golden", GoldenRectangle{2.0});
  const auto history_initial = DocumentEvaluator::evaluate(history.document());
  assert(history.setTransform(history_circle, Transform{Point{8.0, 9.0}, 45.0, Point{2.0, 2.0}}));
  const auto history_transformed = DocumentEvaluator::evaluate(history.document());
  assert(history.undo());
  const auto history_undo = DocumentEvaluator::evaluate(history.document());
  assert(history.redo());
  const auto history_redo = DocumentEvaluator::evaluate(history.document());
  assert(history_initial.circles.size() == 1);
  assert(history_initial.curve_sets.size() == 1);
  assert(curveSet(history_initial, history_golden).node_id == history_golden);
  assert(history_undo.circles == history_initial.circles);
  assert(history_undo.curve_sets == history_initial.curve_sets);
  assert(history_redo.circles == history_transformed.circles);
  assert(history_redo.curve_sets == history_transformed.curve_sets);

  DocumentHistory boolean_history(Document("Boolean history"));
  const NodeId left = boolean_history.addPrimitive(
      "Left", Circle{10.0}, Transform{Point{-5.0, 0.0}});
  const NodeId right = boolean_history.addPrimitive(
      "Right", Circle{10.0}, Transform{Point{5.0, 0.0}});
  const NodeId boolean_result = boolean_history.addBoolean(
      "Intersection", BooleanOperation::intersect, left, right);
  const auto boolean_initial = DocumentEvaluator::evaluate(boolean_history.document());
  assert(boolean_initial.booleans.size() == 1);
  assert(boolean_initial.booleans.front().node_id == boolean_result);
  assert(boolean_initial.booleans.front().regions.size() == 1);

  assert(boolean_history.setTransform(
      right, Transform{Point{30.0, 0.0}, 0.0, Point{1.0, 1.0}}));
  const auto boolean_moved = DocumentEvaluator::evaluate(boolean_history.document());
  assert(boolean_moved.booleans.size() == 1);
  assert(boolean_moved.booleans.front().regions.empty());
  assert(boolean_history.undo());
  const auto boolean_undo = DocumentEvaluator::evaluate(boolean_history.document());
  assert(boolean_undo.booleans.front().regions.size() == 1);
  assert(boolean_history.redo());
  const auto boolean_redo = DocumentEvaluator::evaluate(boolean_history.document());
  assert(boolean_redo.booleans.front().regions.empty());

  Document primitives("Primitive evaluation");
  const NodeId identity_rectangle = primitives.addPrimitive("Identity", Rectangle{8.0, 6.0});
  const NodeId translated_rectangle = primitives.addPrimitive(
      "Translated", Rectangle{8.0, 6.0}, Transform{Point{10.0, 20.0}});
  const NodeId rotated_rectangle = primitives.addPrimitive(
      "Rotated", Rectangle{8.0, 6.0}, Transform{Point{}, 90.0});
  const NodeId nonuniform_rectangle = primitives.addPrimitive(
      "Nonuniform", Rectangle{8.0, 6.0}, Transform{Point{}, 0.0, Point{2.0, 3.0}});
  const NodeId reflected_rectangle = primitives.addPrimitive(
      "Reflected", Rectangle{8.0, 6.0}, Transform{Point{}, 0.0, Point{-2.0, 1.0}});
  const NodeId golden_rectangle = primitives.addPrimitive("Golden", GoldenRectangle{2.0});
  const NodeId positive_arc = primitives.addPrimitive("Positive arc", Arc{10.0, 0.0, 90.0});
  const NodeId negative_arc = primitives.addPrimitive("Negative arc", Arc{10.0, 0.0, -90.0});
  const NodeId transformed_arc = primitives.addPrimitive(
      "Transformed arc", Arc{10.0, 0.0, 90.0}, Transform{Point{5.0, -3.0}, 90.0, Point{2.0, 2.0}});
  const NodeId reflected_arc = primitives.addPrimitive(
      "Reflected arc", Arc{10.0, 0.0, 90.0}, Transform{Point{}, 0.0, Point{-1.0, 1.0}});
  const NodeId nonuniform_arc = primitives.addPrimitive(
      "Nonuniform arc", Arc{10.0, 0.0, 90.0}, Transform{Point{}, 0.0, Point{2.0, 3.0}});
  const NodeId full_arc = primitives.addPrimitive("Full arc", Arc{10.0, 37.0, -360.0});

  const auto evaluated = DocumentEvaluator::evaluate(primitives);
  assert(evaluated.diagnostics.size() == 1);
  assert(hasDiagnostic(evaluated, nonuniform_arc, "non-uniform"));
  assert(curveSet(evaluated, identity_rectangle).curves.size() == 4);
  assert(curveSet(evaluated, identity_rectangle).node_id == identity_rectangle);
  const auto& identity_curves = curveSet(evaluated, identity_rectangle).curves;
  assertSegment(std::get<signet::geometry::SegmentInput>(identity_curves[0]), -4.0, -3.0, 4.0, -3.0);
  assertSegment(std::get<signet::geometry::SegmentInput>(identity_curves[1]), 4.0, -3.0, 4.0, 3.0);
  assertSegment(std::get<signet::geometry::SegmentInput>(identity_curves[2]), 4.0, 3.0, -4.0, 3.0);
  assertSegment(std::get<signet::geometry::SegmentInput>(identity_curves[3]), -4.0, 3.0, -4.0, -3.0);
  assertSegment(
      std::get<signet::geometry::SegmentInput>(curveSet(evaluated, translated_rectangle).curves[0]),
      6.0, 17.0, 14.0, 17.0);
  assertSegment(
      std::get<signet::geometry::SegmentInput>(curveSet(evaluated, rotated_rectangle).curves[0]),
      3.0, -4.0, 3.0, 4.0);
  assertSegment(
      std::get<signet::geometry::SegmentInput>(curveSet(evaluated, nonuniform_rectangle).curves[0]),
      -8.0, -9.0, 8.0, -9.0);
  assertSegment(
      std::get<signet::geometry::SegmentInput>(curveSet(evaluated, reflected_rectangle).curves[0]),
      8.0, -3.0, -8.0, -3.0);

  const auto& golden_curves = curveSet(evaluated, golden_rectangle).curves;
  assert(golden_curves.size() == 4);
  const double phi = (1.0 + std::sqrt(5.0)) / 2.0;
  assert(close(std::abs(std::get<signet::geometry::SegmentInput>(golden_curves[0]).target_x -
                         std::get<signet::geometry::SegmentInput>(golden_curves[0]).source_x),
               2.0 * phi));
  assert(close(std::abs(std::get<signet::geometry::SegmentInput>(golden_curves[1]).target_y -
                         std::get<signet::geometry::SegmentInput>(golden_curves[1]).source_y),
               2.0));

  const auto& positive = std::get<signet::geometry::ArcInput>(curveSet(evaluated, positive_arc).curves[0]);
  assertPoint(positive.source_x, positive.source_y, 10.0, 0.0);
  assertPoint(positive.interior_x, positive.interior_y, std::sqrt(50.0), std::sqrt(50.0));
  assertPoint(positive.target_x, positive.target_y, 0.0, 10.0);
  const auto& negative = std::get<signet::geometry::ArcInput>(curveSet(evaluated, negative_arc).curves[0]);
  assertPoint(negative.interior_x, negative.interior_y, std::sqrt(50.0), -std::sqrt(50.0));
  assertPoint(negative.target_x, negative.target_y, 0.0, -10.0);
  const auto& transformed_arc_input = std::get<signet::geometry::ArcInput>(
      curveSet(evaluated, transformed_arc).curves[0]);
  assertPoint(transformed_arc_input.source_x, transformed_arc_input.source_y, 5.0, 17.0);
  assertPoint(transformed_arc_input.target_x, transformed_arc_input.target_y, -15.0, -3.0);
  const auto& reflected = std::get<signet::geometry::ArcInput>(
      curveSet(evaluated, reflected_arc).curves[0]);
  assertPoint(reflected.source_x, reflected.source_y, -10.0, 0.0);
  assertPoint(reflected.interior_x, reflected.interior_y, -std::sqrt(50.0), std::sqrt(50.0));
  assertPoint(reflected.target_x, reflected.target_y, 0.0, 10.0);
  const auto& full = curveSet(evaluated, full_arc).curves;
  assert(full.size() == 1);
  assert(std::holds_alternative<signet::geometry::CircleInput>(full.front()));
  assert(std::get<signet::geometry::CircleInput>(full.front()) ==
         (signet::geometry::CircleInput{0.0, 0.0, 10.0}));

  // Direct Document Boolean coverage: every primitive kind is paired with
  // every other kind, including a full-circle Arc. The evaluator must retain
  // all four operation nodes without diagnostics; detailed boundary topology
  // and spatial edge cases are covered by ArrangementModel's general matrix.
  const std::array<MatrixPrimitive, 4> matrix_primitives{
      MatrixPrimitive::circle,
      MatrixPrimitive::rectangle,
      MatrixPrimitive::golden_rectangle,
      MatrixPrimitive::full_circle_arc};
  const std::array<BooleanOperation, 4> matrix_operations{
      BooleanOperation::unite,
      BooleanOperation::intersect,
      BooleanOperation::subtract,
      BooleanOperation::exclusive_or};
  for (const MatrixPrimitive left_primitive : matrix_primitives) {
    for (const MatrixPrimitive right_primitive : matrix_primitives) {
      Document matrix_document("Direct Boolean matrix");
      const NodeId left_node = matrix_document.addPrimitive(
          "Left", matrixPrimitive(left_primitive));
      const NodeId right_node = matrix_document.addPrimitive(
          "Right", matrixPrimitive(right_primitive),
          Transform{Point{0.5, 0.0}, 0.0, Point{1.0, 1.0}});
      std::array<NodeId, 4> boolean_nodes{};
      for (std::size_t index = 0; index < matrix_operations.size(); ++index) {
        boolean_nodes[index] = matrix_document.addBoolean(
            "Direct Boolean", matrix_operations[index], left_node, right_node);
      }
      const auto first_matrix_snapshot = DocumentEvaluator::evaluate(matrix_document);
      assert(first_matrix_snapshot.diagnostics.empty());
      assert(first_matrix_snapshot.booleans.size() == matrix_operations.size());
      for (std::size_t index = 0; index < boolean_nodes.size(); ++index) {
        const auto& result = booleanResult(first_matrix_snapshot, boolean_nodes[index]);
        assert(result.operation == matrix_operations[index]);
        for (const auto& region : result.regions) {
          assert(region.bounded);
          assert(region.selected);
          assert(region.face_id != 0);
          assert(region.outer_boundary.closed);
          assert(!region.outer_boundary.edges.empty());
        }
      }
      const auto second_matrix_snapshot = DocumentEvaluator::evaluate(matrix_document);
      assertBooleanSnapshotsEqual(first_matrix_snapshot, second_matrix_snapshot);
    }
  }

  // Boolean inputs retain their transformed evaluated curves: translation,
  // rotation, rectangle non-uniform scale, and negative-scale reflection all
  // participate in direct Boolean evaluation.
  Document transformed_booleans("Transformed Boolean inputs");
  const NodeId transformed_rectangle = transformed_booleans.addPrimitive(
      "Non-uniform rectangle", Rectangle{8.0, 6.0},
      Transform{Point{1.0, 0.0}, 30.0, Point{2.0, 3.0}});
  const NodeId transformed_rotated_rectangle = transformed_booleans.addPrimitive(
      "Rotated rectangle", Rectangle{8.0, 6.0},
      Transform{Point{1.0, 0.0}, 37.0, Point{1.0, 1.0}});
  const NodeId reflected_rectangle_for_boolean = transformed_booleans.addPrimitive(
      "Reflected rectangle", Rectangle{8.0, 6.0},
      Transform{Point{1.0, 0.0}, -17.0, Point{-1.0, 1.0}});
  const NodeId translated_arc = transformed_booleans.addPrimitive(
      "Translated full arc", Arc{4.0, 11.0, 360.0},
      Transform{Point{1.0, 0.0}, 91.0, Point{-1.0, -1.0}});
  const NodeId transformed_union = transformed_booleans.addBoolean(
      "Transformed union", BooleanOperation::unite,
      transformed_rectangle, translated_arc);
  const NodeId transformed_intersection = transformed_booleans.addBoolean(
      "Transformed intersection", BooleanOperation::intersect,
      transformed_rotated_rectangle, translated_arc);
  const NodeId transformed_subtract = transformed_booleans.addBoolean(
      "Transformed subtract", BooleanOperation::subtract,
      reflected_rectangle_for_boolean, translated_arc);
  const NodeId transformed_xor = transformed_booleans.addBoolean(
      "Transformed xor", BooleanOperation::exclusive_or,
      transformed_rectangle, translated_arc);
  const auto transformed_boolean_snapshot = DocumentEvaluator::evaluate(transformed_booleans);
  assert(transformed_boolean_snapshot.diagnostics.empty());
  assert(booleanResult(transformed_boolean_snapshot, transformed_union).operation ==
         BooleanOperation::unite);
  assert(booleanResult(transformed_boolean_snapshot, transformed_intersection).operation ==
         BooleanOperation::intersect);
  assert(booleanResult(transformed_boolean_snapshot, transformed_subtract).operation ==
         BooleanOperation::subtract);
  assert(booleanResult(transformed_boolean_snapshot, transformed_xor).operation ==
         BooleanOperation::exclusive_or);

  // Re-evaluation after a transform edit must also survive History undo/redo.
  DocumentHistory transformed_history(Document("Transformed Boolean history"));
  const NodeId history_rectangle = transformed_history.addPrimitive(
      "Rectangle", Rectangle{8.0, 6.0},
      Transform{Point{}, 23.0, Point{2.0, 3.0}});
  const NodeId history_arc = transformed_history.addPrimitive(
      "Full arc", Arc{4.0, 0.0, 360.0});
  const NodeId history_boolean = transformed_history.addBoolean(
      "Intersection", BooleanOperation::intersect, history_rectangle, history_arc);
  const auto transformed_history_initial =
      DocumentEvaluator::evaluate(transformed_history.document());
  assert(booleanResult(transformed_history_initial, history_boolean).regions.size() == 1);
  assert(transformed_history.setTransform(
      history_arc, Transform{Point{30.0, 0.0}, 0.0, Point{-1.0, -1.0}}));
  const auto transformed_history_moved =
      DocumentEvaluator::evaluate(transformed_history.document());
  assert(booleanResult(transformed_history_moved, history_boolean).regions.empty());
  assert(transformed_history.undo());
  const auto transformed_history_undo =
      DocumentEvaluator::evaluate(transformed_history.document());
  assertBooleanSnapshotsEqual(transformed_history_undo, transformed_history_initial);
  assert(transformed_history.redo());
  const auto transformed_history_redo =
      DocumentEvaluator::evaluate(transformed_history.document());
  assertBooleanSnapshotsEqual(transformed_history_redo, transformed_history_moved);

  // Stage 2 expands every Boolean root to primitive/Symmetry leaves.  The
  // same leaf may occur through several branches, but it is one Arrangement
  // operand group and one membership bit for that root evaluation.
  Document nested("Nested Boolean DAG");
  const NodeId nested_a = nested.addPrimitive(
      "A", Circle{10.0}, Transform{Point{-25.0, 0.0}});
  const NodeId nested_b = nested.addPrimitive(
      "B", Circle{10.0}, Transform{Point{-15.0, 0.0}});
  const NodeId nested_c = nested.addPrimitive(
      "C", Circle{10.0}, Transform{Point{25.0, 0.0}});
  const NodeId nested_d = nested.addPrimitive(
      "D", Circle{10.0}, Transform{Point{30.0, 0.0}});
  const NodeId nested_ab_union = nested.addBoolean(
      "A union B", BooleanOperation::unite, nested_a, nested_b);
  const NodeId nested_b_xor_c = nested.addBoolean(
      "B xor C", BooleanOperation::exclusive_or, nested_b, nested_c);
  const NodeId nested_formula_one = nested.addBoolean(
      "(A union B) subtract C", BooleanOperation::subtract,
      nested_ab_union, nested_c);
  const NodeId nested_cd_intersection = nested.addBoolean(
      "C intersection D", BooleanOperation::intersect, nested_c, nested_d);
  const NodeId nested_a_subtract_b = nested.addBoolean(
      "A subtract B", BooleanOperation::subtract, nested_a, nested_b);
  const NodeId nested_formula_two = nested.addBoolean(
      "(A subtract B) union (C intersection D)", BooleanOperation::unite,
      nested_a_subtract_b, nested_cd_intersection);
  const NodeId nested_formula_three = nested.addBoolean(
      "A intersection (B xor C)", BooleanOperation::intersect,
      nested_a, nested_b_xor_c);
  const NodeId nested_deep = nested.addBoolean(
      "Deep nested", BooleanOperation::unite,
      nested_formula_one, nested_formula_two);
  const NodeId shared_subtree = nested.addBoolean(
      "Shared subtree", BooleanOperation::unite, nested_a, nested_b);
  const NodeId shared_branch = nested.addBoolean(
      "Shared branch", BooleanOperation::unite, shared_subtree, nested_c);
  const NodeId shared_root = nested.addBoolean(
      "Shared root", BooleanOperation::intersect, shared_subtree, shared_branch);

  const auto nested_snapshot = DocumentEvaluator::evaluate(nested);
  assert(nested_snapshot.diagnostics.empty());
  assert(nested_snapshot.booleans.size() == 11);
  assertBooleanMembership(
      booleanResult(nested_snapshot, nested_formula_one), 3,
      [](const std::vector<bool>& membership) {
        return (membership[0] || membership[1]) && !membership[2];
      });
  assertBooleanMembership(
      booleanResult(nested_snapshot, nested_formula_three), 3,
      [](const std::vector<bool>& membership) {
        return membership[0] && (membership[1] != membership[2]);
      });
  assertBooleanMembership(
      booleanResult(nested_snapshot, nested_formula_two), 4,
      [](const std::vector<bool>& membership) {
        return (membership[0] && !membership[1]) ||
               (membership[2] && membership[3]);
      });
  assertBooleanMembership(
      booleanResult(nested_snapshot, nested_deep), 4,
      [](const std::vector<bool>& membership) {
        const bool first = (membership[0] || membership[1]) && !membership[2];
        const bool second = (membership[0] && !membership[1]) ||
                            (membership[2] && membership[3]);
        return first || second;
      });
  assertBooleanMembership(
      booleanResult(nested_snapshot, shared_root), 3,
      [](const std::vector<bool>& membership) {
        return membership[0] || membership[1];
      });
  assertBooleanRegionsHealthy(
      booleanResult(nested_snapshot, nested_formula_one), 3, 3);
  assertBooleanRegionsHealthy(
      booleanResult(nested_snapshot, nested_formula_three), 3, 3);
  assertBooleanRegionsHealthy(
      booleanResult(nested_snapshot, nested_formula_two), 4, 4);
  assertBooleanRegionsHealthy(
      booleanResult(nested_snapshot, nested_deep), 4, 4);
  assertBooleanRegionsHealthy(booleanResult(nested_snapshot, shared_root), 3, 3);
  const auto nested_repeat = DocumentEvaluator::evaluate(nested);
  assertBooleanSnapshotsEqual(nested_snapshot, nested_repeat);

  // Nested evaluation also accepts all currently supported closed leaf
  // shapes, including a full-circle Arc and a reflected closed curve.
  Document mixed_nested("Mixed nested Boolean DAG");
  const NodeId mixed_circle = mixed_nested.addPrimitive("Circle", Circle{10.0});
  const NodeId mixed_rectangle = mixed_nested.addPrimitive(
      "Rectangle", Rectangle{12.0, 12.0}, Transform{Point{1.0, 0.0}, 17.0});
  const NodeId mixed_golden = mixed_nested.addPrimitive(
      "Golden", GoldenRectangle{6.0}, Transform{Point{-1.0, 0.0}});
  const NodeId mixed_arc = mixed_nested.addPrimitive(
      "Full circle", Arc{5.0, 23.0, -360.0});
  const NodeId mixed_reflection = mixed_nested.addSymmetry(
      "Reflected rectangle", mixed_rectangle, Point{}, Point{0.0, 1.0});
  const NodeId mixed_left = mixed_nested.addBoolean(
      "Circle union rectangle", BooleanOperation::unite,
      mixed_circle, mixed_reflection);
  const NodeId mixed_right = mixed_nested.addBoolean(
      "Golden intersection full circle", BooleanOperation::intersect,
      mixed_golden, mixed_arc);
  const NodeId mixed_root = mixed_nested.addBoolean(
      "Mixed subtract", BooleanOperation::subtract, mixed_left, mixed_right);
  const auto mixed_snapshot = DocumentEvaluator::evaluate(mixed_nested);
  assert(mixed_snapshot.diagnostics.empty());
  assert(mixed_snapshot.booleans.size() == 3);
  assert(booleanResult(mixed_snapshot, mixed_left).regions.size() >= 1);
  assert(booleanResult(mixed_snapshot, mixed_right).regions.size() >= 1);
  assertBooleanRegionsHealthy(booleanResult(mixed_snapshot, mixed_root), 4, 10);

  // Transform edits and History re-evaluation preserve child and parent
  // snapshots, including exact DTO/FaceId determinism after undo and redo.
  DocumentHistory nested_history(Document("Nested Boolean history"));
  const NodeId history_a = nested_history.addPrimitive(
      "A", Circle{10.0}, Transform{Point{-5.0, 0.0}});
  const NodeId history_b = nested_history.addPrimitive(
      "B", Circle{10.0}, Transform{Point{5.0, 0.0}});
  const NodeId history_c = nested_history.addPrimitive(
      "C", Circle{3.0}, Transform{Point{30.0, 0.0}});
  const NodeId history_child = nested_history.addBoolean(
      "A union B", BooleanOperation::unite, history_a, history_b);
  const NodeId history_parent = nested_history.addBoolean(
      "Subtract C", BooleanOperation::subtract, history_child, history_c);
  const auto nested_history_initial =
      DocumentEvaluator::evaluate(nested_history.document());
  assert(nested_history_initial.booleans.size() == 2);
  assert(!booleanResult(nested_history_initial, history_child).regions.empty());
  assert(!booleanResult(nested_history_initial, history_parent).regions.empty());
  assert(nested_history.setTransform(history_c, Transform{Point{}, 0.0, Point{1.0, 1.0}}));
  const auto nested_history_moved =
      DocumentEvaluator::evaluate(nested_history.document());
  assert(nested_history_moved.booleans.size() == 2);
  assert(booleanResult(nested_history_moved, history_parent).regions !=
         booleanResult(nested_history_initial, history_parent).regions);
  assert(nested_history.undo());
  const auto nested_history_undo =
      DocumentEvaluator::evaluate(nested_history.document());
  assertBooleanSnapshotsEqual(nested_history_undo, nested_history_initial);
  assert(nested_history.redo());
  const auto nested_history_redo =
      DocumentEvaluator::evaluate(nested_history.document());
  assertBooleanSnapshotsEqual(nested_history_redo, nested_history_moved);

  // An unsupported leaf diagnoses only the Boolean that consumes it. Other
  // independent nodes still produce their normal snapshots.
  Document invalid_nested("Invalid nested Boolean leaves");
  const NodeId invalid_circle = invalid_nested.addPrimitive("Circle", Circle{4.0});
  const NodeId invalid_open_arc = invalid_nested.addPrimitive(
      "Open arc", Arc{4.0, 0.0, 90.0});
  const NodeId invalid_other_circle = invalid_nested.addPrimitive(
      "Other circle", Circle{4.0}, Transform{Point{20.0, 0.0}});
  const NodeId invalid_open_boolean = invalid_nested.addBoolean(
      "Open arc Boolean", BooleanOperation::unite,
      invalid_circle, invalid_open_arc);
  const NodeId invalid_direct_boolean = invalid_nested.addBoolean(
      "Independent valid Boolean", BooleanOperation::unite,
      invalid_circle, invalid_other_circle);
  const NodeId invalid_split = invalid_nested.addSplit(
      "Split", invalid_circle, Point{}, Point{1.0, 0.0});
  const NodeId invalid_split_boolean = invalid_nested.addBoolean(
      "Split Boolean", BooleanOperation::unite,
      invalid_circle, invalid_split);
  const NodeId invalid_selection = invalid_nested.addRegionSelection(
      "Selection",
      invalid_split,
      {RegionKey{
          invalid_split,
          {RegionExpressionTerm{RegionExpressionTerm::Kind::leaf, invalid_circle}},
          RegionCutterSide::negative,
          {}}});
  const NodeId invalid_selection_boolean = invalid_nested.addBoolean(
      "Selection Boolean", BooleanOperation::unite,
      invalid_circle, invalid_selection);
  const auto invalid_snapshot = DocumentEvaluator::evaluate(invalid_nested);
  assert(invalid_snapshot.booleans.size() == 1);
  assert(booleanResult(invalid_snapshot, invalid_direct_boolean).regions.size() == 2);
  assert(hasDiagnostic(invalid_snapshot, invalid_open_boolean, "open Arc"));
  assert(hasDiagnostic(invalid_snapshot, invalid_open_boolean,
                       std::to_string(invalid_open_arc)));
  assert(hasDiagnostic(invalid_snapshot, invalid_split_boolean, "Split node"));
  assert(hasDiagnostic(invalid_snapshot, invalid_selection_boolean,
                       "Region selection node"));
  assert(invalid_snapshot.splits.size() == 1);
  assert(splitResult(invalid_snapshot, invalid_split).status ==
         signet::geometry::SplitStatus::success);
  assert(splitResult(invalid_snapshot, invalid_split).chords.size() == 1);
  assert(splitResult(invalid_snapshot, invalid_split).cells.size() == 2);
  assert(splitResult(invalid_snapshot, invalid_split).cells.front().key.split_node_id ==
         invalid_split);
  assert(!hasDiagnostic(invalid_snapshot, invalid_split, "not evaluated"));
  assert(hasDiagnostic(invalid_snapshot, invalid_selection, "Region selection"));
  assert(!hasDiagnostic(invalid_snapshot, invalid_direct_boolean, "unsupported"));

  Document split_evaluation("Split evaluation");
  const NodeId split_rectangle = split_evaluation.addPrimitive(
      "Rectangle", Rectangle{8.0, 6.0});
  const NodeId split_circle = split_evaluation.addPrimitive(
      "Circle", Circle{3.0}, Transform{Point{15.0, 0.0}});
  const NodeId split_rectangle_node = split_evaluation.addSplit(
      "Rectangle split", split_rectangle, Point{}, Point{0.0, 1.0});
  const NodeId split_circle_node = split_evaluation.addSplit(
      "Circle split", split_circle, Point{15.0, 0.0}, Point{1.0, 0.0});
  const auto split_first = DocumentEvaluator::evaluate(split_evaluation);
  assert(split_first.diagnostics.empty());
  assert(split_first.splits.size() == 2);
  const auto& rectangle_result = splitResult(split_first, split_rectangle_node);
  assert(rectangle_result.status == signet::geometry::SplitStatus::success);
  assert(rectangle_result.chords.size() == 1);
  assert(rectangle_result.cells.size() == 2);
  for (const auto& region : rectangle_result.cells) {
    assert(region.key.split_node_id == split_rectangle_node);
    assert(region.key.construction_expression.size() == 1);
    assert(region.key.construction_expression.front().node_id == split_rectangle);
    assert(!region.key.boundary_provenance.empty());
    for (const auto& loop : region.key.boundary_provenance) {
      for (const auto& provenance : loop) {
        assert(provenance.source_node_id == split_rectangle);
      }
    }
    assert(region.cell.face_id != 0);
  }
  const auto& circle_result = splitResult(split_first, split_circle_node);
  assert(circle_result.status == signet::geometry::SplitStatus::success);
  assert(circle_result.chords.size() == 1);
  assert(circle_result.cells.size() == 2);
  const auto split_second = DocumentEvaluator::evaluate(split_evaluation);
  assert(split_first.splits == split_second.splits);

  const NodeId reversed_rectangle_node = split_evaluation.addSplit(
      "Reversed rectangle split", split_rectangle, Point{}, Point{0.0, -1.0});
  const auto split_reversed_snapshot = DocumentEvaluator::evaluate(split_evaluation);
  const auto& reversed_rectangle_result =
      splitResult(split_reversed_snapshot, reversed_rectangle_node);
  assert(reversed_rectangle_result.status == rectangle_result.status);
  assert(reversed_rectangle_result.chords == rectangle_result.chords);
  assert(reversed_rectangle_result.cells.size() == rectangle_result.cells.size());
  for (std::size_t index = 0; index < rectangle_result.cells.size(); ++index) {
    auto forward_key = rectangle_result.cells[index].key;
    auto reversed_key = reversed_rectangle_result.cells[index].key;
    forward_key.split_node_id = 0;
    reversed_key.split_node_id = 0;
    assert(forward_key == reversed_key);
  }

  Document region_selection_evaluation("Region selection evaluation");
  const NodeId region_rectangle = region_selection_evaluation.addPrimitive(
      "Rectangle", Rectangle{8.0, 6.0});
  const NodeId region_split = region_selection_evaluation.addSplit(
      "Split", region_rectangle, Point{}, Point{0.0, 1.0});
  const auto region_initial = DocumentEvaluator::evaluate(region_selection_evaluation);
  const auto& region_split_result = splitResult(region_initial, region_split);
  assert(region_split_result.cells.size() == 2);
  std::vector<RegionKey> region_keys{
      region_split_result.cells[1].key,
      region_split_result.cells[0].key};
  std::ranges::sort(region_keys);
  const NodeId one_selection = region_selection_evaluation.addRegionSelection(
      "One", region_split, {region_keys.front()});
  const NodeId all_selection = region_selection_evaluation.addRegionSelection(
      "All", region_split, region_keys);
  const NodeId keep_one = region_selection_evaluation.addRegionFilter(
      "Keep one", region_split, one_selection, RegionFilterMode::keep_selected);
  const NodeId remove_one = region_selection_evaluation.addRegionFilter(
      "Remove one", region_split, one_selection, RegionFilterMode::remove_selected);
  const NodeId keep_all = region_selection_evaluation.addRegionFilter(
      "Keep all", region_split, all_selection, RegionFilterMode::keep_selected);
  const NodeId remove_all = region_selection_evaluation.addRegionFilter(
      "Remove all", region_split, all_selection, RegionFilterMode::remove_selected);
  const auto region_evaluated = DocumentEvaluator::evaluate(region_selection_evaluation);
  assert(region_evaluated.diagnostics.empty());
  assert(selectionResult(region_evaluated, one_selection).region_keys.size() == 1);
  assert(selectionResult(region_evaluated, one_selection).cells.size() == 1);
  assert(selectionResult(region_evaluated, all_selection).region_keys == region_keys);
  assert(selectionResult(region_evaluated, all_selection).cells.size() == 2);
  assert(filterResult(region_evaluated, keep_one).cells.size() == 1);
  assert(filterResult(region_evaluated, remove_one).cells.size() == 1);
  assert(filterResult(region_evaluated, keep_all).cells.size() == 2);
  assert(filterResult(region_evaluated, remove_all).cells.empty());
  assert(region_evaluated.region_selections ==
         DocumentEvaluator::evaluate(region_selection_evaluation).region_selections);
  assert(region_evaluated.region_filters ==
         DocumentEvaluator::evaluate(region_selection_evaluation).region_filters);

  const NodeId reversed_region_split = region_selection_evaluation.addSplit(
      "Reversed split", region_rectangle, Point{}, Point{0.0, -1.0});
  const auto reversed_region_initial =
      DocumentEvaluator::evaluate(region_selection_evaluation);
  const auto& reversed_split_result =
      splitResult(reversed_region_initial, reversed_region_split);
  assert(reversed_split_result.cells.size() == 2);
  const NodeId reversed_selection = region_selection_evaluation.addRegionSelection(
      "Reversed selection", reversed_region_split,
      {reversed_split_result.cells.front().key});
  const auto reversed_region_evaluated =
      DocumentEvaluator::evaluate(region_selection_evaluation);
  assert(reversed_region_evaluated.diagnostics.empty());
  assert(selectionResult(reversed_region_evaluated, reversed_selection).cells.size() == 1);

  Document unresolved_selection("Unresolved region selection");
  const NodeId unresolved_rectangle = unresolved_selection.addPrimitive(
      "Rectangle", Rectangle{8.0, 6.0});
  const NodeId unrelated_circle = unresolved_selection.addPrimitive(
      "Unrelated", Circle{2.0});
  const NodeId unresolved_split = unresolved_selection.addSplit(
      "Split", unresolved_rectangle, Point{}, Point{0.0, 1.0});
  const auto unresolved_initial = DocumentEvaluator::evaluate(unresolved_selection);
  RegionKey missing_key = splitResult(unresolved_initial, unresolved_split).cells.front().key;
  missing_key.construction_expression.front().node_id = unrelated_circle;
  const NodeId unresolved_node = unresolved_selection.addRegionSelection(
      "Missing key", unresolved_split, {missing_key});
  const NodeId unresolved_filter = unresolved_selection.addRegionFilter(
      "Missing filter", unresolved_split, unresolved_node, RegionFilterMode::remove_selected);
  const auto unresolved_evaluated = DocumentEvaluator::evaluate(unresolved_selection);
  assert(selectionResult(unresolved_evaluated, unresolved_node).cells.empty());
  assert(hasDiagnostic(unresolved_evaluated, unresolved_node, "unresolved RegionKey"));
  assert(filterResult(unresolved_evaluated, unresolved_filter).cells.empty());
  assert(hasDiagnostic(unresolved_evaluated, unresolved_filter, "selection is unresolved"));

  DocumentHistory region_history(Document("Region selection history"));
  const NodeId region_history_rectangle = region_history.addPrimitive(
      "Rectangle", Rectangle{8.0, 6.0});
  const NodeId region_history_split = region_history.addSplit(
      "Split", region_history_rectangle, Point{}, Point{0.0, 1.0});
  const auto region_history_before_nodes =
      DocumentEvaluator::evaluate(region_history.document());
  const NodeId region_history_selection = region_history.addRegionSelection(
      "Selection", region_history_split,
      {splitResult(region_history_before_nodes, region_history_split).cells.front().key});
  const NodeId region_history_filter = region_history.addRegionFilter(
      "Filter", region_history_split, region_history_selection,
      RegionFilterMode::remove_selected);
  const auto region_history_initial = DocumentEvaluator::evaluate(region_history.document());
  assert(region_history.setTransform(
      region_history_rectangle, Transform{Point{1.0, 0.0}, 0.0, Point{1.0, 1.0}}));
  const auto region_history_transformed =
      DocumentEvaluator::evaluate(region_history.document());
  assert(region_history.undo());
  const auto region_history_undo = DocumentEvaluator::evaluate(region_history.document());
  assert(region_history_undo.region_selections == region_history_initial.region_selections);
  assert(region_history_undo.region_filters == region_history_initial.region_filters);
  assert(region_history.redo());
  const auto region_history_redo = DocumentEvaluator::evaluate(region_history.document());
  assert(region_history_redo.region_selections == region_history_transformed.region_selections);
  assert(region_history_redo.region_filters == region_history_transformed.region_filters);
  assert(filterResult(region_history_redo, region_history_filter).cells.size() == 1);

  Document boolean_split("Boolean split evaluation");
  const NodeId outer = boolean_split.addPrimitive("Outer", Rectangle{8.0, 6.0});
  const NodeId hole = boolean_split.addPrimitive("Hole", Rectangle{4.0, 2.0});
  const NodeId difference = boolean_split.addBoolean(
      "Difference", BooleanOperation::subtract, outer, hole);
  const NodeId difference_split = boolean_split.addSplit(
      "Difference split", difference, Point{}, Point{1.0, 0.0});
  const NodeId left_circle = boolean_split.addPrimitive(
      "Left", Circle{2.0}, Transform{Point{-6.0, 0.0}});
  const NodeId right_circle = boolean_split.addPrimitive(
      "Right", Circle{2.0}, Transform{Point{6.0, 0.0}});
  const NodeId disconnected_union = boolean_split.addBoolean(
      "Disconnected union", BooleanOperation::unite, left_circle, right_circle);
  const NodeId disconnected_split = boolean_split.addSplit(
      "Disconnected split", disconnected_union, Point{}, Point{1.0, 0.0});
  const auto boolean_split_snapshot = DocumentEvaluator::evaluate(boolean_split);
  assert(boolean_split_snapshot.diagnostics.empty());
  const auto& difference_result = splitResult(boolean_split_snapshot, difference_split);
  assert(difference_result.status == signet::geometry::SplitStatus::success);
  assert(difference_result.chords.size() == 2);
  assert(difference_result.cells.size() == 2);
  for (const auto& region : difference_result.cells) {
    assert(region.key.construction_expression.size() == 3);
    assert(region.key.construction_expression[0].node_id == outer);
    assert(region.key.construction_expression[1].node_id == hole);
    assert(region.key.construction_expression[2].kind == RegionExpressionTerm::Kind::boolean);
    assert(region.key.construction_expression[2].operation == BooleanOperation::subtract);
    for (const auto& loop : region.key.boundary_provenance) {
      for (const auto& provenance : loop) {
        assert(provenance.source_node_id == outer || provenance.source_node_id == hole);
      }
    }
  }
  const auto& disconnected_result = splitResult(boolean_split_snapshot, disconnected_split);
  assert(disconnected_result.status == signet::geometry::SplitStatus::success);
  assert(disconnected_result.chords.size() == 2);
  assert(disconnected_result.cells.size() == 4);
  assert(disconnected_result.cells.front().key.construction_expression.size() == 3);

  Document invalid_split_inputs("Invalid split inputs");
  const NodeId open_arc = invalid_split_inputs.addPrimitive(
      "Open arc", Arc{4.0, 0.0, 90.0});
  const NodeId open_arc_split = invalid_split_inputs.addSplit(
      "Open arc split", open_arc, Point{}, Point{1.0, 0.0});
  const NodeId coincident_rectangle = invalid_split_inputs.addPrimitive(
      "Coincident rectangle", Rectangle{8.0, 6.0});
  const NodeId coincident_split = invalid_split_inputs.addSplit(
      "Coincident split", coincident_rectangle, Point{-4.0, 0.0}, Point{0.0, 1.0});
  const NodeId ambiguous_circle = invalid_split_inputs.addPrimitive(
      "Ambiguous circle", Circle{1.0}, Transform{Point{0.0, 1.0}});
  const NodeId ambiguous_outer = invalid_split_inputs.addPrimitive(
      "Ambiguous outer", Rectangle{8.0, 6.0});
  const NodeId ambiguous_boolean = invalid_split_inputs.addBoolean(
      "Ambiguous difference", BooleanOperation::subtract, ambiguous_outer, ambiguous_circle);
  const NodeId ambiguous_split = invalid_split_inputs.addSplit(
      "Ambiguous split", ambiguous_boolean, Point{}, Point{1.0, 0.0});
  bool rejected_missing_input = false;
  try {
    static_cast<void>(invalid_split_inputs.addSplit(
        "Missing input", 999999, Point{}, Point{1.0, 0.0}));
  } catch (const std::invalid_argument&) {
    rejected_missing_input = true;
  }
  assert(rejected_missing_input);
  const auto invalid_split_snapshot = DocumentEvaluator::evaluate(invalid_split_inputs);
  assert(splitResult(invalid_split_snapshot, open_arc_split).status ==
         signet::geometry::SplitStatus::invalid_input);
  assert(hasDiagnostic(invalid_split_snapshot, open_arc_split, "open Arc"));
  assert(hasDiagnostic(invalid_split_snapshot, open_arc_split, std::to_string(open_arc)));
  assert(splitResult(invalid_split_snapshot, coincident_split).status ==
         signet::geometry::SplitStatus::boundary_coincident);
  assert(hasDiagnostic(invalid_split_snapshot, coincident_split, "coincident"));
  assert(splitResult(invalid_split_snapshot, ambiguous_split).status ==
         signet::geometry::SplitStatus::branch_ambiguity);
  assert(hasDiagnostic(invalid_split_snapshot, ambiguous_split, "branch ambiguity"));
}
