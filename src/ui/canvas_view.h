// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "core/document.h"
#include "geometry/document_evaluator.h"
#include "ui/editor_interaction.h"

#include <QLineF>
#include <QPointF>
#include <QString>
#include <QWidget>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace signet::ui {

struct CircleGuideOverlay final {
  core::NodeId node_id{};
  QPointF center_view;
  QPointF radius_endpoint_view;
  double radius{};
  QString radius_label;

  friend bool operator==(const CircleGuideOverlay&, const CircleGuideOverlay&) = default;
};

enum class SnapGuideKind : std::uint8_t {
  grid,
  center,
  geometry,
};

struct SnapGuideOverlay final {
  SnapGuideKind kind{SnapGuideKind::grid};
  QPointF source_view;
  QPointF target_view;
  QString label;

  friend bool operator==(const SnapGuideOverlay&, const SnapGuideOverlay&) = default;
};

class CanvasView final : public QWidget {
  Q_OBJECT

 public:
  enum class Tool { select, circle, rectangle, arc, golden_rectangle, split };

  explicit CanvasView(core::DocumentHistory& history, QWidget* parent = nullptr);

  [[nodiscard]] QPointF documentToView(QPointF point) const;
  [[nodiscard]] QPointF viewToDocument(QPointF point) const;
  [[nodiscard]] double zoom() const noexcept { return zoom_; }
  [[nodiscard]] QPointF panOffset() const noexcept { return pan_offset_; }
  [[nodiscard]] std::optional<core::NodeId> selectedNodeId() const noexcept {
    return selected_node_ids_.empty() ? std::nullopt
                                      : std::optional<core::NodeId>(selected_node_ids_.front());
  }
  [[nodiscard]] const std::vector<core::NodeId>& selectedNodeIds() const noexcept {
    return selected_node_ids_;
  }
  [[nodiscard]] bool hasTransientInteraction() const noexcept;
  [[nodiscard]] bool hasSelectedRegions() const noexcept {
    return !selected_region_keys_.empty();
  }
  [[nodiscard]] const std::vector<core::RegionKey>& selectedRegionKeys() const noexcept {
    return selected_region_keys_;
  }
  [[nodiscard]] std::optional<core::NodeId> selectedRegionSplitId() const noexcept {
    return selected_region_split_id_;
  }
  [[nodiscard]] std::optional<core::SymmetryAxis> splitPreviewAxis() const noexcept {
    return split_preview_axis_;
  }
  [[nodiscard]] std::size_t evaluatedCircleCount() const noexcept {
    return evaluation_.circles.size();
  }
  [[nodiscard]] std::size_t evaluatedCurveSetCount() const noexcept {
    return evaluation_.curve_sets.size();
  }
  [[nodiscard]] std::size_t evaluatedSplitCount() const noexcept {
    return evaluation_.splits.size();
  }
  [[nodiscard]] std::size_t diagnosticCount() const noexcept {
    return evaluation_.diagnostics.size();
  }
  [[nodiscard]] QPointF circleCenter(core::NodeId node_id) const;
  [[nodiscard]] std::optional<CircleGuideOverlay> circleGuideOverlay() const;
  [[nodiscard]] std::vector<CircleGuideOverlay> circleGuideOverlays() const;
  [[nodiscard]] std::vector<SnapGuideOverlay> snapGuideOverlays() const;
  [[nodiscard]] Tool tool() const noexcept { return tool_; }
  [[nodiscard]] bool hasPlacementPreview() const noexcept;
  [[nodiscard]] std::size_t placementPointCount() const noexcept { return placement_points_.size(); }
  [[nodiscard]] std::optional<interaction::HandleLayout> selectionHandleLayout() const;

  void refreshFromDocument();
  void setSelectedNode(std::optional<core::NodeId> node_id);
  void setSelectedNodes(std::vector<core::NodeId> node_ids);
  void setTool(Tool tool);
  void duplicateSelection();
  void deleteSelection();
  void flipSelectionHorizontal();
  void flipSelectionVertical();

 signals:
  void selectionChanged();
  void documentChanged();
  void statusMessage(QString message);

