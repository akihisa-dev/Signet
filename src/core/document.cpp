// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/document.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace signet::core {

namespace {

bool finite(const double value) { return std::isfinite(value); }

void validateTransform(const Transform& transform) {
  if (!finite(transform.translation.x) || !finite(transform.translation.y) ||
      !finite(transform.rotation_degrees) || !finite(transform.scale.x) ||
      !finite(transform.scale.y) || transform.scale.x == 0.0 || transform.scale.y == 0.0) {
    throw std::invalid_argument("Transforms require finite values and non-zero scale");
  }
}

void validatePoint(const Point& point, const char* description) {
  if (!finite(point.x) || !finite(point.y)) {
    throw std::invalid_argument(std::string(description) + " requires finite coordinates");
  }
}

void validateAxis(const SymmetryAxis& axis) {
  validatePoint(axis.origin, "Operation axis origin");
  validatePoint(axis.direction, "Operation axis direction");
  if (axis.direction.x == 0.0 && axis.direction.y == 0.0) {
    throw std::invalid_argument("Operation axis direction must not be degenerate");
  }
}

void validateRegionKey(
    const Document& document,
    const NodeId expected_split_node_id,
    const RegionKey& key) {
  if (key.split_node_id != expected_split_node_id) {
    throw std::invalid_argument(
        "Region key split NodeId must match the RegionSelection input");
  }
  const auto* split_node = document.findNode(key.split_node_id);
  if (split_node == nullptr || !std::holds_alternative<SplitNode>(split_node->definition)) {
    throw std::invalid_argument("Region key split NodeId must reference a Split node");
  }
  if (key.construction_expression.empty()) {
    throw std::invalid_argument("Region key construction expression must not be empty");
  }
  std::size_t expression_depth = 0;
  for (const auto& term : key.construction_expression) {
    if (term.kind == RegionExpressionTerm::Kind::leaf) {
      if (term.node_id == 0 || document.findNode(term.node_id) == nullptr) {
        throw std::invalid_argument(
            "Region key expression leaf must reference an existing node");
      }
      ++expression_depth;
    } else if (term.kind == RegionExpressionTerm::Kind::boolean) {
      if (term.node_id != 0) {
        throw std::invalid_argument("Region key Boolean expression term must not carry a NodeId");
      }
      switch (term.operation) {
        case BooleanOperation::unite:
        case BooleanOperation::intersect:
        case BooleanOperation::subtract:
        case BooleanOperation::exclusive_or:
          break;
        default:
          throw std::invalid_argument("Region key Boolean expression operation is invalid");
      }
      if (expression_depth < 2) {
        throw std::invalid_argument(
            "Region key construction expression Boolean term has insufficient operands");
      }
      --expression_depth;
    } else {
      throw std::invalid_argument("Region key construction expression kind is invalid");
    }
  }
  if (expression_depth != 1) {
    throw std::invalid_argument(
        "Region key construction expression must contain exactly one result");
  }
  for (const auto& loop : key.boundary_provenance) {
    for (const auto& provenance : loop) {
      if (provenance.source_node_id == 0 ||
          document.findNode(provenance.source_node_id) == nullptr) {
        throw std::invalid_argument(
            "Region key boundary provenance must reference an existing node");
      }
    }
  }
  switch (key.cutter_side) {
    case RegionCutterSide::negative:
    case RegionCutterSide::positive:
    case RegionCutterSide::on_boundary:
      break;
    default:
      throw std::invalid_argument("Region key cutter side is invalid");
  }
}

void validateRegionKeys(
    const Document& document,
    const NodeId split_node_id,
    const std::vector<RegionKey>& keys) {
  if (keys.empty()) {
    throw std::invalid_argument("Region selection keys must not be empty");
  }
  for (std::size_t index = 0; index < keys.size(); ++index) {
    validateRegionKey(document, split_node_id, keys[index]);
    if (index != 0 && !(keys[index - 1] < keys[index])) {
      throw std::invalid_argument("Region selection keys must be sorted and unique");
    }
  }
}

void validatePrimitive(const Primitive& primitive) {
  std::visit(
      [](const auto& value) {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, Circle>) {
          if (!finite(value.radius) || value.radius <= 0.0) {
            throw std::invalid_argument("Circle radius must be finite and positive");
          }
        } else if constexpr (std::is_same_v<Value, Rectangle>) {
          if (!finite(value.width) || !finite(value.height) || value.width <= 0.0 ||
              value.height <= 0.0) {
            throw std::invalid_argument("Rectangle dimensions must be finite and positive");
          }
        } else if constexpr (std::is_same_v<Value, GoldenRectangle>) {
          if (!finite(value.short_side) || value.short_side <= 0.0) {
            throw std::invalid_argument("GoldenRectangle short side must be finite and positive");
          }
        } else if (!finite(value.radius) || !finite(value.start_degrees) ||
                   !finite(value.sweep_degrees) || value.radius <= 0.0 ||
                   value.sweep_degrees == 0.0 || std::abs(value.sweep_degrees) > 360.0) {
          throw std::invalid_argument(
              "Arc values must be finite with a positive radius and a non-zero sweep up to 360 degrees");
        }
      },
      primitive);
}

