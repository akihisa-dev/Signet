// SPDX-License-Identifier: AGPL-3.0-or-later
#include "geometry/arrangement_model.h"
#include "geometry/document_evaluator.h"

#include <cassert>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

bool close(const double left, const double right) {
  return std::abs(left - right) <= 1.0e-12;
}

enum class MatrixShape {
  circle,
  rectangle,
  golden_rectangle,
  full_circle_arc,
};

enum class SpatialCase {
  overlap,
  disjoint,
  touching,
  containment,
};

double halfWidth(const MatrixShape shape, const double scale) {
  switch (shape) {
    case MatrixShape::circle:
    case MatrixShape::full_circle_arc:
      return 4.0 * scale;
    case MatrixShape::rectangle:
      return 4.0 * scale;
    case MatrixShape::golden_rectangle:
      return 3.0 * std::numbers::phi_v<double> * scale;
  }
  return 0.0;
}

std::vector<signet::geometry::CurveInput> matrixShape(
    const MatrixShape shape,
    const double center_x,
    const double scale = 1.0) {
  using signet::geometry::CircleInput;
  using signet::geometry::CurveInput;
  using signet::geometry::SegmentInput;

  switch (shape) {
    case MatrixShape::circle:
    case MatrixShape::full_circle_arc:
      // ArrangementModel's public curve DTO intentionally normalizes a full
      // 360-degree Arc to CircleInput; DocumentEvaluator covers the source
      // primitive distinction separately.
      return {CircleInput{center_x, 0.0, 4.0 * scale}};
    case MatrixShape::rectangle: {
      const double half_width = 4.0 * scale;
      const double half_height = 3.0 * scale;
      return {
          SegmentInput{center_x - half_width, -half_height, center_x + half_width, -half_height},
          SegmentInput{center_x + half_width, -half_height, center_x + half_width, half_height},
          SegmentInput{center_x + half_width, half_height, center_x - half_width, half_height},
          SegmentInput{center_x - half_width, half_height, center_x - half_width, -half_height}};
    }
    case MatrixShape::golden_rectangle: {
      const double half_width = halfWidth(shape, scale);
      const double half_height = 3.0 * scale;
      return {
          SegmentInput{center_x - half_width, -half_height, center_x + half_width, -half_height},
          SegmentInput{center_x + half_width, -half_height, center_x + half_width, half_height},
          SegmentInput{center_x + half_width, half_height, center_x - half_width, half_height},
          SegmentInput{center_x - half_width, half_height, center_x - half_width, -half_height}};
    }
  }
  return {};
}

std::array<std::size_t, 4> expectedCounts(const SpatialCase spatial_case) {
  switch (spatial_case) {
    case SpatialCase::overlap:
      return {3, 1, 1, 2};
    case SpatialCase::disjoint:
    case SpatialCase::touching:
      return {2, 0, 1, 2};
    case SpatialCase::containment:
      return {2, 1, 1, 1};
  }
  return {};
}

void assertClosedBooleanSnapshot(
    const signet::geometry::BooleanEvaluationSnapshot& snapshot,
    const std::size_t source_curve_count) {
  for (std::size_t index = 0; index < snapshot.regions.size(); ++index) {
    const auto& region = snapshot.regions[index];
    assert(region.bounded);
    assert(region.selected);
    assert(region.face_id != 0);
    assert(!region.outer_boundary.edges.empty());
    assert(region.outer_boundary.closed);
    assert(region.outer_boundary.edges.back().end == region.outer_boundary.edges.front().start);
    for (const auto& loop : region.holes) {
      assert(loop.closed);
      assert(!loop.edges.empty());
      assert(loop.edges.back().end == loop.edges.front().start);
    }
    const auto assert_sources = [source_curve_count](const auto& loop) {
      for (const auto& edge : loop.edges) {
        assert(!edge.source_curve_indices.empty());
        for (const std::size_t source_index : edge.source_curve_indices) {
          assert(source_index < source_curve_count);
        }
      }
    };
    assert_sources(region.outer_boundary);
    for (const auto& hole : region.holes) {
      assert_sources(hole);
    }
  }
}

void assertBooleanMatrixCase(
    signet::geometry::ArrangementModel& model,
    const std::vector<signet::geometry::CurveInput>& left,
    const std::vector<signet::geometry::CurveInput>& right,
    const std::array<std::size_t, 4>* expected = nullptr) {
  model.setBooleanOperands(left, right);
  assert(model.hasCompleteSourceHistory());
  const std::size_t left_source_curve_count = left.size();
  const std::array<signet::geometry::BooleanOperation, 4> operations{
      signet::geometry::BooleanOperation::unite,
      signet::geometry::BooleanOperation::intersect,
      signet::geometry::BooleanOperation::subtract,
      signet::geometry::BooleanOperation::exclusive_or};
  for (std::size_t index = 0; index < operations.size(); ++index) {
    const auto classifications = model.classifyFaces(operations[index], left_source_curve_count);
    const auto expected_selected = [operation = operations[index]](
                                       const bool inside_left,
                                       const bool inside_right) {
      switch (operation) {
        case signet::geometry::BooleanOperation::unite:
          return inside_left || inside_right;
        case signet::geometry::BooleanOperation::intersect:
          return inside_left && inside_right;
        case signet::geometry::BooleanOperation::subtract:
          return inside_left && !inside_right;
        case signet::geometry::BooleanOperation::exclusive_or:
          return inside_left != inside_right;
      }
      return false;
    };
    std::size_t selected_region_count = 0;
    for (const auto& face : classifications) {
      assert(face.selected == expected_selected(face.inside_left, face.inside_right));
      if (face.bounded && face.selected) {
        ++selected_region_count;
      }
    }
    const auto first = model.evaluateBoolean(operations[index], left_source_curve_count);
    const auto second = model.evaluateBoolean(operations[index], left_source_curve_count);
    assert(first == second);
    assert(first.regions.size() == selected_region_count);
    assertClosedBooleanSnapshot(first, model.sourceCurveCount());
    if (expected != nullptr) {
      assert(first.regions.size() == (*expected)[index]);
    }
  }
}

}  // namespace

