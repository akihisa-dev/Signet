// SPDX-License-Identifier: AGPL-3.0-or-later
#include "geometry/document_evaluator.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <numbers>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>

namespace signet::geometry {

namespace {

bool finite(const double value) { return std::isfinite(value); }

std::optional<core::Point> transformPoint(
    const core::Point point,
    const core::Transform& transform) {
  const double scaled_x = point.x * transform.scale.x;
  const double scaled_y = point.y * transform.scale.y;
  const double radians = transform.rotation_degrees * std::numbers::pi_v<double> / 180.0;
  const double cosine = std::cos(radians);
  const double sine = std::sin(radians);
  const core::Point result{
      transform.translation.x + scaled_x * cosine - scaled_y * sine,
      transform.translation.y + scaled_x * sine + scaled_y * cosine};
  if (!finite(result.x) || !finite(result.y)) {
    return std::nullopt;
  }
  return result;
}

core::Point circlePoint(const double radius, const double degrees) {
  double normalized = std::fmod(degrees, 360.0);
  if (normalized < 0.0) {
    normalized += 360.0;
  }
  if (normalized == 0.0) {
    return core::Point{radius, 0.0};
  }
  if (normalized == 90.0) {
    return core::Point{0.0, radius};
  }
  if (normalized == 180.0) {
    return core::Point{-radius, 0.0};
  }
  if (normalized == 270.0) {
    return core::Point{0.0, -radius};
  }
  const double radians = normalized * std::numbers::pi_v<double> / 180.0;
  return core::Point{radius * std::cos(radians), radius * std::sin(radians)};
}

std::optional<EvaluatedCurveSet> evaluateRectangle(
    const core::NodeId node_id,
    const double width,
    const double height,
    const core::Transform& transform) {
  const std::array<core::Point, 4> corners{
      core::Point{-width / 2.0, -height / 2.0},
      core::Point{width / 2.0, -height / 2.0},
      core::Point{width / 2.0, height / 2.0},
      core::Point{-width / 2.0, height / 2.0}};

  EvaluatedCurveSet result{node_id, {}};
  result.curves.reserve(corners.size());
  for (std::size_t index = 0; index < corners.size(); ++index) {
    const auto source = transformPoint(corners[index], transform);
    const auto target = transformPoint(corners[(index + 1) % corners.size()], transform);
    if (!source.has_value() || !target.has_value()) {
      return std::nullopt;
    }
    result.curves.emplace_back(SegmentInput{
        source->x, source->y, target->x, target->y});
  }
  return result;
}

std::optional<EvaluatedCurveSet> evaluateArc(
    const core::NodeId node_id,
    const core::Arc& arc,
    const core::Transform& transform) {
  const double scale_x = std::abs(transform.scale.x);
  const double scale_y = std::abs(transform.scale.y);
  if (scale_x != scale_y) {
    return std::nullopt;
  }

  const double scale = scale_x;
  if (std::abs(arc.sweep_degrees) == 360.0) {
    if (!finite(arc.radius * scale)) {
      return std::nullopt;
    }
    return EvaluatedCurveSet{
        node_id,
        {CircleInput{transform.translation.x, transform.translation.y, arc.radius * scale}}};
  }

  const auto pointAt = [&](const double degrees) {
    return transformPoint(circlePoint(arc.radius, degrees), transform);
  };
  const auto source = pointAt(arc.start_degrees);
  const auto interior = pointAt(arc.start_degrees + arc.sweep_degrees / 2.0);
  const auto target = pointAt(arc.start_degrees + arc.sweep_degrees);
  if (!source.has_value() || !interior.has_value() || !target.has_value()) {
    return std::nullopt;
  }

  return EvaluatedCurveSet{
      node_id,
      {ArcInput{
          source->x,
          source->y,
          interior->x,
          interior->y,
          target->x,
          target->y}}};
}

using EvaluatedGeometry = std::variant<EvaluatedCircle, EvaluatedCurveSet>;

std::optional<core::Point> reflectPoint(
    const core::Point point,
    const core::SymmetryAxis& axis) {
  // Scale the direction before forming the projection to avoid avoidable
  // overflow in direction.x * direction.x for finite input.
  const double direction_scale =
      std::max(std::abs(axis.direction.x), std::abs(axis.direction.y));
  if (!finite(direction_scale) || direction_scale == 0.0) {
    return std::nullopt;
  }
  const double direction_x = axis.direction.x / direction_scale;
  const double direction_y = axis.direction.y / direction_scale;
  const double relative_x = point.x - axis.origin.x;
  const double relative_y = point.y - axis.origin.y;
  const double denominator = direction_x * direction_x + direction_y * direction_y;
  const double projection_scale =
      2.0 * (relative_x * direction_x + relative_y * direction_y) / denominator;
  const core::Point reflected{
      axis.origin.x + projection_scale * direction_x - relative_x,
      axis.origin.y + projection_scale * direction_y - relative_y};
  if (!finite(reflected.x) || !finite(reflected.y)) {
    return std::nullopt;
  }
  return reflected;
}

std::optional<std::string> reflectionAxisDiagnostic(const core::SymmetryAxis& axis) {
  if (!finite(axis.origin.x) || !finite(axis.origin.y) ||
      !finite(axis.direction.x) || !finite(axis.direction.y)) {
    return "Reflection axis is invalid: origin and direction must be finite";
  }
  if (axis.direction.x == 0.0 && axis.direction.y == 0.0) {
    return "Reflection axis is degenerate: direction must not be zero";
  }
  return std::nullopt;
}

std::optional<CurveInput> reflectCurve(
    const CurveInput& curve,
    const core::SymmetryAxis& axis) {
  return std::visit(
      [&axis](const auto& input) -> std::optional<CurveInput> {
        using Input = std::decay_t<decltype(input)>;
        if constexpr (std::is_same_v<Input, CircleInput>) {
          const auto center = reflectPoint(
              core::Point{input.center_x, input.center_y}, axis);
          if (!center.has_value()) {
            return std::nullopt;
          }
          return CurveInput{CircleInput{center->x, center->y, input.radius}};
        } else if constexpr (std::is_same_v<Input, SegmentInput>) {
          const auto source = reflectPoint(
              core::Point{input.source_x, input.source_y}, axis);
          const auto target = reflectPoint(
              core::Point{input.target_x, input.target_y}, axis);
          if (!source.has_value() || !target.has_value()) {
            return std::nullopt;
          }
          return CurveInput{SegmentInput{
              source->x, source->y, target->x, target->y}};
        } else {
          const auto source = reflectPoint(
              core::Point{input.source_x, input.source_y}, axis);
          const auto interior = reflectPoint(
              core::Point{input.interior_x, input.interior_y}, axis);
          const auto target = reflectPoint(
              core::Point{input.target_x, input.target_y}, axis);
          if (!source.has_value() || !interior.has_value() || !target.has_value()) {
            return std::nullopt;
          }
          // Keep source/interior/target order; the three-point arc then has
          // the naturally reversed orientation after reflection.
          return CurveInput{ArcInput{
              source->x,
              source->y,
              interior->x,
              interior->y,
              target->x,
              target->y}};
        }
      },
      curve);
}

std::optional<EvaluatedGeometry> reflectGeometry(
    const EvaluatedGeometry& source,
    const core::NodeId node_id,
    const core::SymmetryAxis& axis) {
  return std::visit(
      [node_id, &axis](const auto& geometry) -> std::optional<EvaluatedGeometry> {
        using Geometry = std::decay_t<decltype(geometry)>;
        if constexpr (std::is_same_v<Geometry, EvaluatedCircle>) {
          const auto center = reflectPoint(
              core::Point{geometry.circle.center_x, geometry.circle.center_y}, axis);
          if (!center.has_value()) {
            return std::nullopt;
          }
          return EvaluatedGeometry{EvaluatedCircle{
              node_id,
              CircleInput{center->x, center->y, geometry.circle.radius}}};
        } else {
          EvaluatedCurveSet reflected{node_id, {}};
          reflected.curves.reserve(geometry.curves.size());
          for (const auto& curve : geometry.curves) {
            const auto result = reflectCurve(curve, axis);
            if (!result.has_value()) {
              return std::nullopt;
            }
            reflected.curves.push_back(*result);
          }
          return EvaluatedGeometry{std::move(reflected)};
        }
      },
      source);
}

std::optional<EvaluatedGeometry> evaluatePrimitive(
    const core::NodeId node_id,
    const core::Primitive& primitive,
    const core::Transform& transform,
    std::string& reason) {
  return std::visit(
      [&](const auto& value) -> std::optional<EvaluatedGeometry> {
        using Primitive = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Primitive, core::Circle>) {
          const double scale_x = std::abs(transform.scale.x);
          const double scale_y = std::abs(transform.scale.y);
          if (scale_x != scale_y) {
            reason = "Circle with non-uniform scale is not evaluated because it becomes an ellipse";
            return std::nullopt;
          }
          const double radius = value.radius * scale_x;
          if (!finite(radius) || !finite(transform.translation.x) ||
              !finite(transform.translation.y)) {
            reason = "Circle evaluation produced non-finite geometry";
            return std::nullopt;
          }
          return EvaluatedGeometry{EvaluatedCircle{
              node_id,
              CircleInput{transform.translation.x, transform.translation.y, radius}}};
        } else if constexpr (std::is_same_v<Primitive, core::Rectangle>) {
          const auto evaluated = evaluateRectangle(node_id, value.width, value.height, transform);
          if (!evaluated.has_value()) {
            reason = "Rectangle evaluation produced non-finite geometry";
            return std::nullopt;
          }
          return EvaluatedGeometry{*evaluated};
        } else if constexpr (std::is_same_v<Primitive, core::GoldenRectangle>) {
          const auto evaluated = evaluateRectangle(
              node_id, value.longSide(), value.short_side, transform);
          if (!evaluated.has_value()) {
            reason = "GoldenRectangle evaluation produced non-finite geometry";
            return std::nullopt;
          }
          return EvaluatedGeometry{*evaluated};
        } else {
          const double scale_x = std::abs(transform.scale.x);
          const double scale_y = std::abs(transform.scale.y);
          if (scale_x != scale_y) {
            reason = "Arc with non-uniform scale is not evaluated because it becomes an ellipse";
            return std::nullopt;
          }
          const auto evaluated = evaluateArc(node_id, value, transform);
          if (!evaluated.has_value()) {
            reason = "Arc evaluation produced non-finite geometry";
            return std::nullopt;
          }
          return EvaluatedGeometry{*evaluated};
        }
      },
      primitive);
}

void appendGeometry(
    DocumentEvaluationSnapshot& snapshot,
    std::unordered_map<core::NodeId, EvaluatedGeometry>& evaluated,
    EvaluatedGeometry geometry) {
  std::visit(
      [&snapshot, &evaluated](const auto& value) {
        using Geometry = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Geometry, EvaluatedCircle>) {
          snapshot.circles.push_back(value);
        } else {
          snapshot.curve_sets.push_back(value);
        }
        evaluated.insert_or_assign(value.node_id, EvaluatedGeometry{value});
      },
      std::move(geometry));
}