void validateInput(const Document& document, const NodeId input, const char* operation) {
  if (document.findNode(input) == nullptr) {
    throw std::invalid_argument(std::string(operation) + " input must reference an existing node");
  }
}

template <typename Callback>
void visitInputs(const NodeDefinition& definition, Callback&& callback) {
  std::visit(
      [&callback](const auto& value) {
        using Value = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, BooleanNode>) {
          callback(value.left);
          callback(value.right);
        } else if constexpr (std::is_same_v<Value, SymmetryNode> ||
                             std::is_same_v<Value, SplitNode>) {
          callback(value.input);
        } else if constexpr (std::is_same_v<Value, RegionSelectionNode>) {
          callback(value.input);
          for (const auto& key : value.region_keys) {
            callback(key.split_node_id);
            for (const auto& term : key.construction_expression) {
              if (term.kind == RegionExpressionTerm::Kind::leaf) {
                callback(term.node_id);
              }
            }
            for (const auto& loop : key.boundary_provenance) {
              for (const auto& provenance : loop) {
                callback(provenance.source_node_id);
              }
            }
          }
        } else if constexpr (std::is_same_v<Value, RegionFilterNode>) {
          callback(value.input);
          callback(value.selection);
        }
      },
      definition);
}

void remapRegionKey(
    RegionKey& key,
    const std::unordered_map<NodeId, NodeId>& remapping) {
  if (const auto found = remapping.find(key.split_node_id); found != remapping.end()) {
    key.split_node_id = found->second;
  }
  for (auto& term : key.construction_expression) {
    if (term.kind == RegionExpressionTerm::Kind::leaf) {
      if (const auto found = remapping.find(term.node_id); found != remapping.end()) {
        term.node_id = found->second;
      }
    }
  }
  for (auto& loop : key.boundary_provenance) {
    for (auto& provenance : loop) {
      if (const auto found = remapping.find(provenance.source_node_id);
          found != remapping.end()) {
        provenance.source_node_id = found->second;
      }
    }
  }
}

bool definitionReferencesExistingNodes(
    const Document& document,
    const NodeDefinition& definition,
    const std::unordered_map<NodeId, NodeId>& remapping) {
  bool valid = true;
  visitInputs(definition, [&](const NodeId input) {
    const bool is_new_node = std::ranges::any_of(
        remapping,
        [input](const auto& pair) { return pair.second == input; });
    if (document.findNode(input) == nullptr && !is_new_node) {
      valid = false;
    }
  });
  return valid;
}

bool definitionReferencesRemovedNode(
    const NodeDefinition& definition,
    const std::unordered_set<NodeId>& removed) {
  bool references_removed = false;
  visitInputs(definition, [&](const NodeId input) {
    references_removed = references_removed || removed.contains(input);
  });
  return references_removed;
}

}  // namespace

Document::Document(std::string name) : name_(std::move(name)) {}

std::uint32_t Document::schemaVersion() const noexcept { return schema_version_; }

const std::string& Document::name() const noexcept { return name_; }

const std::vector<Node>& Document::nodes() const noexcept { return nodes_; }

