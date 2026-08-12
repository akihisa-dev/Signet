// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <compare>
#include <functional>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace signet::core {

using NodeId = std::uint64_t;

struct Point final {
  double x{};
  double y{};

  friend bool operator==(const Point&, const Point&) = default;
};

struct Transform final {
  Point translation{};
  double rotation_degrees{};
  Point scale{1.0, 1.0};

  friend bool operator==(const Transform&, const Transform&) = default;
};

struct Circle final {
  double radius{1.0};
};

struct Rectangle final {
  double width{1.0};
  double height{1.0};
};

struct GoldenRectangle final {
  // Persistence stores only the defining short side.  The long side is
  // derived at evaluation time from the mathematical golden ratio.
  double short_side{1.0};

  [[nodiscard]] constexpr double longSide() const noexcept {
    return short_side * std::numbers::phi_v<double>;
  }
};

struct Arc final {
  double radius{1.0};
  double start_degrees{};
  double sweep_degrees{90.0};
};

using Primitive = std::variant<Circle, Rectangle, GoldenRectangle, Arc>;

enum class BooleanOperation : std::uint8_t {
  unite,
  intersect,
  subtract,
  exclusive_or,
};

// Region keys are document identities, not arrangement snapshot identities.
// They intentionally contain no coordinates, CGAL handles, or FaceId values.
enum class RegionCutterSide : std::uint8_t {
  negative,
  positive,
  on_boundary,
};

struct RegionExpressionTerm final {
  enum class Kind : std::uint8_t {
    leaf,
    boolean,
  };

  Kind kind{Kind::leaf};
  NodeId node_id{};
  BooleanOperation operation{BooleanOperation::unite};

  friend bool operator==(const RegionExpressionTerm&, const RegionExpressionTerm&) = default;
  friend auto operator<=>(const RegionExpressionTerm&, const RegionExpressionTerm&) = default;
};

struct RegionBoundaryProvenance final {
  NodeId source_node_id{};
  std::uint32_t curve_index{};

  friend bool operator==(const RegionBoundaryProvenance&, const RegionBoundaryProvenance&) =
      default;
  friend auto operator<=>(const RegionBoundaryProvenance&, const RegionBoundaryProvenance&) =
      default;
};

using RegionBoundaryLoop = std::vector<RegionBoundaryProvenance>;

struct RegionKey final {
  NodeId split_node_id{};
  // Post-order, normalized construction expression. Leaf terms identify the
  // source node; boolean terms identify the operation joining its two prior
  // expression subtrees. The evaluator emits this in document order.
  std::vector<RegionExpressionTerm> construction_expression;
  RegionCutterSide cutter_side{RegionCutterSide::on_boundary};
  // Each loop is canonicalized independently (orientation-independent,
  // cyclically rotated), then loops are sorted lexicographically.
  std::vector<RegionBoundaryLoop> boundary_provenance;

  friend bool operator==(const RegionKey&, const RegionKey&) = default;
  friend auto operator<=>(const RegionKey&, const RegionKey&) = default;
};

enum class RegionFilterMode : std::uint8_t {
  keep_selected,
  remove_selected,
};

struct PrimitiveNode final {
  Primitive primitive;
  Transform transform{};
};

struct BooleanNode final {
  BooleanOperation operation{BooleanOperation::unite};
  NodeId left{};
  NodeId right{};
};

struct SymmetryAxis final {
  Point origin{};
  Point direction{1.0, 0.0};

  friend bool operator==(const SymmetryAxis&, const SymmetryAxis&) = default;
};

struct SymmetryNode final {
  NodeId input{};
  SymmetryAxis axis{};
};

struct SplitNode final {
  NodeId input{};
  SymmetryAxis axis{};
};

struct RegionSelectionNode final {
  NodeId input{};
  std::vector<RegionKey> region_keys;

  [[nodiscard]] const std::vector<RegionKey>& keys() const noexcept { return region_keys; }
  [[nodiscard]] const std::vector<RegionKey>& regions() const noexcept { return region_keys; }
};

struct RegionFilterNode final {
  NodeId input{};
  NodeId selection{};
  RegionFilterMode mode{RegionFilterMode::keep_selected};
};

using NodeDefinition =
    std::variant<PrimitiveNode,
                 BooleanNode,
                 SymmetryNode,
                 SplitNode,
                 RegionSelectionNode,
                 RegionFilterNode>;

struct Node final {
  NodeId id{};
  std::string name;
  NodeDefinition definition;
  bool visible{true};
};

struct TransformUpdate final {
  NodeId id{};
  Transform after{};
};

using TransformChange = TransformUpdate;

enum class TransformBatchRejection : std::uint8_t {
  none,
  unknown_id,
  duplicate_id,
  operation_node,
  invalid_transform,
};

struct TransformBatchResult final {
  bool accepted{false};
  bool changed{false};
  TransformBatchRejection rejection{TransformBatchRejection::none};

  [[nodiscard]] explicit operator bool() const noexcept { return accepted; }
};

enum class DuplicateRejection : std::uint8_t {
  none,
  unknown_id,
  duplicate_id,
  invalid_reference,
  id_exhausted,
};

struct DuplicateResult final {
  bool accepted{false};
  DuplicateRejection rejection{DuplicateRejection::none};
  std::vector<std::pair<NodeId, NodeId>> mapping;

  [[nodiscard]] explicit operator bool() const noexcept { return accepted; }
};

enum class RemovePolicy : std::uint8_t {
  reject_if_referenced,
  cascade_dependents,
};

enum class RemoveRejection : std::uint8_t {
  none,
  unknown_id,
  duplicate_id,
  externally_referenced,
  invalid_reference,
};