std::string unsupportedReflectionSource(const core::Node* source_node) {
  if (source_node == nullptr) {
    return "Reflection source node does not reference an existing node";
  }
  if (std::holds_alternative<core::BooleanNode>(source_node->definition)) {
    return "Reflection source Boolean node is unsupported";
  }
  if (std::holds_alternative<core::SplitNode>(source_node->definition)) {
    return "Reflection source Split node is unsupported";
  }
  if (std::holds_alternative<core::RegionSelectionNode>(source_node->definition)) {
    return "Reflection source Region selection node is unsupported";
  }
  return "Reflection source node has no evaluated geometry";
}

void addDiagnostic(
    DocumentEvaluationSnapshot& snapshot,
    const core::NodeId node_id,
    std::string reason) {
  snapshot.diagnostics.push_back(EvaluationDiagnostic{node_id, std::move(reason)});
}

struct BooleanExpressionNode final {
  enum class Kind { leaf, operation };

  Kind kind{Kind::leaf};
  std::size_t leaf_index{};
  core::BooleanOperation operation{core::BooleanOperation::unite};
  std::size_t left{};
  std::size_t right{};
};

struct BooleanExpansion final {
  std::vector<OperandGroup> operands;
  std::vector<BooleanExpressionNode> expression;
  std::vector<core::RegionExpressionTerm> construction_expression;
  struct SourceCurve final {
    core::NodeId node_id{};
    std::uint32_t curve_index{};
    std::size_t operand_index{};
  };
  std::vector<SourceCurve> source_curves;
  std::unordered_map<core::NodeId, std::size_t> leaf_indices;
};