const Node* Document::findNode(const NodeId id) const noexcept {
  const auto found = std::ranges::find(nodes_, id, &Node::id);
  return found == nodes_.end() ? nullptr : &*found;
}

NodeId Document::addPrimitive(std::string name, Primitive primitive, const Transform transform) {
  validatePrimitive(primitive);
  validateTransform(transform);
  const NodeId id = next_id_++;
  nodes_.push_back(Node{id, std::move(name), PrimitiveNode{std::move(primitive), transform}, true});
  return id;
}

NodeId Document::addBoolean(
    std::string name,
    const BooleanOperation operation,
    const NodeId left,
    const NodeId right) {
  if (left == right || findNode(left) == nullptr || findNode(right) == nullptr) {
    throw std::invalid_argument("Boolean operands must reference two existing nodes");
  }

  const NodeId id = next_id_++;
  nodes_.push_back(Node{id, std::move(name), BooleanNode{operation, left, right}, true});
  return id;
}

NodeId Document::addSymmetry(std::string name, const NodeId input, const SymmetryAxis axis) {
  validateInput(*this, input, "Symmetry");
  validateAxis(axis);
  const NodeId id = next_id_++;
  nodes_.push_back(Node{id, std::move(name), SymmetryNode{input, axis}, true});
  return id;
}

NodeId Document::addSymmetry(
    std::string name,
    const NodeId input,
    const Point axis_origin,
    const Point axis_direction) {
  return addSymmetry(std::move(name), input, SymmetryAxis{axis_origin, axis_direction});
}

NodeId Document::addSplit(std::string name, const NodeId input, const SymmetryAxis axis) {
  validateInput(*this, input, "Split");
  validateAxis(axis);
  const NodeId id = next_id_++;
  nodes_.push_back(Node{id, std::move(name), SplitNode{input, axis}, true});
  return id;
}

NodeId Document::addSplit(
    std::string name,
    const NodeId input,
    const Point axis_origin,
    const Point axis_direction) {
  return addSplit(std::move(name), input, SymmetryAxis{axis_origin, axis_direction});
}

NodeId Document::addRegionSelection(
    std::string name,
    const NodeId input,
    std::vector<RegionKey> region_keys) {
  const auto* input_node = findNode(input);
  if (input_node == nullptr || !std::holds_alternative<SplitNode>(input_node->definition)) {
    throw std::invalid_argument("Region selection input must reference a Split node");
  }
  validateRegionKeys(*this, input, region_keys);
  const NodeId id = next_id_++;
  nodes_.push_back(
      Node{id, std::move(name), RegionSelectionNode{input, std::move(region_keys)}, true});
  return id;
}

NodeId Document::addRegionFilter(
    std::string name,
    const NodeId input,
    const NodeId selection,
    const RegionFilterMode mode) {
  validateInput(*this, input, "Region filter");
  const auto* selection_node = findNode(selection);
  if (selection_node == nullptr ||
      !std::holds_alternative<RegionSelectionNode>(selection_node->definition)) {
    throw std::invalid_argument("Region filter selection must reference a RegionSelection node");
  }
  const auto& selection_definition = std::get<RegionSelectionNode>(selection_node->definition);
  if (selection_definition.input != input) {
    throw std::invalid_argument("Region filter input must match its RegionSelection input");
  }
  switch (mode) {
    case RegionFilterMode::keep_selected:
    case RegionFilterMode::remove_selected:
      break;
    default:
      throw std::invalid_argument("Region filter mode is invalid");
  }
  const NodeId id = next_id_++;
  nodes_.push_back(
      Node{id, std::move(name), RegionFilterNode{input, selection, mode}, true});
  return id;
}

