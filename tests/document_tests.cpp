// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/document.h"

#include <cassert>
#include <limits>
#include <stdexcept>

int main() {
  using namespace signet::core;

  const auto regionKey = [](const NodeId split_node_id, const NodeId source_node_id) {
    return RegionKey{
        split_node_id,
        {RegionExpressionTerm{RegionExpressionTerm::Kind::leaf, source_node_id}},
        RegionCutterSide::negative,
        {{RegionBoundaryProvenance{source_node_id, 0}}}};
  };

  Document document("Study");
  assert(document.schemaVersion() == Document::current_schema_version);
  assert(document.nodes().empty());

  const NodeId left = document.addPrimitive("Left", Circle{50.0});
  const NodeId right = document.addPrimitive("Right", Circle{50.0}, Transform{Point{50.0, 0.0}});
  const NodeId result = document.addBoolean("Overlap", BooleanOperation::intersect, left, right);

  assert(document.nodes().size() == 3);
  assert(document.findNode(result) != nullptr);
  assert(document.setTransform(left, Transform{Point{-10.0, 0.0}}));
  assert(!document.setTransform(result, Transform{}));

  bool rejected_invalid_operands = false;
  try {
    static_cast<void>(document.addBoolean("Invalid", BooleanOperation::unite, left, 9999));
  } catch (const std::invalid_argument&) {
    rejected_invalid_operands = true;
  }
  assert(rejected_invalid_operands);

  bool rejected_invalid_primitive = false;
  try {
    static_cast<void>(document.addPrimitive("Invalid circle", Circle{0.0}));
  } catch (const std::invalid_argument&) {
    rejected_invalid_primitive = true;
  }
  assert(rejected_invalid_primitive);

  Document golden_document("Golden rectangle");
  const NodeId golden = golden_document.addPrimitive("Golden", GoldenRectangle{2.0});
  assert(golden_document.schemaVersion() == Document::current_schema_version);
  const auto* golden_node = golden_document.findNode(golden);
  assert(golden_node != nullptr);
  const auto* golden_primitive = std::get_if<PrimitiveNode>(&golden_node->definition);
  assert(golden_primitive != nullptr);
  const auto* golden_definition = std::get_if<GoldenRectangle>(&golden_primitive->primitive);
  assert(golden_definition != nullptr);
  assert(golden_definition->short_side == 2.0);
  assert(golden_definition->longSide() > golden_definition->short_side);

  bool rejected_invalid_golden_rectangle = false;
  try {
    static_cast<void>(golden_document.addPrimitive("Invalid golden", GoldenRectangle{0.0}));
  } catch (const std::invalid_argument&) {
    rejected_invalid_golden_rectangle = true;
  }
  assert(rejected_invalid_golden_rectangle);

  bool rejected_invalid_transform = false;
  try {
    static_cast<void>(document.setTransform(
        left,
        Transform{Point{std::numeric_limits<double>::infinity(), 0.0}}));
  } catch (const std::invalid_argument&) {
    rejected_invalid_transform = true;
  }
  assert(rejected_invalid_transform);

  const SymmetryAxis vertical_axis{Point{0.0, 0.0}, Point{0.0, 1.0}};
  const NodeId mirrored = document.addSymmetry("Vertical mirror", result, vertical_axis);
  const NodeId split = document.addSplit("Vertical split", mirrored, vertical_axis);
  const auto selected_region_key = regionKey(split, mirrored);
  const NodeId selected = document.addRegionSelection(
      "Selected region", split, {selected_region_key});
  const NodeId filtered = document.addRegionFilter(
      "Filtered region", split, selected, RegionFilterMode::remove_selected);
  assert(document.nodes().size() == 7);
  assert(std::get<SymmetryNode>(document.findNode(mirrored)->definition).axis == vertical_axis);
  assert(std::get<SplitNode>(document.findNode(split)->definition).input == mirrored);
  assert(std::get<RegionSelectionNode>(document.findNode(selected)->definition).region_keys ==
         std::vector<RegionKey>{selected_region_key});
  const auto& filter_definition =
      std::get<RegionFilterNode>(document.findNode(filtered)->definition);
  assert(filter_definition.input == split);
  assert(filter_definition.selection == selected);
  assert(filter_definition.mode == RegionFilterMode::remove_selected);

  bool rejected_invalid_operation_input = false;
  try {
    static_cast<void>(document.addSymmetry("Invalid mirror", 9999, vertical_axis));
  } catch (const std::invalid_argument&) {
    rejected_invalid_operation_input = true;
  }
  assert(rejected_invalid_operation_input);

  bool rejected_invalid_split_input = false;
  try {
    static_cast<void>(document.addSplit("Invalid split input", 9999, vertical_axis));
  } catch (const std::invalid_argument&) {
    rejected_invalid_split_input = true;
  }
  assert(rejected_invalid_split_input);

  bool rejected_invalid_selection_input = false;
  try {
    static_cast<void>(document.addRegionSelection("Invalid selection input", 9999, {}));
  } catch (const std::invalid_argument&) {
    rejected_invalid_selection_input = true;
  }
  assert(rejected_invalid_selection_input);

  bool rejected_empty_region_keys = false;
  try {
    static_cast<void>(document.addRegionSelection("Empty selection", split, {}));
  } catch (const std::invalid_argument&) {
    rejected_empty_region_keys = true;
  }
  assert(rejected_empty_region_keys);

  bool rejected_wrong_region_selection_input = false;
  try {
    static_cast<void>(document.addRegionSelection(
        "Wrong selection input", mirrored, {selected_region_key}));
  } catch (const std::invalid_argument&) {
    rejected_wrong_region_selection_input = true;
  }
  assert(rejected_wrong_region_selection_input);

  bool rejected_duplicate_region_keys = false;
  try {
    static_cast<void>(document.addRegionSelection(
        "Duplicate selection", split, {selected_region_key, selected_region_key}));
  } catch (const std::invalid_argument&) {
    rejected_duplicate_region_keys = true;
  }
  assert(rejected_duplicate_region_keys);

  bool rejected_future_region_reference = false;
  try {
    static_cast<void>(document.addRegionSelection(
        "Future selection",
        split,
        {RegionKey{
            split,
            {RegionExpressionTerm{RegionExpressionTerm::Kind::leaf, 999999}},
            RegionCutterSide::negative,
            {}}}));
  } catch (const std::invalid_argument&) {
    rejected_future_region_reference = true;
  }
  assert(rejected_future_region_reference);

  bool rejected_invalid_split_axis = false;
  try {
    static_cast<void>(document.addSplit("Invalid split", mirrored, Point{}, Point{}));
  } catch (const std::invalid_argument&) {
    rejected_invalid_split_axis = true;
  }
  assert(rejected_invalid_split_axis);

  bool rejected_nonfinite_symmetry_axis = false;
  try {
    static_cast<void>(document.addSymmetry(
        "Invalid axis", mirrored, Point{}, Point{std::numeric_limits<double>::quiet_NaN(), 1.0}));
  } catch (const std::invalid_argument&) {
    rejected_nonfinite_symmetry_axis = true;
  }
  assert(rejected_nonfinite_symmetry_axis);

  bool rejected_invalid_region_key = false;
  try {
    static_cast<void>(document.addRegionSelection("Invalid selection", split, {RegionKey{}}));
  } catch (const std::invalid_argument&) {
    rejected_invalid_region_key = true;
  }
  assert(rejected_invalid_region_key);

  DocumentHistory history(Document("History"));
  const NodeId first = history.addPrimitive("First", Circle{10.0});
  const NodeId removed_by_undo = history.addPrimitive("Second", Rectangle{20.0, 30.0});
  assert(history.document().nodes().size() == 2);
  assert(history.canUndo());
  assert(history.undo());
  assert(history.document().findNode(first) != nullptr);
  assert(history.document().findNode(removed_by_undo) == nullptr);
  assert(history.canRedo());
  assert(history.redo());
  assert(history.document().findNode(removed_by_undo) != nullptr);

  assert(history.undo());
  const NodeId branch = history.addPrimitive("Branch", Arc{12.0, 0.0, 90.0});
  assert(branch > removed_by_undo);
  assert(!history.canRedo());

  DocumentHistory golden_history(Document("Golden history"));
  const NodeId golden_history_node =
      golden_history.addPrimitive("Golden", GoldenRectangle{3.0});
  assert(golden_history.undo());
  assert(golden_history.document().findNode(golden_history_node) == nullptr);
  assert(golden_history.redo());
  assert(golden_history.document().findNode(golden_history_node) != nullptr);
  const NodeId golden_branch = golden_history.addPrimitive("Branch", Rectangle{2.0, 4.0});
  assert(golden_branch > golden_history_node);
  assert(!golden_history.canRedo());

  const Transform moved{Point{4.0, 5.0}, 30.0, Point{-1.0, 1.0}};
  assert(history.setTransform(branch, moved));
  const auto* moved_node = history.document().findNode(branch);
  assert(moved_node != nullptr);
  assert(std::get<PrimitiveNode>(moved_node->definition).transform == moved);
  assert(history.undo());
  const auto* restored_node = history.document().findNode(branch);
  assert(restored_node != nullptr);
  assert(std::get<PrimitiveNode>(restored_node->definition).transform == Transform{});

  DocumentHistory operation_history(Document("Operations"));
  const NodeId operation_source = operation_history.addPrimitive("Source", Circle{8.0});
  const NodeId operation_mirror = operation_history.addSymmetry(
      "Mirror", operation_source, Point{}, Point{1.0, 1.0});
  const NodeId operation_split = operation_history.addSplit(
      "Split", operation_mirror, SymmetryAxis{Point{2.0, 3.0}, Point{0.0, 1.0}});
  const NodeId operation_selection = operation_history.addRegionSelection(
      "Selection", operation_split, {regionKey(operation_split, operation_source)});
  assert(operation_history.document().nodes().size() == 4);
  assert(operation_history.undo());
  assert(operation_history.document().findNode(operation_selection) == nullptr);
  assert(operation_history.redo());
  assert(operation_history.document().findNode(operation_selection) != nullptr);
  assert(operation_history.undo());
  assert(operation_history.undo());
  assert(operation_history.document().findNode(operation_mirror) != nullptr);
  assert(operation_history.document().findNode(operation_split) == nullptr);
  assert(operation_history.redo());
  assert(operation_history.document().findNode(operation_split) != nullptr);
  assert(operation_history.redo());
  assert(operation_history.document().findNode(operation_selection) != nullptr);
  assert(operation_history.undo());
  const NodeId operation_branch = operation_history.addPrimitive("Branch", Rectangle{3.0, 4.0});
  assert(operation_branch > operation_selection);
  assert(!operation_history.canRedo());

  // Region selection and filtering are one domain operation and therefore
  // one history entry. Invalid requests do not consume IDs or history.
  DocumentHistory atomic_region_history(Document("Atomic region operation"));
  const NodeId atomic_source = atomic_region_history.addPrimitive("Source", Circle{8.0});
  const NodeId atomic_split = atomic_region_history.addSplit(
      "Split", atomic_source, SymmetryAxis{Point{}, Point{0.0, 1.0}});
  const auto atomic_key = regionKey(atomic_split, atomic_source);
  const auto atomic_node_count = atomic_region_history.document().nodes().size();
  const NodeId atomic_last_id = atomic_region_history.document().nodes().back().id;
  const bool atomic_can_undo = atomic_region_history.canUndo();

  const auto invalid_split_result = atomic_region_history.addRegionSelectionAndFilter(
      "Invalid selection", "Invalid filter", 999999, {atomic_key},
      RegionFilterMode::remove_selected);
  assert(!invalid_split_result);
  assert(invalid_split_result.rejection == RegionFilterRejection::invalid_split);
  assert(atomic_region_history.document().nodes().size() == atomic_node_count);
  assert(atomic_region_history.document().nodes().back().id == atomic_last_id);
  assert(atomic_region_history.canUndo() == atomic_can_undo);

  const auto wrong_type_split_result = atomic_region_history.addRegionSelectionAndFilter(
      "Wrong type selection", "Wrong type filter", atomic_source, {atomic_key},
      RegionFilterMode::remove_selected);
  assert(!wrong_type_split_result);
  assert(wrong_type_split_result.rejection == RegionFilterRejection::invalid_split);
  assert(atomic_region_history.document().nodes().size() == atomic_node_count);
  assert(atomic_region_history.document().nodes().back().id == atomic_last_id);

  const auto duplicate_key_result = atomic_region_history.addRegionSelectionAndFilter(
      "Duplicate selection", "Duplicate filter", atomic_split, {atomic_key, atomic_key},
      RegionFilterMode::remove_selected);
  assert(!duplicate_key_result);
  assert(duplicate_key_result.rejection == RegionFilterRejection::duplicate_region_key);
  assert(atomic_region_history.document().nodes().size() == atomic_node_count);
  assert(atomic_region_history.document().nodes().back().id == atomic_last_id);

  const auto invalid_mode_result = atomic_region_history.addRegionSelectionAndFilter(
      "Invalid mode selection", "Invalid mode filter", atomic_split, {atomic_key},
      static_cast<RegionFilterMode>(255));
  assert(!invalid_mode_result);
  assert(invalid_mode_result.rejection == RegionFilterRejection::invalid_mode);
  assert(atomic_region_history.document().nodes().size() == atomic_node_count);
  assert(atomic_region_history.document().nodes().back().id == atomic_last_id);

  const auto atomic_result = atomic_region_history.addRegionSelectionAndFilter(
      "Atomic selection", "Atomic filter", atomic_split, {atomic_key},
      RegionFilterMode::remove_selected);
  assert(atomic_result);
  assert(atomic_result.selection_id == atomic_split + 1);
  assert(atomic_result.filter_id == atomic_split + 2);
  assert(atomic_region_history.document().nodes().size() == atomic_node_count + 2);
  const auto* atomic_selection =
      atomic_region_history.document().findNode(atomic_result.selection_id);
  const auto* atomic_filter = atomic_region_history.document().findNode(atomic_result.filter_id);
  assert(atomic_selection != nullptr);
  assert(atomic_filter != nullptr);
  assert(atomic_selection->id + 1 == atomic_filter->id);
  assert(atomic_selection->name == "Atomic selection");
  assert(atomic_filter->name == "Atomic filter");
  const auto& atomic_selection_definition =
      std::get<RegionSelectionNode>(atomic_selection->definition);
  assert(atomic_selection_definition.input == atomic_split);
  assert(atomic_selection_definition.region_keys == std::vector<RegionKey>{atomic_key});
  const auto& atomic_filter_definition = std::get<RegionFilterNode>(atomic_filter->definition);
  assert(atomic_filter_definition.input == atomic_split);
  assert(atomic_filter_definition.selection == atomic_result.selection_id);
  assert(atomic_filter_definition.mode == RegionFilterMode::remove_selected);

  assert(atomic_region_history.undo());
  assert(atomic_region_history.document().findNode(atomic_result.selection_id) == nullptr);
  assert(atomic_region_history.document().findNode(atomic_result.filter_id) == nullptr);
  assert(atomic_region_history.document().findNode(atomic_split) != nullptr);
  assert(atomic_region_history.redo());
  const auto* redone_selection =
      atomic_region_history.document().findNode(atomic_result.selection_id);
  const auto* redone_filter = atomic_region_history.document().findNode(atomic_result.filter_id);
  assert(redone_selection != nullptr);
  assert(redone_filter != nullptr);
  assert(redone_selection->name == "Atomic selection");
  assert(redone_filter->name == "Atomic filter");
  assert(std::get<RegionSelectionNode>(redone_selection->definition).region_keys ==
         std::vector<RegionKey>{atomic_key});
  const auto& redone_filter_definition = std::get<RegionFilterNode>(redone_filter->definition);
  assert(redone_filter_definition.selection == atomic_result.selection_id);
  assert(redone_filter_definition.mode == RegionFilterMode::remove_selected);

  assert(atomic_region_history.undo());
  const NodeId atomic_branch = atomic_region_history.addPrimitive("Branch", Rectangle{3.0, 4.0});
  assert(atomic_branch > atomic_result.filter_id);
  assert(!atomic_region_history.canRedo());

  // A batch is validated as a whole and applied in document order.
  DocumentHistory batch_history(Document("Batch"));
  const NodeId batch_circle = batch_history.addPrimitive("Circle", Circle{2.0});
  const NodeId batch_rectangle = batch_history.addPrimitive("Rectangle", Rectangle{3.0, 4.0});
  const NodeId batch_golden = batch_history.addPrimitive("Golden", GoldenRectangle{5.0});
  const NodeId batch_arc = batch_history.addPrimitive("Arc", Arc{6.0, 10.0, -45.0});
  const NodeId batch_operation =
      batch_history.addBoolean("Operation", BooleanOperation::unite, batch_circle, batch_rectangle);
  const Transform batch_circle_transform{Point{1.0, 2.0}, 15.0, Point{-1.0, 1.0}};
  const Transform batch_arc_transform{Point{-3.0, 4.0}, -25.0, Point{2.0, 3.0}};
  const auto batch_result = batch_history.applyTransforms({
      TransformUpdate{batch_arc, batch_arc_transform},
      TransformUpdate{batch_circle, batch_circle_transform},
      TransformUpdate{batch_rectangle, Transform{Point{7.0, 8.0}}},
      TransformUpdate{batch_golden, Transform{Point{9.0, 10.0}, 90.0, Point{1.0, -1.0}}},
  });
  assert(batch_result && batch_result.changed);
  assert(std::get<PrimitiveNode>(batch_history.document().findNode(batch_circle)->definition).transform ==
         batch_circle_transform);
  assert(std::get<PrimitiveNode>(batch_history.document().findNode(batch_arc)->definition).transform ==
         batch_arc_transform);
  assert(!batch_history.applyTransforms({TransformUpdate{batch_operation, Transform{}}}));
  assert(batch_history.applyTransforms({TransformUpdate{batch_circle, batch_circle_transform}}));
  assert(!batch_history.applyTransforms({
      TransformUpdate{batch_circle, Transform{Point{20.0, 0.0}}},
      TransformUpdate{batch_rectangle, Transform{Point{30.0, 0.0}}},
      TransformUpdate{99999, Transform{}},
  }));
  assert(std::get<PrimitiveNode>(batch_history.document().findNode(batch_circle)->definition).transform ==
         batch_circle_transform);
  assert(!batch_history.applyTransforms({
      TransformUpdate{batch_circle, Transform{}},
      TransformUpdate{batch_circle, Transform{}},
  }));
  assert(!batch_history.applyTransforms({
      TransformUpdate{batch_circle, Transform{Point{20.0, 0.0}}},
      TransformUpdate{batch_rectangle,
                      Transform{Point{}, 0.0, Point{std::numeric_limits<double>::quiet_NaN(), 1.0}}},
  }));
  assert(batch_history.undo());
  assert(std::get<PrimitiveNode>(batch_history.document().findNode(batch_circle)->definition).transform ==
         Transform{});
  assert(std::get<PrimitiveNode>(batch_history.document().findNode(batch_arc)->definition).transform ==
         Transform{});

  // No-op batches do not create an additional history entry.
  DocumentHistory no_op_history(Document("No-op batch"));
  const NodeId no_op_node = no_op_history.addPrimitive("Node", Circle{1.0});
  assert(no_op_history.applyTransforms({TransformUpdate{no_op_node, Transform{}}}));
  assert(no_op_history.undo());
  assert(no_op_history.document().findNode(no_op_node) == nullptr);
  assert(no_op_history.redo());

  // Duplicate every node variant.  The selection order is deliberately not
  // the document order; the result mapping must still be stable.
  DocumentHistory duplicate_history(Document("Duplicate"));
  const NodeId external = duplicate_history.addPrimitive("External", Circle{11.0});
  const NodeId duplicate_circle = duplicate_history.addPrimitive(
      "Circle", Circle{2.0}, Transform{Point{1.0, 2.0}, 17.0, Point{-1.0, 1.0}});
  const NodeId duplicate_rectangle = duplicate_history.addPrimitive("Rectangle", Rectangle{3.0, 4.0});
  const NodeId duplicate_golden = duplicate_history.addPrimitive("Golden", GoldenRectangle{5.0});
  const NodeId duplicate_arc = duplicate_history.addPrimitive("Arc", Arc{6.0, 10.0, -45.0});
  const NodeId duplicate_boolean = duplicate_history.addBoolean(
      "Boolean", BooleanOperation::subtract, duplicate_circle, external);
  const NodeId duplicate_symmetry = duplicate_history.addSymmetry(
      "Symmetry", duplicate_boolean, SymmetryAxis{Point{1.0, 2.0}, Point{0.0, 1.0}});
  const NodeId duplicate_split = duplicate_history.addSplit(
      "Split", duplicate_symmetry, SymmetryAxis{Point{3.0, 4.0}, Point{1.0, 1.0}});
  const NodeId duplicate_selection = duplicate_history.addRegionSelection(
      "Selection", duplicate_split, {regionKey(duplicate_split, duplicate_circle)});
  const NodeId duplicate_filter = duplicate_history.addRegionFilter(
      "Filter", duplicate_split, duplicate_selection, RegionFilterMode::keep_selected);
  const std::vector<NodeId> all_selected{
      duplicate_selection, duplicate_filter, duplicate_arc, duplicate_rectangle,
      duplicate_symmetry, duplicate_circle, duplicate_split, duplicate_boolean, duplicate_golden,
  };
  const auto duplicate_result = duplicate_history.duplicateSelected(all_selected);
  assert(duplicate_result);
  assert(duplicate_result.mapping.size() == all_selected.size());
  assert(duplicate_result.mapping[0].first == external + 1);
  assert(duplicate_result.mapping[0].second == duplicate_result.mapping.front().second);
  for (std::size_t index = 1; index < duplicate_result.mapping.size(); ++index) {
    assert(duplicate_result.mapping[index].first > duplicate_result.mapping[index - 1].first);
    assert(duplicate_result.mapping[index].second == duplicate_result.mapping[index - 1].second + 1);
  }
  const auto duplicate_id = [&duplicate_result](const NodeId original) {
    for (const auto [source, copy] : duplicate_result.mapping) {
      if (source == original) {
        return copy;
      }
    }
    assert(false);
    return NodeId{};
  };
  const NodeId copied_boolean = duplicate_id(duplicate_boolean);
  const auto& copied_boolean_definition =
      std::get<BooleanNode>(duplicate_history.document().findNode(copied_boolean)->definition);
  assert(copied_boolean_definition.left == duplicate_id(duplicate_circle));
  assert(copied_boolean_definition.right == external);
  const NodeId copied_filter = duplicate_id(duplicate_filter);
  const auto& copied_filter_definition =
      std::get<RegionFilterNode>(duplicate_history.document().findNode(copied_filter)->definition);
  assert(copied_filter_definition.input == duplicate_id(duplicate_split));
  assert(copied_filter_definition.selection == duplicate_id(duplicate_selection));
  assert(std::get<PrimitiveNode>(duplicate_history.document().findNode(duplicate_id(duplicate_circle))->definition)
             .transform ==
         std::get<PrimitiveNode>(duplicate_history.document().findNode(duplicate_circle)->definition).transform);
  assert(duplicate_history.document().nodes().back().id == duplicate_result.mapping.back().second);
  assert(duplicate_history.undo());
  assert(duplicate_history.document().findNode(copied_boolean) == nullptr);
  assert(duplicate_history.redo());
  assert(duplicate_history.document().findNode(copied_boolean) != nullptr);
  const NodeId duplicate_branch = duplicate_history.addPrimitive("Branch", Circle{12.0});
  assert(duplicate_branch > duplicate_result.mapping.back().second);
  assert(!duplicate_history.duplicateSelected({duplicate_circle, duplicate_circle}));
  assert(!duplicate_history.duplicateSelected({999999}));

  // Removal policies are explicit and preserve the ID high-water mark.
  DocumentHistory remove_history(Document("Remove"));
  const NodeId remove_external = remove_history.addPrimitive("External", Circle{3.0});
  const NodeId remove_source = remove_history.addPrimitive("Source", Circle{2.0});
  const NodeId remove_shared = remove_history.addBoolean(
      "Shared", BooleanOperation::unite, remove_source, remove_external);
  const NodeId remove_chain = remove_history.addSymmetry(
      "Chain", remove_shared, SymmetryAxis{Point{}, Point{1.0, 0.0}});
  const NodeId remove_independent = remove_history.addPrimitive("Independent", Rectangle{2.0, 3.0});
  const NodeId remove_split = remove_history.addSplit(
      "Split", remove_source, SymmetryAxis{Point{}, Point{0.0, 1.0}});
  const NodeId remove_selection = remove_history.addRegionSelection(
      "Selection", remove_split, {regionKey(remove_split, remove_source)});
  const NodeId remove_filter = remove_history.addRegionFilter(
      "Filter", remove_split, remove_selection, RegionFilterMode::keep_selected);
  const auto rejected_remove = remove_history.removeSelected(
      {remove_source}, RemovePolicy::reject_if_referenced);
  assert(!rejected_remove);
  assert(rejected_remove.rejection == RemoveRejection::externally_referenced);
  assert(remove_history.document().findNode(remove_source) != nullptr);
  const auto cascaded_remove = remove_history.removeSelected(
      {remove_source}, RemovePolicy::cascade_dependents);
  assert(cascaded_remove);
  assert((cascaded_remove.removed_ids ==
          std::vector<NodeId>{
              remove_source, remove_shared, remove_chain, remove_split, remove_selection,
              remove_filter}));
  assert(remove_history.document().findNode(remove_independent) != nullptr);
  assert(remove_history.undo());
  assert(remove_history.document().findNode(remove_chain) != nullptr);
  assert(remove_history.redo());
  const NodeId high_water_branch = remove_history.addPrimitive("After remove", Arc{3.0, 0.0, 90.0});
  assert(high_water_branch > remove_filter);

  DocumentHistory operation_remove_history(Document("Remove operation"));
  const NodeId operation_input = operation_remove_history.addPrimitive("Input", Circle{4.0});
  const NodeId operation_only = operation_remove_history.addSymmetry(
      "Operation", operation_input, SymmetryAxis{Point{}, Point{0.0, 1.0}});
  const auto removed_operation = operation_remove_history.removeSelected(
      {operation_only}, RemovePolicy::reject_if_referenced);
  assert(removed_operation);
  assert(removed_operation.removed_ids == std::vector<NodeId>{operation_only});
  assert(operation_remove_history.document().findNode(operation_input) != nullptr);
}