std::optional<EvaluatedCurveSet> geometryCurveSet(
    const core::NodeId node_id,
    const EvaluatedGeometry& geometry) {
  return std::visit(
      [node_id](const auto& value) -> std::optional<EvaluatedCurveSet> {
        using Geometry = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Geometry, EvaluatedCircle>) {
          return EvaluatedCurveSet{node_id, {value.circle}};
        } else {
          return EvaluatedCurveSet{node_id, value.curves};
        }
      },
      geometry);
}

std::string unsupportedBooleanLeaf(
    const core::NodeId node_id,
    const core::Node* node) {
  if (node == nullptr) {
    return "Boolean operand leaf NodeId " + std::to_string(node_id) +
           " does not reference an existing node";
  }
  if (std::holds_alternative<core::SplitNode>(node->definition)) {
    return "Boolean operand leaf NodeId " + std::to_string(node_id) +
           " is a Split node; Split results are unsupported";
  }
  if (std::holds_alternative<core::RegionSelectionNode>(node->definition)) {
    return "Boolean operand leaf NodeId " + std::to_string(node_id) +
           " is a Region selection node; Region selection results are unsupported";
  }
  return "Boolean operand leaf NodeId " + std::to_string(node_id) +
         " has no evaluated closed curve set";
}

std::optional<std::size_t> appendBooleanExpression(
    const core::Document& document,
    const core::NodeId node_id,
    const std::unordered_map<core::NodeId, EvaluatedGeometry>& evaluated,
    BooleanExpansion& expansion,
    std::string& reason) {
  const auto* node = document.findNode(node_id);
  if (node == nullptr) {
    reason = unsupportedBooleanLeaf(node_id, node);
    return std::nullopt;
  }

  if (const auto* boolean = std::get_if<core::BooleanNode>(&node->definition);
      boolean != nullptr) {
    const auto left = appendBooleanExpression(
        document, boolean->left, evaluated, expansion, reason);
    if (!left.has_value()) {
      return std::nullopt;
    }
    const auto right = appendBooleanExpression(
        document, boolean->right, evaluated, expansion, reason);
    if (!right.has_value()) {
      return std::nullopt;
    }
    expansion.expression.push_back(BooleanExpressionNode{
        BooleanExpressionNode::Kind::operation,
        0,
        boolean->operation,
        *left,
        *right});
    expansion.construction_expression.push_back(core::RegionExpressionTerm{
        core::RegionExpressionTerm::Kind::boolean,
        0,
        boolean->operation});
    return expansion.expression.size() - 1;
  }

  if (!std::holds_alternative<core::PrimitiveNode>(node->definition) &&
      !std::holds_alternative<core::SymmetryNode>(node->definition)) {
    reason = unsupportedBooleanLeaf(node_id, node);
    return std::nullopt;
  }

  const auto found_leaf = expansion.leaf_indices.find(node_id);
  if (found_leaf != expansion.leaf_indices.end()) {
    expansion.expression.push_back(BooleanExpressionNode{
        BooleanExpressionNode::Kind::leaf,
        found_leaf->second});
    expansion.construction_expression.push_back(core::RegionExpressionTerm{
        core::RegionExpressionTerm::Kind::leaf,
        node_id});
    return expansion.expression.size() - 1;
  }

  const auto found_geometry = evaluated.find(node_id);
  if (found_geometry == evaluated.end()) {
    reason = unsupportedBooleanLeaf(node_id, node);
    return std::nullopt;
  }
  const auto curve_set = geometryCurveSet(node_id, found_geometry->second);
  if (!curve_set.has_value() || curve_set->curves.empty()) {
    reason = unsupportedBooleanLeaf(node_id, node);
    return std::nullopt;
  }

  const std::size_t leaf_index = expansion.operands.size();
  expansion.operands.push_back(curve_set->curves);
  if (curve_set->curves.size() >
      static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
    reason = "Boolean operand leaf NodeId " + std::to_string(node_id) +
             " has too many source curves for RegionKey provenance";
    expansion.operands.pop_back();
    return std::nullopt;
  }
  for (std::size_t curve_index = 0; curve_index < curve_set->curves.size(); ++curve_index) {
    expansion.source_curves.push_back(BooleanExpansion::SourceCurve{
        node_id,
        static_cast<std::uint32_t>(curve_index),
        leaf_index});
  }
  expansion.leaf_indices.emplace(node_id, leaf_index);
  expansion.expression.push_back(BooleanExpressionNode{
      BooleanExpressionNode::Kind::leaf,
      leaf_index});
  expansion.construction_expression.push_back(core::RegionExpressionTerm{
      core::RegionExpressionTerm::Kind::leaf,
      node_id});
  return expansion.expression.size() - 1;
}