RegionFilterResult Document::addRegionSelectionAndFilter(
    std::string selection_name,
    std::string filter_name,
    const NodeId split,
    std::vector<RegionKey> region_keys,
    const RegionFilterMode mode) {
  RegionFilterResult result{false, RegionFilterRejection::none, 0, 0};

  const auto* split_node = findNode(split);
  if (split_node == nullptr || !std::holds_alternative<SplitNode>(split_node->definition)) {
    result.rejection = RegionFilterRejection::invalid_split;
    return result;
  }
  if (region_keys.empty()) {
    result.rejection = RegionFilterRejection::empty_region_keys;
    return result;
  }
  for (std::size_t index = 1; index < region_keys.size(); ++index) {
    if (region_keys[index - 1] == region_keys[index]) {
      result.rejection = RegionFilterRejection::duplicate_region_key;
      return result;
    }
    if (!(region_keys[index - 1] < region_keys[index])) {
      result.rejection = RegionFilterRejection::invalid_region_key;
      return result;
    }
  }
  try {
    validateRegionKeys(*this, split, region_keys);
  } catch (const std::invalid_argument&) {
    result.rejection = RegionFilterRejection::invalid_region_key;
    return result;
  }
  switch (mode) {
    case RegionFilterMode::keep_selected:
    case RegionFilterMode::remove_selected:
      break;
    default:
      result.rejection = RegionFilterRejection::invalid_mode;
      return result;
  }
  if (next_id_ > std::numeric_limits<NodeId>::max() - 2) {
    result.rejection = RegionFilterRejection::id_exhausted;
    return result;
  }

  const NodeId selection_id = next_id_;
  const NodeId filter_id = selection_id + 1;
  // Reserve before the first insertion. Node moves are non-throwing after the
  // reservation, so a failed validation or allocation cannot leave one node
  // of the pair in the document.
  nodes_.reserve(nodes_.size() + 2);
  nodes_.push_back(
      Node{selection_id,
           std::move(selection_name),
           RegionSelectionNode{split, std::move(region_keys)},
           true});
  nodes_.push_back(
      Node{filter_id,
           std::move(filter_name),
           RegionFilterNode{split, selection_id, mode},
           true});
  next_id_ += 2;
  result.accepted = true;
  result.rejection = RegionFilterRejection::none;
  result.selection_id = selection_id;
  result.filter_id = filter_id;
  return result;
}

bool Document::setTransform(const NodeId id, const Transform transform) {
  const auto found = std::ranges::find(nodes_, id, &Node::id);
  if (found == nodes_.end()) {
    return false;
  }

  auto* primitive = std::get_if<PrimitiveNode>(&found->definition);
  if (primitive == nullptr) {
    return false;
  }

  validateTransform(transform);
  return applyTransforms({TransformUpdate{id, transform}}).accepted;
}

TransformBatchResult Document::applyTransforms(
    const std::vector<TransformUpdate>& updates) {
  TransformBatchResult result{true, false, TransformBatchRejection::none};
  std::unordered_set<NodeId> seen;
  seen.reserve(updates.size());

  for (const auto& update : updates) {
    if (!seen.insert(update.id).second) {
      result.accepted = false;
      result.rejection = TransformBatchRejection::duplicate_id;
      return result;
    }

    const auto found = std::ranges::find(nodes_, update.id, &Node::id);
    if (found == nodes_.end()) {
      result.accepted = false;
      result.rejection = TransformBatchRejection::unknown_id;
      return result;
    }
    if (!std::holds_alternative<PrimitiveNode>(found->definition)) {
      result.accepted = false;
      result.rejection = TransformBatchRejection::operation_node;
      return result;
    }
    try {
      validateTransform(update.after);
    } catch (const std::invalid_argument&) {
      result.accepted = false;
      result.rejection = TransformBatchRejection::invalid_transform;
      return result;
    }
  }

  // Walk the document, rather than the request, so request order cannot affect
  // either the result or the order in which a batch is applied.
  for (auto& node : nodes_) {
    const auto update = std::ranges::find(updates, node.id, &TransformUpdate::id);
    if (update == updates.end()) {
      continue;
    }
    auto& primitive = std::get<PrimitiveNode>(node.definition);
    if (primitive.transform != update->after) {
      primitive.transform = update->after;
      result.changed = true;
    }
  }
  return result;
}