struct RemoveResult final {
  bool accepted{false};
  RemoveRejection rejection{RemoveRejection::none};
  std::vector<NodeId> removed_ids;

  [[nodiscard]] explicit operator bool() const noexcept { return accepted; }
};

enum class RegionFilterRejection : std::uint8_t {
  none,
  invalid_split,
  empty_region_keys,
  duplicate_region_key,
  invalid_region_key,
  invalid_mode,
  id_exhausted,
};

struct RegionFilterResult final {
  bool accepted{false};
  RegionFilterRejection rejection{RegionFilterRejection::none};
  NodeId selection_id{};
  NodeId filter_id{};

  [[nodiscard]] explicit operator bool() const noexcept { return accepted; }
};

struct AtomicApplyResult final {
  bool accepted{false};
  std::string reason;

  [[nodiscard]] explicit operator bool() const noexcept { return accepted; }
};

class Document final {
 public:
  static constexpr std::uint32_t current_schema_version = 1;

  explicit Document(std::string name);

  [[nodiscard]] std::uint32_t schemaVersion() const noexcept;
  [[nodiscard]] const std::string& name() const noexcept;
  [[nodiscard]] const std::vector<Node>& nodes() const noexcept;
  [[nodiscard]] const Node* findNode(NodeId id) const noexcept;

  NodeId addPrimitive(std::string name, Primitive primitive, Transform transform = {});
  NodeId addBoolean(std::string name, BooleanOperation operation, NodeId left, NodeId right);
  NodeId addSymmetry(std::string name, NodeId input, SymmetryAxis axis);
  NodeId addSymmetry(std::string name, NodeId input, Point axis_origin, Point axis_direction);
  NodeId addSplit(std::string name, NodeId input, SymmetryAxis axis);
  NodeId addSplit(std::string name, NodeId input, Point axis_origin, Point axis_direction);
  NodeId addRegionSelection(std::string name, NodeId input, std::vector<RegionKey> region_keys);
  NodeId addRegionFilter(
      std::string name,
      NodeId input,
      NodeId selection,
      RegionFilterMode mode);
  RegionFilterResult addRegionSelectionAndFilter(
      std::string selection_name,
      std::string filter_name,
      NodeId split,
      std::vector<RegionKey> region_keys,
      RegionFilterMode mode);
  bool setTransform(NodeId id, Transform transform);
  TransformBatchResult applyTransforms(const std::vector<TransformUpdate>& updates);
  TransformBatchResult applyTransformBatch(const std::vector<TransformUpdate>& updates) {
    return applyTransforms(updates);
  }
  DuplicateResult duplicateSelected(const std::vector<NodeId>& selected);
  DuplicateResult duplicateSubgraph(const std::vector<NodeId>& selected) {
    return duplicateSelected(selected);
  }
  RemoveResult removeSelected(
      const std::vector<NodeId>& selected,
      RemovePolicy policy);
  AtomicApplyResult applyAtomic(const std::function<AtomicApplyResult(Document&)>& builder);

 private:
  friend class DocumentHistory;

  std::uint32_t schema_version_{current_schema_version};
  std::string name_;
  std::vector<Node> nodes_;
  NodeId next_id_{1};
};

class DocumentHistory final {
 public:
  explicit DocumentHistory(Document document);

  [[nodiscard]] const Document& document() const noexcept;
  [[nodiscard]] bool canUndo() const noexcept;
  [[nodiscard]] bool canRedo() const noexcept;
  [[nodiscard]] std::uint64_t revision() const noexcept;

  NodeId addPrimitive(std::string name, Primitive primitive, Transform transform = {});
  NodeId addBoolean(std::string name, BooleanOperation operation, NodeId left, NodeId right);
  NodeId addSymmetry(std::string name, NodeId input, SymmetryAxis axis);
  NodeId addSymmetry(std::string name, NodeId input, Point axis_origin, Point axis_direction);
  NodeId addSplit(std::string name, NodeId input, SymmetryAxis axis);
  NodeId addSplit(std::string name, NodeId input, Point axis_origin, Point axis_direction);
  NodeId addRegionSelection(std::string name, NodeId input, std::vector<RegionKey> region_keys);
  NodeId addRegionFilter(
      std::string name,
      NodeId input,
      NodeId selection,
      RegionFilterMode mode);
  RegionFilterResult addRegionSelectionAndFilter(
      std::string selection_name,
      std::string filter_name,
      NodeId split,
      std::vector<RegionKey> region_keys,
      RegionFilterMode mode);
  bool setTransform(NodeId id, Transform transform);
  TransformBatchResult applyTransforms(const std::vector<TransformUpdate>& updates);
  TransformBatchResult applyTransformBatch(const std::vector<TransformUpdate>& updates) {
    return applyTransforms(updates);
  }
  DuplicateResult duplicateSelected(const std::vector<NodeId>& selected);
  DuplicateResult duplicateSubgraph(const std::vector<NodeId>& selected) {
    return duplicateSelected(selected);
  }
  RemoveResult removeSelected(
      const std::vector<NodeId>& selected,
      RemovePolicy policy);
  AtomicApplyResult applyAtomic(const std::function<AtomicApplyResult(Document&)>& builder);
  bool undo();
  bool redo();

 private:
  void commit(Document next);
  void preserveNodeIdHighWater(Document& document) const noexcept;

  Document current_;
  std::vector<Document> undo_stack_;
  std::vector<Document> redo_stack_;
  NodeId next_id_high_water_{};
  std::uint64_t revision_{};
};

}  // namespace signet::core