bool evaluateBooleanExpression(
    const std::vector<BooleanExpressionNode>& expression,
    const std::size_t expression_index,
    const std::span<const bool> membership) {
  const auto& node = expression.at(expression_index);
  if (node.kind == BooleanExpressionNode::Kind::leaf) {
    return membership[node.leaf_index];
  }

  const bool left = evaluateBooleanExpression(expression, node.left, membership);
  const bool right = evaluateBooleanExpression(expression, node.right, membership);
  switch (node.operation) {
    case core::BooleanOperation::unite:
      return left || right;
    case core::BooleanOperation::intersect:
      return left && right;
    case core::BooleanOperation::subtract:
      return left && !right;
    case core::BooleanOperation::exclusive_or:
      return left != right;
  }
  return false;
}

std::optional<std::string> openArcDiagnostic(
    const BooleanExpansion& expansion,
    const std::string_view operation_name) {
  for (std::size_t index = 0; index < expansion.operands.size(); ++index) {
    const auto& operand = expansion.operands[index];
    if (operand.size() == 1 && std::holds_alternative<ArcInput>(operand.front())) {
      const auto found = std::ranges::find_if(
          expansion.leaf_indices,
          [index](const auto& entry) { return entry.second == index; });
      if (found != expansion.leaf_indices.end()) {
        return std::string(operation_name) + " input leaf NodeId " +
               std::to_string(found->first) +
               " is an open Arc; only full-circle Arc is supported";
      }
    }
  }
  return std::nullopt;
}