DuplicateResult Document::duplicateSelected(const std::vector<NodeId>& selected) {
  DuplicateResult result{true, DuplicateRejection::none, {}};
  std::unordered_set<NodeId> selected_set;
  selected_set.reserve(selected.size());
  for (const NodeId id : selected) {
    if (findNode(id) == nullptr) {
      result.accepted = false;
      result.rejection = DuplicateRejection::unknown_id;
      return result;
    }
    if (!selected_set.insert(id).second) {
      result.accepted = false;
      result.rejection = DuplicateRejection::duplicate_id;
      return result;
    }
  }
  if (selected.empty()) {
    return result;
  }
  if (selected.size() >
      static_cast<std::size_t>(std::numeric_limits<NodeId>::max() - next_id_)) {
    result.accepted = false;
    result.rejection = DuplicateRejection::id_exhausted;
    return result;
  }

  std::unordered_map<NodeId, NodeId> remapping;
  remapping.reserve(selected.size());
  for (const auto& node : nodes_) {
    if (selected_set.contains(node.id)) {
      const NodeId duplicate_id = next_id_ + result.mapping.size();
      remapping.emplace(node.id, duplicate_id);
      result.mapping.emplace_back(node.id, duplicate_id);
    }
  }

  std::vector<Node> duplicates;
  duplicates.reserve(result.mapping.size());
  for (const auto& node : nodes_) {
    if (!selected_set.contains(node.id)) {
      continue;
    }
    Node duplicate = node;
    duplicate.id = remapping.at(node.id);
    std::visit(
        [&remapping](auto& value) {
          using Value = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<Value, BooleanNode>) {
            if (const auto found = remapping.find(value.left); found != remapping.end()) {
              value.left = found->second;
            }
            if (const auto found = remapping.find(value.right); found != remapping.end()) {
              value.right = found->second;
            }
          } else if constexpr (std::is_same_v<Value, SymmetryNode> ||
                               std::is_same_v<Value, SplitNode>) {
            if (const auto found = remapping.find(value.input); found != remapping.end()) {
              value.input = found->second;
            }
          } else if constexpr (std::is_same_v<Value, RegionSelectionNode>) {
            if (const auto found = remapping.find(value.input); found != remapping.end()) {
              value.input = found->second;
            }
            for (auto& key : value.region_keys) {
              remapRegionKey(key, remapping);
            }
            std::ranges::sort(value.region_keys);
          } else if constexpr (std::is_same_v<Value, RegionFilterNode>) {
            if (const auto found = remapping.find(value.input); found != remapping.end()) {
              value.input = found->second;
            }
            if (const auto found = remapping.find(value.selection); found != remapping.end()) {
              value.selection = found->second;
            }
          }
        },
        duplicate.definition);
    if (!definitionReferencesExistingNodes(*this, duplicate.definition, remapping)) {
      result.accepted = false;
      result.rejection = DuplicateRejection::invalid_reference;
      result.mapping.clear();
      return result;
    }
    duplicates.push_back(std::move(duplicate));
  }

  nodes_.insert(nodes_.end(),
                std::make_move_iterator(duplicates.begin()),
                std::make_move_iterator(duplicates.end()));
  next_id_ += result.mapping.size();
  return result;
}

RemoveResult Document::removeSelected(
    const std::vector<NodeId>& selected,
    const RemovePolicy policy) {
  RemoveResult result{true, RemoveRejection::none, {}};
  std::unordered_set<NodeId> removed;
  removed.reserve(selected.size());
  for (const NodeId id : selected) {
    if (findNode(id) == nullptr) {
      result.accepted = false;
      result.rejection = RemoveRejection::unknown_id;
      return result;
    }
    if (!removed.insert(id).second) {
      result.accepted = false;
      result.rejection = RemoveRejection::duplicate_id;
      return result;
    }
  }
  if (selected.empty()) {
    return result;
  }

  if (policy == RemovePolicy::reject_if_referenced) {
    for (const auto& node : nodes_) {
      if (!removed.contains(node.id) &&
          definitionReferencesRemovedNode(node.definition, removed)) {
        result.accepted = false;
        result.rejection = RemoveRejection::externally_referenced;
        return result;
      }
    }
  } else {
    bool expanded = true;
    while (expanded) {
      expanded = false;
      for (const auto& node : nodes_) {
        if (!removed.contains(node.id) &&
            definitionReferencesRemovedNode(node.definition, removed)) {
          removed.insert(node.id);
          expanded = true;
        }
      }
    }
  }

  std::unordered_set<NodeId> remaining_ids;
  remaining_ids.reserve(nodes_.size() - removed.size());
  for (const auto& node : nodes_) {
    if (!removed.contains(node.id)) {
      remaining_ids.insert(node.id);
    }
  }
  for (const auto& node : nodes_) {
    if (removed.contains(node.id)) {
      continue;
    }
    bool valid = true;
    visitInputs(node.definition, [&](const NodeId input) {
      valid = valid && remaining_ids.contains(input);
    });
    if (!valid) {
      result.accepted = false;
      result.rejection = RemoveRejection::invalid_reference;
      return result;
    }
    result.removed_ids.push_back(node.id);
  }

  result.removed_ids.clear();
  std::vector<Node> remaining;
  remaining.reserve(nodes_.size() - removed.size());
  for (auto& node : nodes_) {
    if (removed.contains(node.id)) {
      result.removed_ids.push_back(node.id);
    } else {
      remaining.push_back(std::move(node));
    }
  }
  nodes_ = std::move(remaining);
  return result;
}