 protected:
  bool event(QEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;

 private:
  enum class Interaction {
    none,
    drag,
    pan,
    place_circle,
    place_rectangle,
    place_arc,
    place_golden,
    split,
    resize_uniform,
    resize_rectangle,
    rotate,
  };

  struct PreviewTransform final {
    core::NodeId node_id{};
    core::Transform origin{};
    core::Transform current{};
  };

  struct RegionHit final {
    core::NodeId split_node_id{};
    core::RegionKey key{};
  };

  [[nodiscard]] std::optional<core::NodeId> primitiveAt(QPointF point) const;
  [[nodiscard]] std::optional<RegionHit> regionAt(QPointF point) const;
  [[nodiscard]] std::optional<core::NodeId> regionContextForNode(
      core::NodeId node_id) const;
  [[nodiscard]] bool isClosedSplitTarget(core::NodeId node_id) const;
  [[nodiscard]] bool validateSplitCandidate(
      core::NodeId target_node_id,
      const core::SymmetryAxis& axis,
      core::NodeId* candidate_split_id,
      QString* failure_message) const;
  [[nodiscard]] std::optional<QLineF> splitPreviewLine() const;
  [[nodiscard]] std::optional<interaction::SelectionBounds> selectionBoundsInView() const;
  [[nodiscard]] std::array<core::Point, 8> selectionHandlePointsInView() const;
  [[nodiscard]] std::optional<interaction::ResizeHandle> resizeHandleAt(QPointF point) const;
  [[nodiscard]] bool rotateHandleAt(QPointF point) const;
  [[nodiscard]] std::vector<interaction::SelectionItem> selectionItems() const;
  [[nodiscard]] std::optional<core::SymmetryAxis> selectionSymmetryAxis(
      interaction::ReflectionAxis axis) const;
  [[nodiscard]] const geometry::EvaluatedCircle* evaluatedCircle(core::NodeId node_id) const;
  [[nodiscard]] std::optional<core::Transform> previewTransform(core::NodeId node_id) const;
  [[nodiscard]] bool selectionIsPrimitive() const;
  [[nodiscard]] std::vector<core::NodeId> normalizedSelection(
      const std::vector<core::NodeId>& node_ids) const;
  void beginPrimitiveDrag(QPointF document_point, Qt::KeyboardModifiers modifiers);
  void beginResize(interaction::ResizeHandle handle, QPointF view_point);
  void beginRotate(QPointF view_point);
  void updateTransformPreview(QPointF view_point, Qt::KeyboardModifiers modifiers = {});
  void commitTransformPreview();
  void updatePlacementPreview(
      QPointF document_point,
      Qt::KeyboardModifiers modifiers = {});
  void commitPlacement();
  void beginSplit(QPointF document_point);
  void updateSplitPreview(QPointF document_point);
  void commitSplit();
  void toggleRegionSelection(const RegionHit& hit, Qt::KeyboardModifiers modifiers);
  void clearRegionSelection();
  void deleteSelectedRegions();
  void cancelTransientInteraction();
  void commitKeyboardMove(QPointF delta);
  [[nodiscard]] core::Point snapDocumentPoint(
      core::Point point,
      Qt::KeyboardModifiers modifiers);
  void updateDragPreview(QPointF document_point, Qt::KeyboardModifiers modifiers);

  core::DocumentHistory& history_;
  geometry::DocumentEvaluationSnapshot evaluation_;
  std::vector<core::NodeId> selected_node_ids_;
  std::optional<core::NodeId> selected_region_split_id_;
  std::vector<core::RegionKey> selected_region_keys_;
  Interaction interaction_{Interaction::none};
  Tool tool_{Tool::select};
  QPointF interaction_start_view_;
  QPointF interaction_start_document_;
  std::vector<PreviewTransform> preview_transforms_;
  std::vector<interaction::SelectionItem> gesture_items_;
  interaction::SelectionBounds gesture_bounds_{};
  interaction::ResizeHandle gesture_handle_{interaction::ResizeHandle::top_left};
  QPointF gesture_fixed_view_;
  QPointF gesture_start_view_;
  QPointF gesture_pivot_view_;
  core::Point gesture_fixed_local_{};
  core::Point gesture_moving_local_{};
  bool gesture_is_edge_{};
  std::vector<core::Point> placement_points_;
  std::optional<core::Point> placement_cursor_;
  std::optional<interaction::PrimitivePlacement> placement_preview_;
  std::optional<core::SymmetryAxis> split_preview_axis_;
  std::optional<core::NodeId> split_target_node_id_;
  std::optional<SnapGuideOverlay> snap_guide_;
  QPointF pan_offset_;
  QPointF pan_origin_;
  bool space_down_{};
  double zoom_{2.0};
};

}  // namespace signet::ui