core::RegionCutterSide regionCutterSide(const SplitChordSide side) {
  switch (side) {
    case SplitChordSide::negative:
      return core::RegionCutterSide::negative;
    case SplitChordSide::positive:
      return core::RegionCutterSide::positive;
    case SplitChordSide::on_axis:
      return core::RegionCutterSide::on_boundary;
  }
  return core::RegionCutterSide::on_boundary;
}

std::string splitStatusDiagnostic(const SplitStatus status) {
  switch (status) {
    case SplitStatus::invalid_input:
      return "Split evaluation failed: invalid input";
    case SplitStatus::success:
      return {};
    case SplitStatus::nonintersection:
      return "Split axis does not intersect the selected material";
    case SplitStatus::tangent:
      return "Split axis is tangent to the selected material";
    case SplitStatus::boundary_coincident:
      return "Split axis is coincident with a source boundary";
    case SplitStatus::vertex_touch:
      return "Split axis touches a source vertex or shared boundary";
    case SplitStatus::odd_intersections:
      return "Split axis produced an odd number of material intersections";
    case SplitStatus::branch_ambiguity:
      return "Split evaluation failed: branch ambiguity";
  }
  return "Split evaluation failed: unknown status";
}

std::optional<core::RegionBoundaryProvenance> sourceProvenance(
    const std::size_t source_curve_index,
    const std::size_t operand_index,
    const std::vector<BooleanExpansion::SourceCurve>& source_curves) {
  if (source_curve_index >= source_curves.size()) {
    return std::nullopt;
  }
  const auto& source = source_curves[source_curve_index];
  if (source.operand_index != operand_index) {
    return std::nullopt;
  }
  return core::RegionBoundaryProvenance{source.node_id, source.curve_index};
}

std::optional<core::RegionBoundaryLoop> mapRegionBoundaryLoop(
    const RegionBoundaryProvenanceLoop& loop,
    const std::vector<BooleanExpansion::SourceCurve>& source_curves,
    const std::vector<SplitChord>& chords) {
  core::RegionBoundaryLoop mapped;
  for (const auto& token : loop) {
    if (token.kind == RegionBoundaryProvenanceKind::source_curve) {
      const auto provenance = sourceProvenance(
          token.source_curve_index, token.operand_index, source_curves);
      if (!provenance.has_value()) {
        return std::nullopt;
      }
      mapped.push_back(*provenance);
      continue;
    }

    if (token.split_chord_index >= chords.size()) {
      return std::nullopt;
    }
    const auto& chord = chords[token.split_chord_index];
    if (token.split_chord_source_curve_indices != chord.source_curve_indices) {
      return std::nullopt;
    }
    if (token.split_chord_operand_indices.size() !=
        token.split_chord_source_curve_indices.size()) {
      return std::nullopt;
    }
    for (std::size_t index = 0;
         index < token.split_chord_source_curve_indices.size();
         ++index) {
      const auto provenance = sourceProvenance(
          token.split_chord_source_curve_indices[index],
          token.split_chord_operand_indices[index],
          source_curves);
      if (!provenance.has_value()) {
        return std::nullopt;
      }
      mapped.push_back(*provenance);
    }
  }
  return mapped;
}