AtomicApplyResult Document::applyAtomic(
    const std::function<AtomicApplyResult(Document&)>& builder) {
  if (!builder) {
    return AtomicApplyResult{false, "Atomic builder is empty"};
  }

  Document next = *this;
  AtomicApplyResult result;
  try {
    result = builder(next);
  } catch (const std::exception& error) {
    return AtomicApplyResult{false, error.what()};
  }
  if (!result.accepted) {
    return result;
  }
  *this = std::move(next);
  return result;
}

DocumentHistory::DocumentHistory(Document document)
    : current_(std::move(document)), next_id_high_water_(current_.next_id_) {}

const Document& DocumentHistory::document() const noexcept { return current_; }

bool DocumentHistory::canUndo() const noexcept { return !undo_stack_.empty(); }

bool DocumentHistory::canRedo() const noexcept { return !redo_stack_.empty(); }

std::uint64_t DocumentHistory::revision() const noexcept { return revision_; }

NodeId DocumentHistory::addPrimitive(
    std::string name,
    Primitive primitive,
    const Transform transform) {
  Document next = current_;
  preserveNodeIdHighWater(next);
  const NodeId id = next.addPrimitive(std::move(name), std::move(primitive), transform);
  next_id_high_water_ = next.next_id_;
  commit(std::move(next));
  return id;
}

NodeId DocumentHistory::addBoolean(
    std::string name,
    const BooleanOperation operation,
    const NodeId left,
    const NodeId right) {
  Document next = current_;
  preserveNodeIdHighWater(next);
  const NodeId id = next.addBoolean(std::move(name), operation, left, right);
  next_id_high_water_ = next.next_id_;
  commit(std::move(next));
  return id;
}

NodeId DocumentHistory::addSymmetry(
    std::string name,
    const NodeId input,
    const SymmetryAxis axis) {
  Document next = current_;
  preserveNodeIdHighWater(next);
  const NodeId id = next.addSymmetry(std::move(name), input, axis);
  next_id_high_water_ = next.next_id_;
  commit(std::move(next));
  return id;
}

NodeId DocumentHistory::addSymmetry(
    std::string name,
    const NodeId input,
    const Point axis_origin,
    const Point axis_direction) {
  return addSymmetry(std::move(name), input, SymmetryAxis{axis_origin, axis_direction});
}

NodeId DocumentHistory::addSplit(
    std::string name,
    const NodeId input,
    const SymmetryAxis axis) {
  Document next = current_;
  preserveNodeIdHighWater(next);
  const NodeId id = next.addSplit(std::move(name), input, axis);
  next_id_high_water_ = next.next_id_;
  commit(std::move(next));
  return id;
}

NodeId DocumentHistory::addSplit(
    std::string name,
    const NodeId input,
    const Point axis_origin,
    const Point axis_direction) {
  return addSplit(std::move(name), input, SymmetryAxis{axis_origin, axis_direction});
}

NodeId DocumentHistory::addRegionSelection(
    std::string name,
    const NodeId input,
    std::vector<RegionKey> region_keys) {
  Document next = current_;
  preserveNodeIdHighWater(next);
  const NodeId id = next.addRegionSelection(std::move(name), input, std::move(region_keys));
  next_id_high_water_ = next.next_id_;
  commit(std::move(next));
  return id;
}