int main() {
  using signet::geometry::ArrangementModel;
  using signet::geometry::ArcInput;
  using signet::geometry::BooleanOperation;
  using signet::geometry::CircleInput;
  using signet::geometry::CurveInput;
  using signet::geometry::FaceClassification;
  using signet::geometry::FaceSelector;
  using signet::geometry::OperandGroup;
  using signet::geometry::RegionHit;
  using signet::geometry::SegmentInput;
  using signet::geometry::SplitAxis;

  ArrangementModel model;
  model.setCircles({CircleInput{-5.0, 0.0, 10.0}, CircleInput{5.0, 0.0, 10.0}});

  assert(model.sourceCurveCount() == 2);
  assert(model.vertexCount() == 6);
  assert(model.edgeCount() == 8);
  assert(model.faceCount() == 4);
  assert(model.hasCompleteSourceHistory());

  const RegionHit overlap = model.locate(0.0, 0.0);
  assert(overlap.kind == RegionHit::Kind::face);
  assert(overlap.bounded);
  assert(overlap.face_id.has_value());

  const RegionHit outside = model.locate(30.0, 0.0);
  assert(outside.kind == RegionHit::Kind::face);
  assert(!outside.bounded);
  assert(outside.face_id.has_value());

  const auto has_face_id = [id = *overlap.face_id](const std::vector<FaceClassification>& faces) {
    return std::any_of(faces.begin(), faces.end(), [=](const FaceClassification face) {
      return face.face_id == id && face.bounded;
    });
  };
  const auto all_boundaries_are_closed = [](const std::vector<FaceClassification>& faces) {
    return std::all_of(faces.begin(), faces.end(), [](const FaceClassification& face) {
      const auto loop_closed = [](const signet::geometry::BoundaryLoop& loop) {
        return loop.closed && !loop.edges.empty() &&
               loop.edges.back().end == loop.edges.front().start &&
               std::all_of(loop.edges.begin(), loop.edges.end(), [](const auto& edge) {
                 return !edge.source_curve_indices.empty();
               });
      };
      return !face.bounded || (loop_closed(face.outer_boundary) &&
                               std::all_of(face.holes.begin(), face.holes.end(), loop_closed));
    });
  };

  const auto selected_count = [](const std::vector<FaceClassification>& faces) {
    return static_cast<std::size_t>(std::count_if(
        faces.begin(), faces.end(), [](const FaceClassification face) { return face.selected; }));
  };
  const auto find_classification = [](const std::vector<FaceClassification>& faces,
                                      const bool inside_left, const bool inside_right) {
    const auto found = std::find_if(
        faces.begin(), faces.end(), [=](const FaceClassification face) {
          return face.inside_left == inside_left && face.inside_right == inside_right;
        });
    assert(found != faces.end());
    return *found;
  };

  const auto assert_boolean_region_counts = [&model](
      std::vector<CurveInput> left,
      std::vector<CurveInput> right,
      const std::array<std::size_t, 4> expected) {
    model.setBooleanOperands(std::move(left), std::move(right));
    assert(model.hasCompleteSourceHistory());
    assert(model.evaluateBoolean(BooleanOperation::unite).regions.size() == expected[0]);
    assert(model.evaluateBoolean(BooleanOperation::intersect).regions.size() == expected[1]);
    assert(model.evaluateBoolean(BooleanOperation::subtract).regions.size() == expected[2]);
    assert(model.evaluateBoolean(BooleanOperation::exclusive_or).regions.size() == expected[3]);
  };

  assert(selected_count(model.classifyFaces(BooleanOperation::unite, 1)) == 3);
  assert(selected_count(model.classifyFaces(BooleanOperation::intersect, 1)) == 1);
  assert(selected_count(model.classifyFaces(BooleanOperation::subtract, 1)) == 1);
  assert(selected_count(model.classifyFaces(BooleanOperation::exclusive_or, 1)) == 2);
  const FaceClassification overlap_face = find_classification(
      model.classifyFaces(BooleanOperation::intersect, 1), true, true);
  assert(overlap_face.bounded);
  assert(overlap_face.selected);
  assert(has_face_id(model.classifyFaces(BooleanOperation::intersect, 1)));
  assert(all_boundaries_are_closed(model.classifyFaces(BooleanOperation::intersect, 1)));
  assert(overlap_face.outer_boundary.edges.size() >= 2);
  for (const auto& edge : overlap_face.outer_boundary.edges) {
    assert(std::holds_alternative<signet::geometry::CircularArcBoundary>(edge.curve));
    const auto& arc = std::get<signet::geometry::CircularArcBoundary>(edge.curve);
    assert(close(arc.radius, 10.0));
    assert(arc.start != arc.end);
  }

  const auto union_regions = model.evaluateBoolean(BooleanOperation::unite, 1).regions;
  assert(union_regions.size() == 3);
  assert(all_boundaries_are_closed(union_regions));

  const std::vector<CurveInput> outer_rectangle{
      SegmentInput{-4.0, -3.0, 4.0, -3.0},
      SegmentInput{4.0, -3.0, 4.0, 3.0},
      SegmentInput{4.0, 3.0, -4.0, 3.0},
      SegmentInput{-4.0, 3.0, -4.0, -3.0}};
  const std::vector<CurveInput> inner_rectangle{
      SegmentInput{-2.0, -1.0, 2.0, -1.0},
      SegmentInput{2.0, -1.0, 2.0, 1.0},
      SegmentInput{2.0, 1.0, -2.0, 1.0},
      SegmentInput{-2.0, 1.0, -2.0, -1.0}};
  assert_boolean_region_counts(
      outer_rectangle, {CircleInput{0.0, 0.0, 2.0}}, {2, 1, 1, 1});
  assert_boolean_region_counts(
      outer_rectangle, {CircleInput{10.0, 0.0, 2.0}}, {2, 0, 1, 2});
  assert_boolean_region_counts(
      outer_rectangle, {CircleInput{6.0, 0.0, 2.0}}, {2, 0, 1, 2});
  assert_boolean_region_counts(outer_rectangle, inner_rectangle, {2, 1, 1, 1});
  assert_boolean_region_counts(outer_rectangle, outer_rectangle, {1, 1, 0, 0});

  model.setBooleanOperands(outer_rectangle, {CircleInput{0.0, 0.0, 2.0}});
  const auto first_general_snapshot = model.evaluateBoolean(BooleanOperation::unite);
  model.setBooleanOperands(outer_rectangle, {CircleInput{0.0, 0.0, 2.0}});
  const auto second_general_snapshot = model.evaluateBoolean(BooleanOperation::unite);
  assert(first_general_snapshot.operation == second_general_snapshot.operation);
  assert(first_general_snapshot.regions == second_general_snapshot.regions);
  assert(all_boundaries_are_closed(first_general_snapshot.regions));

  model.setCircles({CircleInput{0.0, 0.0, 10.0}, CircleInput{20.0, 0.0, 10.0}});
  assert(model.sourceCurveCount() == 2);
  assert(model.vertexCount() == 3);
  assert(model.hasCompleteSourceHistory());
  assert(selected_count(model.classifyFaces(BooleanOperation::unite, 1)) == 2);
  assert(selected_count(model.classifyFaces(BooleanOperation::intersect, 1)) == 0);
  assert(selected_count(model.classifyFaces(BooleanOperation::subtract, 1)) == 1);
  assert(selected_count(model.classifyFaces(BooleanOperation::exclusive_or, 1)) == 2);
  assert(model.evaluateBoolean(BooleanOperation::intersect, 1).regions.empty());
  assert(model.evaluateBoolean(BooleanOperation::unite, 1).regions.size() == 2);

  const RegionHit tangent_vertex = model.locate(10.0, 0.0);
  assert(tangent_vertex.kind == RegionHit::Kind::vertex);
  assert(!tangent_vertex.face_id.has_value());

  model.setCircles({CircleInput{0.0, 0.0, 10.0}, CircleInput{0.0, 0.0, 5.0}});
  assert(model.faceCount() == 3);
  assert(selected_count(model.classifyFaces(BooleanOperation::unite, 1)) == 2);
  assert(selected_count(model.classifyFaces(BooleanOperation::intersect, 1)) == 1);
  assert(selected_count(model.classifyFaces(BooleanOperation::subtract, 1)) == 1);
  assert(selected_count(model.classifyFaces(BooleanOperation::exclusive_or, 1)) == 1);
  const auto containment = model.evaluateBoolean(BooleanOperation::subtract, 1).regions;
  assert(containment.size() == 1);
  assert(containment.front().outer_boundary.edges.size() == 2);
  assert(containment.front().holes.size() == 1);
  assert(containment.front().holes.front().edges.size() == 2);
  assert(all_boundaries_are_closed(containment));

  model.setCircles({CircleInput{0.0, 0.0, 10.0}, CircleInput{0.0, 0.0, 10.0}});
  assert(model.faceCount() == 2);
  assert(model.hasCompleteSourceHistory());
  assert(selected_count(model.classifyFaces(BooleanOperation::unite, 1)) == 1);
  assert(selected_count(model.classifyFaces(BooleanOperation::intersect, 1)) == 1);
  assert(selected_count(model.classifyFaces(BooleanOperation::subtract, 1)) == 0);
  assert(selected_count(model.classifyFaces(BooleanOperation::exclusive_or, 1)) == 0);
  assert(model.evaluateBoolean(BooleanOperation::unite, 1).regions.size() == 1);
  assert(model.evaluateBoolean(BooleanOperation::intersect, 1).regions.size() == 1);
  assert(model.evaluateBoolean(BooleanOperation::subtract, 1).regions.empty());

  model.setCircles({CircleInput{-5.0, 0.0, 10.0}, CircleInput{5.0, 0.0, 10.0}});
  const auto forward_xor = model.classifyFaces(BooleanOperation::exclusive_or, 1);
  model.setCircles({CircleInput{5.0, 0.0, 10.0}, CircleInput{-5.0, 0.0, 10.0}});
  const auto reversed_xor = model.classifyFaces(BooleanOperation::exclusive_or, 1);
  assert(selected_count(forward_xor) == selected_count(reversed_xor));
  assert(model.faceCount() == 4);
  const auto forward_xor_regions = model.evaluateBoolean(BooleanOperation::exclusive_or, 1).regions;
  model.setCircles({CircleInput{5.0, 0.0, 10.0}, CircleInput{-5.0, 0.0, 10.0}});
  const auto reversed_xor_regions = model.evaluateBoolean(BooleanOperation::exclusive_or, 1).regions;
  assert(forward_xor_regions.size() == reversed_xor_regions.size());
  assert(all_boundaries_are_closed(forward_xor_regions));
  assert(all_boundaries_are_closed(reversed_xor_regions));

  model.setCircles({CircleInput{0.0, 0.0, 10.0}, CircleInput{30.0, 0.0, 10.0}});
  assert(selected_count(model.classifyFaces(BooleanOperation::intersect, 1)) == 0);
  model.setCircles({});
  assert(model.faceCount() == 1);
  assert(selected_count(model.classifyFaces(BooleanOperation::unite, 0)) == 0);
  assert(selected_count(model.classifyFaces(BooleanOperation::intersect, 0)) == 0);

  model.setCircles({CircleInput{1.0e170, -1.0e170, 1.0e170}});
  assert(model.faceCount() == 2);
  assert(model.hasCompleteSourceHistory());
  model.setCircles({CircleInput{1.0e-170, -1.0e-170, 1.0e-170}});
  assert(model.faceCount() == 2);
  assert(model.hasCompleteSourceHistory());

  model.setCurves({
      SegmentInput{-10.0, -10.0, 10.0, 10.0},
      SegmentInput{-10.0, 10.0, 10.0, -10.0},
  });
  assert(model.sourceCurveCount() == 2);
  assert(model.vertexCount() == 5);
  assert(model.edgeCount() == 4);
  assert(model.faceCount() == 1);
  assert(model.hasCompleteSourceHistory());

  model.setCurves({
      ArcInput{-10.0, 0.0, 0.0, 10.0, 10.0, 0.0},
      ArcInput{10.0, 0.0, 0.0, -10.0, -10.0, 0.0},
  });
  assert(model.sourceCurveCount() == 2);
  assert(model.faceCount() == 2);
  assert(model.hasCompleteSourceHistory());
  const RegionHit arc_enclosure = model.locate(0.0, 0.0);
  assert(arc_enclosure.kind == RegionHit::Kind::face);
  assert(arc_enclosure.bounded);

  const RegionHit arc_edge = model.locate(0.0, 10.0);
  assert(arc_edge.kind == RegionHit::Kind::edge);
  assert(!arc_edge.face_id.has_value());

  bool rejected_invalid_circle = false;
  try {
    model.setCircles({CircleInput{0.0, 0.0, 0.0}});
  } catch (const std::invalid_argument&) {
    rejected_invalid_circle = true;
  }
  assert(rejected_invalid_circle);

  bool rejected_degenerate_arc = false;
  try {
    model.setCurves({ArcInput{0.0, 0.0, 1.0, 1.0, 2.0, 2.0}});
  } catch (const std::invalid_argument&) {
    rejected_degenerate_arc = true;
  }
  assert(rejected_degenerate_arc);

  bool rejected_open_curve_boolean = false;
  try {
    model.setCurves({SegmentInput{-1.0, 0.0, 1.0, 0.0}});
    static_cast<void>(model.classifyFaces(BooleanOperation::unite, 1));
  } catch (const std::invalid_argument&) {
    rejected_open_curve_boolean = true;
  }
  assert(rejected_open_curve_boolean);

  bool rejected_open_arc_operand = false;
  try {
    model.setBooleanOperands(
        {ArcInput{10.0, 0.0, std::sqrt(50.0), std::sqrt(50.0), 0.0, 10.0}},
        {CircleInput{0.0, 0.0, 5.0}});
  } catch (const std::invalid_argument&) {
    rejected_open_arc_operand = true;
  }
  assert(rejected_open_arc_operand);

  model.setBooleanOperands(outer_rectangle, {CircleInput{0.0, 0.0, 2.0}});
  const auto boolean_state_before_rejection =
      model.evaluateBoolean(BooleanOperation::unite);
  const auto source_count_before_rejection = model.sourceCurveCount();
  const auto vertex_count_before_rejection = model.vertexCount();
  const auto edge_count_before_rejection = model.edgeCount();
  const auto face_count_before_rejection = model.faceCount();

  const auto assert_boolean_state_preserved = [&]() {
    assert(model.sourceCurveCount() == source_count_before_rejection);
    assert(model.vertexCount() == vertex_count_before_rejection);
    assert(model.edgeCount() == edge_count_before_rejection);
    assert(model.faceCount() == face_count_before_rejection);
    assert(model.evaluateBoolean(BooleanOperation::unite) == boolean_state_before_rejection);
  };

  bool rejected_open_segment_loop = false;
  try {
    model.setBooleanOperands(
        {SegmentInput{-2.0, -1.0, 2.0, -1.0}, SegmentInput{2.0, -1.0, 2.0, 1.0},
         SegmentInput{2.0, 1.0, -2.0, 1.0}},
        {CircleInput{0.0, 0.0, 2.0}});
  } catch (const std::invalid_argument&) {
    rejected_open_segment_loop = true;
  }
  assert(rejected_open_segment_loop);
  assert_boolean_state_preserved();

  bool rejected_branch_vertex = false;
  try {
    model.setBooleanOperands(
        {SegmentInput{0.0, 0.0, 1.0, 0.0}, SegmentInput{0.0, 0.0, 0.0, 1.0},
         SegmentInput{0.0, 0.0, -1.0, 0.0}},
        {CircleInput{0.0, 0.0, 2.0}});
  } catch (const std::invalid_argument&) {
    rejected_branch_vertex = true;
  }
  assert(rejected_branch_vertex);
  assert_boolean_state_preserved();

  bool rejected_same_operand_duplicate = false;
  try {
    model.setBooleanOperands(
        {SegmentInput{0.0, 0.0, 1.0, 0.0}, SegmentInput{1.0, 0.0, 0.0, 0.0}},
        {CircleInput{0.0, 0.0, 2.0}});
  } catch (const std::invalid_argument&) {
    rejected_same_operand_duplicate = true;
  }
  assert(rejected_same_operand_duplicate);
  assert_boolean_state_preserved();

  signet::core::Document rectangle_document("Rectangle arrangement");
  const signet::core::NodeId rectangle_node =
      rectangle_document.addPrimitive("Rectangle", signet::core::Rectangle{8.0, 6.0});
  const auto rectangle_evaluation =
      signet::geometry::DocumentEvaluator::evaluate(rectangle_document);
  const auto& rectangle_set = rectangle_evaluation.curve_sets.front();
  assert(rectangle_set.node_id == rectangle_node);
  assert(rectangle_set.curves.size() == 4);
  model.setCurves(rectangle_set.curves);
  assert(model.sourceCurveCount() == 4);
  assert(model.vertexCount() == 4);
  assert(model.edgeCount() == 4);
  assert(model.faceCount() == 2);
  assert(model.hasCompleteSourceHistory());
  const RegionHit rectangle_hit = model.locate(0.0, 0.0);
  assert(rectangle_hit.kind == RegionHit::Kind::face);
  assert(rectangle_hit.bounded);
  assert(rectangle_hit.face_id.has_value());

  signet::core::Document reflected_rectangle_document("Reflected rectangle arrangement");
  const signet::core::NodeId source_rectangle = reflected_rectangle_document.addPrimitive(
      "Source", signet::core::Rectangle{8.0, 6.0});
  const signet::core::NodeId reflected_rectangle = reflected_rectangle_document.addSymmetry(
      "Reflection", source_rectangle, signet::core::Point{2.0, 0.0},
      signet::core::Point{0.0, 1.0});
  const auto reflected_rectangle_evaluation =
      signet::geometry::DocumentEvaluator::evaluate(reflected_rectangle_document);
  const auto reflected_rectangle_set = std::find_if(
      reflected_rectangle_evaluation.curve_sets.begin(),
      reflected_rectangle_evaluation.curve_sets.end(),
      [reflected_rectangle](const auto& set) { return set.node_id == reflected_rectangle; });
  assert(reflected_rectangle_set != reflected_rectangle_evaluation.curve_sets.end());
  model.setCurves(reflected_rectangle_set->curves);
  assert(model.sourceCurveCount() == 4);
  assert(model.faceCount() == 2);
  assert(model.hasCompleteSourceHistory());
  const RegionHit reflected_rectangle_hit = model.locate(4.0, 0.0);
  assert(reflected_rectangle_hit.kind == RegionHit::Kind::face);
  assert(reflected_rectangle_hit.bounded);
  assert(reflected_rectangle_hit.face_id.has_value());

  signet::core::Document golden_document("Golden rectangle arrangement");
  const signet::core::NodeId golden_node =
      golden_document.addPrimitive("Golden", signet::core::GoldenRectangle{2.0});
  const auto golden_evaluation = signet::geometry::DocumentEvaluator::evaluate(golden_document);
  const auto golden_set = std::find_if(
      golden_evaluation.curve_sets.begin(),
      golden_evaluation.curve_sets.end(),
      [golden_node](const auto& set) { return set.node_id == golden_node; });
  assert(golden_set != golden_evaluation.curve_sets.end());
  assert(golden_set->curves.size() == 4);
  assert_boolean_region_counts(
      golden_set->curves, {CircleInput{0.0, 0.0, 0.5}}, {2, 1, 1, 1});

  signet::core::Document arc_document("Arc arrangement");
  const signet::core::NodeId upper_arc =
      arc_document.addPrimitive("Upper", signet::core::Arc{10.0, 0.0, 360.0});
  const auto arc_evaluation = signet::geometry::DocumentEvaluator::evaluate(arc_document);
  assert(arc_evaluation.curve_sets.size() == 1);
  assert(arc_evaluation.curve_sets[0].node_id == upper_arc);
  assert(std::holds_alternative<CircleInput>(arc_evaluation.curve_sets[0].curves.front()));
  model.setCurves(arc_evaluation.curve_sets[0].curves);
  assert(model.sourceCurveCount() == 1);
  assert(model.faceCount() == 2);
  assert(model.hasCompleteSourceHistory());
  const RegionHit arc_face = model.locate(0.0, 0.0);
  assert(arc_face.kind == RegionHit::Kind::face);
  assert(arc_face.bounded);
  assert(arc_face.face_id.has_value());
  const RegionHit arc_boundary = model.locate(10.0, 0.0);
  assert(arc_boundary.kind == RegionHit::Kind::vertex);
  assert(!arc_boundary.face_id.has_value());

  // Every source kind is exercised against every other source kind. The
  // expected topologies below are deliberately checked without inspecting
  // CGAL's curve/edge order; only the public region and provenance DTOs are
  // part of this contract.
  const std::array<MatrixShape, 4> matrix_shapes{
      MatrixShape::circle,
      MatrixShape::rectangle,
      MatrixShape::golden_rectangle,
      MatrixShape::full_circle_arc};
  const std::array<SpatialCase, 4> spatial_cases{
      SpatialCase::overlap,
      SpatialCase::disjoint,
      SpatialCase::touching,
      SpatialCase::containment};
  for (const MatrixShape left_shape : matrix_shapes) {
    for (const MatrixShape right_shape : matrix_shapes) {
      for (const SpatialCase spatial_case : spatial_cases) {
        const double left_scale = spatial_case == SpatialCase::containment ? 3.0 : 1.0;
        const double right_scale = spatial_case == SpatialCase::containment ? 0.5 : 1.0;
        double right_center = 2.0;
        if (spatial_case == SpatialCase::disjoint) {
          right_center = halfWidth(left_shape, left_scale) +
                         halfWidth(right_shape, right_scale) + 5.0;
        } else if (spatial_case == SpatialCase::touching) {
          right_center = halfWidth(left_shape, left_scale) +
                         halfWidth(right_shape, right_scale);
        } else if (spatial_case == SpatialCase::containment) {
          right_center = 0.0;
        }
        const auto left = matrixShape(left_shape, 0.0, left_scale);
        const auto right = matrixShape(right_shape, right_center, right_scale);
        const auto counts = expectedCounts(spatial_case);
        assertBooleanMatrixCase(
            model, left, right,
            spatial_case == SpatialCase::overlap ? nullptr : &counts);
      }

      // Coincident boundaries are only meaningful where the two DTOs have
      // the same contour. CircleInput is also the normalized representation
      // of full-circle Arc at this layer, so that pair is intentionally
      // included as a valid coincident case.
      if (left_shape == right_shape ||
          (left_shape == MatrixShape::circle &&
           right_shape == MatrixShape::full_circle_arc) ||
          (left_shape == MatrixShape::full_circle_arc &&
           right_shape == MatrixShape::circle)) {
        const auto coincident_left = matrixShape(left_shape, 0.0);
        const auto coincident_right = matrixShape(right_shape, 0.0);
        const std::array<std::size_t, 4> counts{1, 1, 0, 0};
        assertBooleanMatrixCase(model, coincident_left, coincident_right, &counts);
      }
    }
  }

  // Region location and FaceId remain snapshot-local, but every selected
  // region must be discoverable by its stable ID within that snapshot.
  model.setBooleanOperands(matrixShape(MatrixShape::circle, 0.0),
                           matrixShape(MatrixShape::rectangle, 0.5));
  const auto representative = model.evaluateBoolean(BooleanOperation::intersect);
  const RegionHit representative_hit = model.locate(0.0, 0.0);
  assert(representative_hit.kind == RegionHit::Kind::face);
  assert(representative_hit.bounded);
  assert(representative_hit.face_id.has_value());
  const auto representative_face = std::find_if(
      representative.regions.begin(), representative.regions.end(),
      [id = *representative_hit.face_id](const auto& region) { return region.face_id == id; });
  assert(representative_face != representative.regions.end());

  const OperandGroup operand_a{CircleInput{-10.0, 0.0, 2.0}};
  const OperandGroup operand_b{CircleInput{0.0, 0.0, 2.0}};
  const OperandGroup operand_c{CircleInput{10.0, 0.0, 2.0}};
  const OperandGroup operand_d{CircleInput{12.0, 0.0, 2.0}};

  // N-operand selection exposes exact, caller-ordered group membership and
  // retains the normal boundary/source-history DTO contract.
  model.setBooleanOperandGroups({operand_a, operand_b, operand_c});
  std::vector<std::vector<bool>> three_memberships;
  const FaceSelector three_selector = [&three_memberships](const std::span<const bool> membership) {
    assert(membership.size() == 3);
    three_memberships.emplace_back(membership.begin(), membership.end());
    return (membership[0] || membership[1]) && !membership[2];
  };
  const auto three_classifications = model.classifyFaces(three_selector);
  assert(three_classifications.size() == 4);
  assert(three_memberships.size() == three_classifications.size());
  for (const auto& face : three_classifications) {
    assert(face.operand_membership.size() == 3);
    assert(face.inside_left == face.operand_membership[0]);
    assert(face.inside_right == face.operand_membership[1]);
  }
  assert(std::ranges::find(three_memberships, std::vector<bool>{false, false, false}) !=
         three_memberships.end());
  assert(std::ranges::find(three_memberships, std::vector<bool>{true, false, false}) !=
         three_memberships.end());
  assert(std::ranges::find(three_memberships, std::vector<bool>{false, true, false}) !=
         three_memberships.end());
  assert(std::ranges::find(three_memberships, std::vector<bool>{false, false, true}) !=
         three_memberships.end());
  const auto three_snapshot = model.evaluateBoolean(three_selector);
  assert(three_snapshot.regions.size() == 2);
  assertClosedBooleanSnapshot(three_snapshot, model.sourceCurveCount());
  assert(model.evaluateBoolean(three_selector) == three_snapshot);

  model.setBooleanOperandGroups({operand_a});
  const FaceSelector one_selector = [](const std::span<const bool> membership) {
    assert(membership.size() == 1);
    return membership[0];
  };
  const auto one_snapshot = model.booleanSnapshot(one_selector);
  assert(one_snapshot.regions.size() == 1);
  assertClosedBooleanSnapshot(one_snapshot, model.sourceCurveCount());

  // A four-group selector can express a nested Boolean expression without
  // exposing arrangement handles or routing through display DTOs.
  model.setBooleanOperandGroups({
      {CircleInput{-12.0, 0.0, 3.0}},
      {CircleInput{-12.0, 0.0, 1.0}},
      {CircleInput{6.0, 0.0, 3.0}},
      {CircleInput{8.0, 0.0, 3.0}},
  });
  const FaceSelector four_selector = [](const std::span<const bool> membership) {
    assert(membership.size() == 4);
    return (membership[0] && !membership[1]) || (membership[2] && membership[3]);
  };
  const auto four_classifications = model.classifyFaces(four_selector);
  std::size_t four_selected_bounded = 0;
  for (const auto& face : four_classifications) {
    assert(face.operand_membership.size() == 4);
    if (face.bounded && face.selected) {
      ++four_selected_bounded;
    }
  }
  assert(four_selected_bounded == 2);
  const auto four_snapshot = model.evaluateBoolean(four_selector);
  assert(four_snapshot.regions.size() == 2);
  assertClosedBooleanSnapshot(four_snapshot, model.sourceCurveCount());

  // Coincident contours in different groups remain distinct memberships and
  // cross each group exactly once, independent of source-history multiplicity.
  model.setBooleanOperandGroups({operand_a, operand_a, operand_c});
  std::vector<std::vector<bool>> coincident_memberships;
  const FaceSelector coincident_selector = [&coincident_memberships](
                                                 const std::span<const bool> membership) {
    assert(membership.size() == 3);
    coincident_memberships.emplace_back(membership.begin(), membership.end());
    return membership[0] || membership[1] || membership[2];
  };
  const auto coincident_snapshot = model.evaluateBoolean(coincident_selector);
  assert(coincident_snapshot.regions.size() == 2);
  assert(std::ranges::find(coincident_memberships, std::vector<bool>{true, true, false}) !=
         coincident_memberships.end());
  assert(std::ranges::find(coincident_memberships, std::vector<bool>{false, false, true}) !=
         coincident_memberships.end());
  assert(model.hasCompleteSourceHistory());

  // Reversing group order changes membership order deliberately, while the
  // same input and selector remain deterministic on repeated evaluation.
  model.setBooleanOperandGroups({operand_c, operand_b, operand_a});
  const FaceSelector all_three_selector = [](const std::span<const bool> membership) {
    assert(membership.size() == 3);
    return membership[0] || membership[1] || membership[2];
  };
  const auto reversed_first = model.evaluateBoolean(all_three_selector);
  const auto reversed_second = model.evaluateBoolean(all_three_selector);
  assert(reversed_first == reversed_second);
  assert(reversed_first.regions.size() == 3);

  const std::vector<CurveInput> split_rectangle{
      SegmentInput{-4.0, -3.0, 4.0, -3.0},
      SegmentInput{4.0, -3.0, 4.0, 3.0},
      SegmentInput{4.0, 3.0, -4.0, 3.0},
      SegmentInput{-4.0, 3.0, -4.0, -3.0}};
  const FaceSelector split_inside = [](const std::span<const bool> membership) {
    return membership.size() == 1 && membership[0];
  };
  const auto central_split = model.splitChords(
      {split_rectangle}, split_inside, SplitAxis{{0.0, 0.0}, {0.0, 1.0}});
  assert(central_split.status == signet::geometry::SplitStatus::success);
  assert(central_split.chords.size() == 1);
  assert((central_split.chords.front().source_curve_indices ==
          std::vector<std::size_t>{0, 2}));
  assert(central_split.chords.front().side == signet::geometry::SplitChordSide::positive);
  assert(central_split.cells.size() == 2);
  for (const auto& cell : central_split.cells) {
    assert(cell.face_id != 0);
    assert(cell.outer_boundary.closed);
    assert(!cell.outer_boundary.edges.empty());
    assert(cell.outer_boundary.edges.back().end == cell.outer_boundary.edges.front().start);
    assert(cell.holes.empty());
    assert(cell.side == signet::geometry::SplitChordSide::negative ||
           cell.side == signet::geometry::SplitChordSide::positive);
    assert(!cell.boundary_provenance.empty());
    for (const auto& edge : cell.outer_boundary.edges) {
      for (const std::size_t split_index : edge.split_chord_indices) {
        assert(split_index < central_split.chords.size());
      }
    }
    assert(std::any_of(
        cell.boundary_provenance.begin(), cell.boundary_provenance.end(),
        [](const auto& loop) {
          return std::any_of(
              loop.begin(), loop.end(), [](const auto& token) {
                return token.kind ==
                           signet::geometry::RegionBoundaryProvenanceKind::split_chord &&
                       token.split_chord_source_curve_indices ==
                           std::vector<std::size_t>{0, 2} &&
                       token.split_chord_operand_indices == std::vector<std::size_t>{0, 0};
              });
        }));
  }
  const auto repeated_central_split = model.splitChords(
      {split_rectangle}, split_inside, SplitAxis{{0.0, 0.0}, {0.0, 1.0}});
  assert(repeated_central_split == central_split);
  const auto reversed_axis_split = model.splitChords(
      {split_rectangle}, split_inside, SplitAxis{{0.0, 0.0}, {0.0, -1.0}});
  assert(reversed_axis_split == central_split);

  // Full circles use exact one-root intersections, including the same
  // source curve at both ends of a material chord.
  const OperandGroup split_circle{CircleInput{0.0, 0.0, 5.0}};
  const auto circle_split = model.splitChords(
      {split_circle}, split_inside, SplitAxis{{0.0, 0.0}, {1.0, 0.0}});
  assert(circle_split.status == signet::geometry::SplitStatus::success);
  assert(circle_split.chords.size() == 1);
  assert((circle_split.chords.front().source_curve_indices ==
          std::vector<std::size_t>{0, 0}));
  assert(circle_split.cells.size() == 2);
  assert(std::all_of(
      circle_split.cells.begin(), circle_split.cells.end(), [](const auto& cell) {
        return cell.holes.empty() && cell.outer_boundary.closed &&
               cell.boundary_provenance.size() == 1;
      }));
  const auto reversed_circle_split = model.splitChords(
      {split_circle}, split_inside, SplitAxis{{0.0, 0.0}, {-1.0, 0.0}});
  assert(reversed_circle_split.status == circle_split.status);
  assert(reversed_circle_split.chords == circle_split.chords);
  assert(reversed_circle_split.cells.size() == circle_split.cells.size());
  for (std::size_t index = 0; index < circle_split.cells.size(); ++index) {
    assert(reversed_circle_split.cells[index].boundary_provenance ==
           circle_split.cells[index].boundary_provenance);
  }
  const auto tangent_split = model.splitChords(
      {split_circle}, split_inside, SplitAxis{{0.0, 5.0}, {1.0, 0.0}});
  assert(tangent_split.status == signet::geometry::SplitStatus::tangent);
  assert(tangent_split.chords.empty());
  assert(tangent_split.cells.size() == 1);
  assert(tangent_split.cells.front().holes.empty());
  const auto circle_nonintersection = model.splitChords(
      {split_circle}, split_inside, SplitAxis{{0.0, 6.0}, {1.0, 0.0}});
  assert(circle_nonintersection.status == signet::geometry::SplitStatus::nonintersection);
  assert(circle_nonintersection.chords.empty());
  assert(circle_nonintersection.cells.size() == 1);
  assert(circle_nonintersection.cells.front().holes.empty());

  // A partial arc may be a boundary edge of a closed contour.  Its exact
  // endpoint is deduplicated with the connected segment before status checks.
  const OperandGroup arc_contour{
      ArcInput{-4.0, 0.0, 0.0, 4.0, 4.0, 0.0},
      SegmentInput{4.0, 0.0, 4.0, -4.0},
      SegmentInput{4.0, -4.0, -4.0, -4.0},
      SegmentInput{-4.0, -4.0, -4.0, 0.0}};
  const auto arc_split = model.splitChords(
      {arc_contour}, split_inside, SplitAxis{{0.0, 0.0}, {0.0, 1.0}});
  assert(arc_split.status == signet::geometry::SplitStatus::success);
  assert(arc_split.chords.size() == 1);
  assert((arc_split.chords.front().source_curve_indices == std::vector<std::size_t>{0, 2}));
  const auto arc_endpoint_split = model.splitChords(
      {arc_contour}, split_inside, SplitAxis{{-4.0, 0.0}, {1.0, 1.0}});
  assert(arc_endpoint_split.status == signet::geometry::SplitStatus::vertex_touch);
  assert(arc_endpoint_split.chords.empty());

  // Difference through a hole produces two independent material intervals
  // on one axis, while disconnected groups do the same for union and xor.
  const FaceSelector split_difference = [](const std::span<const bool> membership) {
    return membership.size() == 2 && membership[0] && !membership[1];
  };
  const auto hole_split = model.splitChords(
      {OperandGroup{outer_rectangle.begin(), outer_rectangle.end()},
       OperandGroup{inner_rectangle.begin(), inner_rectangle.end()}},
      split_difference,
      SplitAxis{{0.0, 0.0}, {1.0, 0.0}});
  assert(hole_split.status == signet::geometry::SplitStatus::success);
  assert(hole_split.chords.size() == 2);
  assert(hole_split.cells.size() == 2);
  assert(std::all_of(
      hole_split.cells.begin(), hole_split.cells.end(), [](const auto& cell) {
        return cell.outer_boundary.closed && cell.holes.empty() &&
               cell.boundary_provenance.size() == 1;
      }));
  assert((hole_split.chords[0].source_curve_indices == std::vector<std::size_t>{3, 7}));
  assert((hole_split.chords[1].source_curve_indices == std::vector<std::size_t>{1, 5}));
  const auto reversed_hole_split = model.splitChords(
      {OperandGroup{outer_rectangle.begin(), outer_rectangle.end()},
       OperandGroup{inner_rectangle.begin(), inner_rectangle.end()}},
      split_difference,
      SplitAxis{{0.0, 0.0}, {-1.0, 0.0}});
  assert(reversed_hole_split == hole_split);

  const auto hole_preserving_split = model.splitChords(
      {OperandGroup{outer_rectangle.begin(), outer_rectangle.end()},
       OperandGroup{inner_rectangle.begin(), inner_rectangle.end()}},
      split_difference,
      SplitAxis{{3.0, 0.0}, {0.0, 1.0}});
  assert(hole_preserving_split.status == signet::geometry::SplitStatus::success);
  assert(hole_preserving_split.chords.size() == 1);
  assert(hole_preserving_split.cells.size() == 2);
  assert(std::count_if(
             hole_preserving_split.cells.begin(), hole_preserving_split.cells.end(),
             [](const auto& cell) { return cell.holes.size() == 1; }) == 1);
  assert(std::all_of(
      hole_preserving_split.cells.begin(), hole_preserving_split.cells.end(),
      [](const auto& cell) {
        return cell.outer_boundary.closed &&
               (cell.holes.empty() ||
                std::all_of(cell.holes.begin(), cell.holes.end(),
                            [](const auto& hole) { return hole.closed && !hole.edges.empty(); }));
      }));

  const std::vector<OperandGroup> disconnected_circles{
      {CircleInput{-6.0, 0.0, 2.0}}, {CircleInput{6.0, 0.0, 2.0}}};
  const FaceSelector split_disconnected = [](const std::span<const bool> membership) {
    return membership.size() == 2 && (membership[0] || membership[1]);
  };
  const auto disconnected_union_split = model.splitChords(
      disconnected_circles, split_disconnected, SplitAxis{{0.0, 0.0}, {1.0, 0.0}});
  assert(disconnected_union_split.status == signet::geometry::SplitStatus::success);
  assert(disconnected_union_split.chords.size() == 2);
  assert(disconnected_union_split.cells.size() == 4);
  assert(std::all_of(
      disconnected_union_split.cells.begin(), disconnected_union_split.cells.end(),
      [](const auto& cell) { return cell.holes.empty() && cell.outer_boundary.closed; }));
  assert((disconnected_union_split.chords[0].source_curve_indices ==
          std::vector<std::size_t>{0, 0}));
  assert((disconnected_union_split.chords[1].source_curve_indices ==
          std::vector<std::size_t>{1, 1}));
  const FaceSelector split_xor = [](const std::span<const bool> membership) {
    return membership.size() == 2 && (membership[0] != membership[1]);
  };
  const auto disconnected_xor_split =
      model.splitChords(disconnected_circles, split_xor, SplitAxis{{0.0, 0.0}, {1.0, 0.0}});
  assert(disconnected_xor_split == disconnected_union_split);

  const auto split_state_before_rejection = model.evaluateBoolean(BooleanOperation::unite);
  const auto split_source_count_before_rejection = model.sourceCurveCount();
  const auto split_face_count_before_rejection = model.faceCount();
  const FaceSelector split_ambiguous = [](const std::span<const bool> membership) {
    return membership.size() == 2 && membership[0] && !membership[1];
  };
  const auto ambiguous_split = model.splitChords(
      {OperandGroup{outer_rectangle.begin(), outer_rectangle.end()},
       OperandGroup{CircleInput{0.0, 1.0, 1.0}}},
      split_ambiguous,
      SplitAxis{{0.0, 0.0}, {1.0, 0.0}});
  assert(ambiguous_split.status == signet::geometry::SplitStatus::branch_ambiguity);
  assert(ambiguous_split.chords.empty());
  assert(model.sourceCurveCount() == split_source_count_before_rejection);
  assert(model.faceCount() == split_face_count_before_rejection);
  assert(model.evaluateBoolean(BooleanOperation::unite) == split_state_before_rejection);

  const auto source_count_before_split_failure = model.sourceCurveCount();
  const auto face_count_before_split_failure = model.faceCount();
  const auto nonintersection = model.splitChords(
      {split_rectangle}, split_inside, SplitAxis{{0.0, 10.0}, {1.0, 0.0}});
  assert(nonintersection.status == signet::geometry::SplitStatus::nonintersection);
  assert(nonintersection.chords.empty());
  assert(model.sourceCurveCount() == source_count_before_split_failure);
  assert(model.faceCount() == face_count_before_split_failure);

  const auto coincident = model.splitChords(
      {split_rectangle}, split_inside, SplitAxis{{-4.0, 0.0}, {0.0, 1.0}});
  assert(coincident.status == signet::geometry::SplitStatus::boundary_coincident);
  const auto vertex_touch = model.splitChords(
      {split_rectangle}, split_inside, SplitAxis{{-4.0, -3.0}, {1.0, 1.0}});
  assert(vertex_touch.status == signet::geometry::SplitStatus::vertex_touch);
  const auto invalid_split = model.splitChords(
      {}, split_inside, SplitAxis{{0.0, 0.0}, {1.0, 0.0}});
  assert(invalid_split.status == signet::geometry::SplitStatus::invalid_input);
  assert(model.sourceCurveCount() == source_count_before_split_failure);
  assert(model.faceCount() == face_count_before_split_failure);
  model.setBooleanOperandGroups({operand_c, operand_b, operand_a});

  // Invalid groups are rejected before any model state is changed.
  const auto state_before_invalid_group = model.evaluateBoolean(all_three_selector);
  const auto source_count_before_invalid_group = model.sourceCurveCount();
  const auto face_count_before_invalid_group = model.faceCount();
  bool rejected_invalid_group = false;
  try {
    model.setBooleanOperandGroups({
        operand_a,
        {SegmentInput{-1.0, 0.0, 1.0, 0.0}},
        operand_c,
    });
  } catch (const std::invalid_argument&) {
    rejected_invalid_group = true;
  }
  assert(rejected_invalid_group);
  assert(model.sourceCurveCount() == source_count_before_invalid_group);
  assert(model.faceCount() == face_count_before_invalid_group);
  assert(model.evaluateBoolean(all_three_selector) == state_before_invalid_group);
}