std::optional<EvaluatedRegion> evaluateRegion(
    const RegionCell& cell,
    const core::NodeId split_node_id,
    const std::vector<core::RegionExpressionTerm>& construction_expression,
    const std::vector<BooleanExpansion::SourceCurve>& source_curves,
    const std::vector<SplitChord>& chords) {
  core::RegionKey key;
  key.split_node_id = split_node_id;
  key.construction_expression = construction_expression;
  key.cutter_side = regionCutterSide(cell.side);
  key.boundary_provenance.reserve(cell.boundary_provenance.size());
  for (const auto& loop : cell.boundary_provenance) {
    const auto mapped = mapRegionBoundaryLoop(loop, source_curves, chords);
    if (!mapped.has_value()) {
      return std::nullopt;
    }
    key.boundary_provenance.push_back(*mapped);
  }
  std::ranges::sort(key.boundary_provenance);
  return EvaluatedRegion{std::move(key), cell};
}

std::vector<core::RegionKey> canonicalRegionKeys(
    std::vector<core::RegionKey> keys) {
  std::ranges::sort(keys);
  const auto unique = std::ranges::unique(keys);
  keys.erase(unique.begin(), unique.end());
  return keys;
}

const EvaluatedSplit* evaluatedSplit(
    const DocumentEvaluationSnapshot& snapshot,
    const core::NodeId node_id) {
  const auto found = std::ranges::find_if(
      snapshot.splits,
      [node_id](const EvaluatedSplit& split) { return split.node_id == node_id; });
  return found == snapshot.splits.end() ? nullptr : &*found;
}

const EvaluatedRegionSelection* evaluatedRegionSelection(
    const DocumentEvaluationSnapshot& snapshot,
    const core::NodeId node_id) {
  const auto found = std::ranges::find_if(
      snapshot.region_selections,
      [node_id](const EvaluatedRegionSelection& selection) {
        return selection.node_id == node_id;
      });
  return found == snapshot.region_selections.end() ? nullptr : &*found;
}

}  // namespace