NodeId DocumentHistory::addRegionFilter(
    std::string name,
    const NodeId input,
    const NodeId selection,
    const RegionFilterMode mode) {
  Document next = current_;
  preserveNodeIdHighWater(next);
  const NodeId id = next.addRegionFilter(std::move(name), input, selection, mode);
  next_id_high_water_ = next.next_id_;
  commit(std::move(next));
  return id;
}

RegionFilterResult DocumentHistory::addRegionSelectionAndFilter(
    std::string selection_name,
    std::string filter_name,
    const NodeId split,
    std::vector<RegionKey> region_keys,
    const RegionFilterMode mode) {
  Document next = current_;
  preserveNodeIdHighWater(next);
  auto result = next.addRegionSelectionAndFilter(
      std::move(selection_name),
      std::move(filter_name),
      split,
      std::move(region_keys),
      mode);
  if (!result.accepted) {
    return result;
  }
  next_id_high_water_ = next.next_id_;
  commit(std::move(next));
  return result;
}

bool DocumentHistory::setTransform(const NodeId id, const Transform transform) {
  const auto* node = current_.findNode(id);
  if (node == nullptr || !std::holds_alternative<PrimitiveNode>(node->definition)) {
    return false;
  }
  validateTransform(transform);
  const auto result = applyTransforms({TransformUpdate{id, transform}});
  return result.accepted;
}

TransformBatchResult DocumentHistory::applyTransforms(
    const std::vector<TransformUpdate>& updates) {
  Document next = current_;
  auto result = next.applyTransforms(updates);
  if (!result.accepted || !result.changed) {
    return result;
  }
  commit(std::move(next));
  return result;
}

DuplicateResult DocumentHistory::duplicateSelected(const std::vector<NodeId>& selected) {
  Document next = current_;
  preserveNodeIdHighWater(next);
  auto result = next.duplicateSelected(selected);
  if (!result.accepted || result.mapping.empty()) {
    return result;
  }
  next_id_high_water_ = next.next_id_;
  commit(std::move(next));
  return result;
}

RemoveResult DocumentHistory::removeSelected(
    const std::vector<NodeId>& selected,
    const RemovePolicy policy) {
  Document next = current_;
  preserveNodeIdHighWater(next);
  auto result = next.removeSelected(selected, policy);
  if (!result.accepted || result.removed_ids.empty()) {
    return result;
  }
  commit(std::move(next));
  return result;
}

AtomicApplyResult DocumentHistory::applyAtomic(
    const std::function<AtomicApplyResult(Document&)>& builder) {
  if (!builder) {
    return AtomicApplyResult{false, "Atomic builder is empty"};
  }

  Document next = current_;
  preserveNodeIdHighWater(next);
  AtomicApplyResult result;
  try {
    result = builder(next);
  } catch (const std::exception& error) {
    return AtomicApplyResult{false, error.what()};
  }
  if (!result.accepted) {
    return result;
  }

  preserveNodeIdHighWater(next);
  next_id_high_water_ = next.next_id_;
  commit(std::move(next));
  return result;
}

bool DocumentHistory::undo() {
  if (!canUndo()) {
    return false;
  }
  redo_stack_.push_back(std::move(current_));
  current_ = std::move(undo_stack_.back());
  undo_stack_.pop_back();
  preserveNodeIdHighWater(current_);
  ++revision_;
  return true;
}

bool DocumentHistory::redo() {
  if (!canRedo()) {
    return false;
  }
  undo_stack_.push_back(std::move(current_));
  current_ = std::move(redo_stack_.back());
  redo_stack_.pop_back();
  preserveNodeIdHighWater(current_);
  ++revision_;
  return true;
}

void DocumentHistory::commit(Document next) {
  undo_stack_.push_back(std::move(current_));
  current_ = std::move(next);
  redo_stack_.clear();
  ++revision_;
}

void DocumentHistory::preserveNodeIdHighWater(Document& document) const noexcept {
  document.next_id_ = std::max(document.next_id_, next_id_high_water_);
}

}  // namespace signet::core