DocumentEvaluationSnapshot DocumentEvaluator::evaluate(const core::Document& document) {
  DocumentEvaluationSnapshot snapshot;
  std::unordered_map<core::NodeId, EvaluatedGeometry> evaluated;
  snapshot.circles.reserve(document.nodes().size());
  snapshot.curve_sets.reserve(document.nodes().size());
  snapshot.booleans.reserve(document.nodes().size());
  snapshot.splits.reserve(document.nodes().size());
  snapshot.region_selections.reserve(document.nodes().size());
  snapshot.region_filters.reserve(document.nodes().size());
  snapshot.diagnostics.reserve(document.nodes().size());
  evaluated.reserve(document.nodes().size());
  std::unordered_map<core::NodeId, bool> region_selection_valid;
  region_selection_valid.reserve(document.nodes().size());

  for (const auto& node : document.nodes()) {
    std::visit(
        [&snapshot,
         &document,
         &evaluated,
         &region_selection_valid,
         node_id = node.id](const auto& definition) {
          using Definition = std::decay_t<decltype(definition)>;
          if constexpr (std::is_same_v<Definition, core::PrimitiveNode>) {
            std::string reason;
            const auto geometry = evaluatePrimitive(
                node_id, definition.primitive, definition.transform, reason);
            if (geometry.has_value()) {
              appendGeometry(snapshot, evaluated, *geometry);
            } else {
              addDiagnostic(snapshot, node_id, std::move(reason));
            }
          } else if constexpr (std::is_same_v<Definition, core::BooleanNode>) {
            BooleanExpansion expansion;
            expansion.operands.reserve(document.nodes().size());
            expansion.expression.reserve(document.nodes().size());
            expansion.leaf_indices.reserve(document.nodes().size());
            std::string reason;
            const auto expression_root = appendBooleanExpression(
                document, node_id, evaluated, expansion, reason);
            if (!expression_root.has_value()) {
              addDiagnostic(
                  snapshot,
                  node_id,
                  "Boolean operation requires closed primitive leaves: " + reason);
              return;
            }
            if (const auto open_arc = openArcDiagnostic(expansion, "Boolean");
                open_arc.has_value()) {
              addDiagnostic(snapshot, node_id, *open_arc);
              return;
            }
            ArrangementModel model;
            try {
              model.setBooleanOperandGroups(std::move(expansion.operands));
              const auto selector = [&expression = expansion.expression, expression_root](
                                        const std::span<const bool> membership) {
                return evaluateBooleanExpression(expression, *expression_root, membership);
              };
              const auto boolean_snapshot = model.evaluateBoolean(selector);
              snapshot.booleans.push_back(
                  EvaluatedBoolean{node_id, definition.operation, boolean_snapshot.regions});
            } catch (const std::exception& error) {
              addDiagnostic(
                  snapshot,
                  node_id,
                  std::string("Boolean operation evaluation failed: ") + error.what());
            }
          } else if constexpr (std::is_same_v<Definition, core::SymmetryNode>) {
            if (const auto axis_diagnostic = reflectionAxisDiagnostic(definition.axis);
                axis_diagnostic.has_value()) {
              addDiagnostic(snapshot, node_id, *axis_diagnostic);
              return;
            }
            const auto source = evaluated.find(definition.input);
            if (source == evaluated.end()) {
              addDiagnostic(
                  snapshot,
                  node_id,
                  unsupportedReflectionSource(document.findNode(definition.input)));
              return;
            }
            const auto reflected = reflectGeometry(source->second, node_id, definition.axis);
            if (!reflected.has_value()) {
              addDiagnostic(
                  snapshot,
                  node_id,
                  "Reflection evaluation produced non-finite geometry");
              return;
            }
            appendGeometry(snapshot, evaluated, *reflected);
          } else if constexpr (std::is_same_v<Definition, core::SplitNode>) {
            EvaluatedSplit evaluated_split;
            evaluated_split.node_id = node_id;

            BooleanExpansion expansion;
            expansion.operands.reserve(document.nodes().size());
            expansion.expression.reserve(document.nodes().size());
            expansion.leaf_indices.reserve(document.nodes().size());
            std::string reason;
            const auto expression_root = appendBooleanExpression(
                document, definition.input, evaluated, expansion, reason);
            if (!expression_root.has_value()) {
              evaluated_split.status = SplitStatus::invalid_input;
              addDiagnostic(
                  snapshot,
                  node_id,
                  "Split operation requires closed primitive/symmetry/Boolean input: " +
                      reason);
              snapshot.splits.push_back(std::move(evaluated_split));
              return;
            }
            if (const auto open_arc = openArcDiagnostic(expansion, "Split");
                open_arc.has_value()) {
              evaluated_split.status = SplitStatus::invalid_input;
              addDiagnostic(snapshot, node_id, *open_arc);
              snapshot.splits.push_back(std::move(evaluated_split));
              return;
            }

            const auto selector = [&expression = expansion.expression, expression_root](
                                      const std::span<const bool> membership) {
              return evaluateBooleanExpression(expression, *expression_root, membership);
            };
            SplitEvaluationSnapshot split_snapshot;
            try {
              ArrangementModel model;
              split_snapshot = model.splitChords(
                  std::move(expansion.operands),
                  selector,
                  SplitAxis{
                      Point2{definition.axis.origin.x, definition.axis.origin.y},
                      Point2{definition.axis.direction.x, definition.axis.direction.y}});
            } catch (const std::exception& error) {
              evaluated_split.status = SplitStatus::branch_ambiguity;
              addDiagnostic(
                  snapshot,
                  node_id,
                  std::string("Split evaluation failed: ") + error.what());
              snapshot.splits.push_back(std::move(evaluated_split));
              return;
            }

            evaluated_split.status = split_snapshot.status;
            evaluated_split.chords = split_snapshot.chords;
            evaluated_split.cells.reserve(split_snapshot.cells.size());
            for (const auto& cell : split_snapshot.cells) {
              const auto evaluated_region = evaluateRegion(
                  cell,
                  node_id,
                  expansion.construction_expression,
                  expansion.source_curves,
                  split_snapshot.chords);
              if (!evaluated_region.has_value()) {
                evaluated_split.status = SplitStatus::branch_ambiguity;
                evaluated_split.chords.clear();
                evaluated_split.cells.clear();
                addDiagnostic(
                    snapshot,
                    node_id,
                    "Split evaluation failed: arrangement provenance could not be mapped to RegionKey");
                snapshot.splits.push_back(std::move(evaluated_split));
                return;
              }
              evaluated_split.cells.push_back(*evaluated_region);
            }
            std::stable_sort(
                evaluated_split.cells.begin(),
                evaluated_split.cells.end(),
                [](const EvaluatedRegion& left, const EvaluatedRegion& right) {
                  return left.key < right.key;
                });
            if (const auto diagnostic = splitStatusDiagnostic(evaluated_split.status);
                !diagnostic.empty()) {
              addDiagnostic(snapshot, node_id, diagnostic);
            }
            snapshot.splits.push_back(std::move(evaluated_split));
          } else if constexpr (std::is_same_v<Definition, core::RegionSelectionNode>) {
            EvaluatedRegionSelection evaluated_selection;
            evaluated_selection.node_id = node_id;
            evaluated_selection.input = definition.input;
            evaluated_selection.region_keys = canonicalRegionKeys(definition.region_keys);

            bool resolved = true;
            if (evaluated_selection.region_keys.empty()) {
              resolved = false;
              addDiagnostic(
                  snapshot,
                  node_id,
                  "Region selection requires at least one RegionKey");
            }

            const auto* split = evaluatedSplit(snapshot, definition.input);
            if (split == nullptr) {
              resolved = false;
              addDiagnostic(
                  snapshot,
                  node_id,
                  "Region selection input NodeId " + std::to_string(definition.input) +
                      " does not reference an evaluated Split node");
            }

            if (resolved) {
              evaluated_selection.cells.reserve(evaluated_selection.region_keys.size());
              for (const auto& key : evaluated_selection.region_keys) {
                const auto found = std::ranges::find_if(
                    split->cells,
                    [&key](const EvaluatedRegion& region) { return region.key == key; });
                if (found == split->cells.end()) {
                  resolved = false;
                  break;
                }
                evaluated_selection.cells.push_back(*found);
              }
            }

            if (!resolved) {
              evaluated_selection.cells.clear();
              if (split != nullptr && !evaluated_selection.region_keys.empty()) {
                addDiagnostic(
                    snapshot,
                    node_id,
                    "Region selection contains an unresolved RegionKey; "
                    "selection is not partially evaluated");
              }
            }
            region_selection_valid.insert_or_assign(node_id, resolved);
            snapshot.region_selections.push_back(std::move(evaluated_selection));
          } else if constexpr (std::is_same_v<Definition, core::RegionFilterNode>) {
            EvaluatedRegionFilter evaluated_filter;
            evaluated_filter.node_id = node_id;
            evaluated_filter.input = definition.input;
            evaluated_filter.selection = definition.selection;
            evaluated_filter.mode = definition.mode;

            bool valid = true;
            const auto* split = evaluatedSplit(snapshot, definition.input);
            if (split == nullptr) {
              valid = false;
              addDiagnostic(
                  snapshot,
                  node_id,
                  "Region filter input NodeId " + std::to_string(definition.input) +
                      " does not reference an evaluated Split node");
            }

            const auto* selection_node = document.findNode(definition.selection);
            const auto* selection_definition =
                selection_node == nullptr
                    ? nullptr
                    : std::get_if<core::RegionSelectionNode>(&selection_node->definition);
            if (selection_definition == nullptr) {
              valid = false;
              addDiagnostic(
                  snapshot,
                  node_id,
                  "Region filter selection NodeId " +
                      std::to_string(definition.selection) +
                      " does not reference a RegionSelection node");
            } else if (selection_definition->input != definition.input) {
              valid = false;
              addDiagnostic(
                  snapshot,
                  node_id,
                  "Region filter input does not match its RegionSelection input");
            }

            const auto* selection = evaluatedRegionSelection(snapshot, definition.selection);
            const auto selection_valid = region_selection_valid.find(definition.selection);
            if (selection == nullptr || selection_valid == region_selection_valid.end() ||
                !selection_valid->second) {
              valid = false;
              addDiagnostic(
                  snapshot,
                  node_id,
                  "Region filter selection is unresolved");
            }

            if (valid) {
              if (definition.mode == core::RegionFilterMode::keep_selected) {
                evaluated_filter.cells = selection->cells;
              } else if (definition.mode == core::RegionFilterMode::remove_selected) {
                evaluated_filter.cells.reserve(split->cells.size());
                for (const auto& region : split->cells) {
                  const auto selected = std::ranges::find_if(
                      selection->cells,
                      [&region](const EvaluatedRegion& selected_region) {
                        return selected_region.key == region.key;
                      });
                  if (selected == selection->cells.end()) {
                    evaluated_filter.cells.push_back(region);
                  }
                }
              } else {
                valid = false;
                addDiagnostic(snapshot, node_id, "Region filter mode is invalid");
              }
            }

            if (!valid) {
              evaluated_filter.cells.clear();
            }
            snapshot.region_filters.push_back(std::move(evaluated_filter));
          } else {
            addDiagnostic(snapshot, node_id, "Unsupported document operation node");
          }
        },
        node.definition);
  }

  return snapshot;
}

}  // namespace signet::geometry
