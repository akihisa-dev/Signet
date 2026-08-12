// SPDX-License-Identifier: AGPL-3.0-or-later
#include "core/document.h"
#include "ui/canvas_view.h"
#include "ui/main_window.h"

#include <QApplication>
#include <QFocusEvent>
#include <QImage>
#include <QKeyEvent>
#include <QKeySequence>
#include <QListWidget>
#include <QMouseEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>

namespace {

bool closeEnough(const QPointF left, const QPointF right, const double epsilon = 1e-9) {
  return std::abs(left.x() - right.x()) < epsilon && std::abs(left.y() - right.y()) < epsilon;
}

signet::core::Document makeDocument() {
  signet::core::Document document("UI test");
  document.addPrimitive("Circle 1", signet::core::Circle{90.0},
                        signet::core::Transform{signet::core::Point{-55.0, 0.0}});
  document.addPrimitive("Circle 2", signet::core::Circle{90.0},
                        signet::core::Transform{signet::core::Point{55.0, 0.0}});
  return document;
}

signet::core::Document makeInteractionDocument() {
  signet::core::Document document("Interaction UI test");
  document.addPrimitive("Circle", signet::core::Circle{8.0},
                        signet::core::Transform{signet::core::Point{-20.0, 0.0}});
  document.addPrimitive("Rectangle", signet::core::Rectangle{12.0, 8.0},
                        signet::core::Transform{signet::core::Point{20.0, 0.0}});
  return document;
}

signet::core::Transform transformOf(
    const signet::core::DocumentHistory& history,
    const signet::core::NodeId node_id) {
  const auto* node = history.document().findNode(node_id);
  assert(node != nullptr);
  const auto* primitive = std::get_if<signet::core::PrimitiveNode>(&node->definition);
  assert(primitive != nullptr);
  return primitive->transform;
}

void sendDrag(
    signet::ui::CanvasView& canvas,
    const QPointF start,
    const QPointF delta) {
  const QPointF end = start + delta;
  QMouseEvent press(
      QEvent::MouseButtonPress, start, start, Qt::LeftButton, Qt::LeftButton, {});
  QApplication::sendEvent(&canvas, &press);
  QMouseEvent move(
      QEvent::MouseMove, end, end, Qt::NoButton, Qt::LeftButton, {});
  QApplication::sendEvent(&canvas, &move);
  QMouseEvent release(
      QEvent::MouseButtonRelease, end, end, Qt::LeftButton, {}, {});
  QApplication::sendEvent(&canvas, &release);
}

void sendPressMove(
    signet::ui::CanvasView& canvas,
    const QPointF start,
    const QPointF delta) {
  const QPointF end = start + delta;
  QMouseEvent press(
      QEvent::MouseButtonPress, start, start, Qt::LeftButton, Qt::LeftButton, {});
  QApplication::sendEvent(&canvas, &press);
  QMouseEvent move(
      QEvent::MouseMove, end, end, Qt::NoButton, Qt::LeftButton, {});
  QApplication::sendEvent(&canvas, &move);
}

void sendPressMoveWithModifiers(
    signet::ui::CanvasView& canvas,
    const QPointF start,
    const QPointF delta,
    const Qt::KeyboardModifiers modifiers) {
  const QPointF end = start + delta;
  QMouseEvent press(
      QEvent::MouseButtonPress, start, start, Qt::LeftButton, Qt::LeftButton, modifiers);
  QApplication::sendEvent(&canvas, &press);
  QMouseEvent move(
      QEvent::MouseMove, end, end, Qt::NoButton, Qt::LeftButton, modifiers);
  QApplication::sendEvent(&canvas, &move);
}

void sendClick(
    signet::ui::CanvasView& canvas,
    const QPointF point,
    const Qt::KeyboardModifiers modifiers = {}) {
  QMouseEvent press(
      QEvent::MouseButtonPress, point, point, Qt::LeftButton, Qt::LeftButton, modifiers);
  QApplication::sendEvent(&canvas, &press);
  QMouseEvent release(
      QEvent::MouseButtonRelease, point, point, Qt::LeftButton, {}, modifiers);
  QApplication::sendEvent(&canvas, &release);
}

void sendDocumentDrag(
    signet::ui::CanvasView& canvas,
    const QPointF start_document,
    const QPointF end_document) {
  sendDrag(canvas, canvas.documentToView(start_document),
           canvas.documentToView(end_document) - canvas.documentToView(start_document));
}

void sendPan(
    signet::ui::CanvasView& canvas,
    const QPointF start,
    const QPointF delta) {
  const QPointF end = start + delta;
  QMouseEvent press(
      QEvent::MouseButtonPress, start, start, Qt::MiddleButton, Qt::MiddleButton, {});
  QApplication::sendEvent(&canvas, &press);
  QMouseEvent move(
      QEvent::MouseMove, end, end, Qt::NoButton, Qt::MiddleButton, {});
  QApplication::sendEvent(&canvas, &move);
  QMouseEvent release(
      QEvent::MouseButtonRelease, end, end, Qt::MiddleButton, {}, {});
  QApplication::sendEvent(&canvas, &release);
}

signet::core::Document makeSplitUiDocument() {
  signet::core::Document document("Split UI test");
  document.addPrimitive(
      "Rectangle", signet::core::Rectangle{40.0, 20.0},
      signet::core::Transform{signet::core::Point{}});
  return document;
}

signet::geometry::EvaluatedSplit evaluatedSplit(
    const signet::core::DocumentHistory& history,
    const signet::core::NodeId split_id) {
  const auto snapshot = signet::geometry::DocumentEvaluator::evaluate(history.document());
  const auto found = std::ranges::find_if(
      snapshot.splits,
      [split_id](const signet::geometry::EvaluatedSplit& split) {
        return split.node_id == split_id;
      });
  assert(found != snapshot.splits.end());
  return *found;
}

void testMultiSelectionAndCommands() {
  signet::core::Document document = makeInteractionDocument();
  const signet::core::NodeId first = 1;
  const signet::core::NodeId second = 2;
  const signet::core::NodeId operation = document.addBoolean(
      "Union", signet::core::BooleanOperation::unite, first, second);
  signet::core::DocumentHistory history(std::move(document));
  signet::ui::CanvasView canvas(history);
  canvas.resize(800, 600);
  canvas.show();

  canvas.setSelectedNodes({second, first, second, 9999});
  assert((canvas.selectedNodeIds() == std::vector<signet::core::NodeId>{first, second}));
  const auto first_before_arrow = transformOf(history, first);
  const auto second_before_arrow = transformOf(history, second);
  QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, {});
  QApplication::sendEvent(&canvas, &right);
  assert(transformOf(history, first).translation.x ==
         first_before_arrow.translation.x + 1.0);
  assert(transformOf(history, second).translation.x ==
         second_before_arrow.translation.x + 1.0);
  assert(history.canUndo());
  assert(history.undo());
  canvas.refreshFromDocument();
  assert(transformOf(history, first) == first_before_arrow);
  assert(transformOf(history, second) == second_before_arrow);
  assert(!history.canUndo());

  canvas.setSelectedNodes({first, second});
  const auto first_before_drag = transformOf(history, first);
  const auto second_before_drag = transformOf(history, second);
  sendDocumentDrag(canvas, {-20.0, 0.0}, {-12.0, 4.0});
  assert((transformOf(history, first).translation == signet::core::Point{-12.0, 4.0}));
  assert((transformOf(history, second).translation == signet::core::Point{28.0, 4.0}));
  assert(history.canUndo());
  assert(history.undo());
  canvas.refreshFromDocument();
  assert(transformOf(history, first) == first_before_drag);
  assert(transformOf(history, second) == second_before_drag);
  assert(!history.canUndo());

  canvas.setSelectedNodes({first, operation});
  const auto first_before_rejection = transformOf(history, first);
  sendPressMove(canvas, canvas.documentToView(QPointF{-20.0, 0.0}), QPointF{12.0, 6.0});
  assert(!canvas.hasTransientInteraction());
  assert(transformOf(history, first) == first_before_rejection);
  QApplication::sendEvent(&canvas, &right);
  assert(transformOf(history, first) == first_before_rejection);
  assert(!history.canUndo());

  canvas.setSelectedNode(first);
  const double zoom_before_gesture = canvas.zoom();
  const QPointF pan_before_gesture = canvas.panOffset();
  sendPressMove(canvas, canvas.documentToView(QPointF{-20.0, 0.0}), QPointF{8.0, -4.0});
  assert(canvas.hasTransientInteraction());
  QWheelEvent wheel(
      QPointF{400.0, 300.0}, QPointF{400.0, 300.0}, QPoint{0, 120}, QPoint{0, 120},
      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(&canvas, &wheel);
  QMouseEvent pan_press(
      QEvent::MouseButtonPress, QPointF{100.0, 100.0}, QPointF{100.0, 100.0},
      Qt::MiddleButton, Qt::MiddleButton, {});
  QApplication::sendEvent(&canvas, &pan_press);
  assert(canvas.zoom() == zoom_before_gesture);
  assert(canvas.panOffset() == pan_before_gesture);
  QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, {});
  QApplication::sendEvent(&canvas, &escape);
  assert(!canvas.hasTransientInteraction());
  assert(transformOf(history, first) == first_before_rejection);
  assert(!history.canUndo());
}

void testPlacementGestures() {
  signet::core::DocumentHistory history(signet::core::Document("Placement UI test"));
  signet::ui::CanvasView canvas(history);
  canvas.resize(800, 600);
  canvas.show();

  canvas.setTool(signet::ui::CanvasView::Tool::circle);
  const QPointF circle_start = canvas.documentToView(QPointF{0.0, 0.0});
  sendPressMove(canvas, circle_start, canvas.documentToView(QPointF{10.0, 0.0}) - circle_start);
  assert(canvas.hasTransientInteraction());
  assert(canvas.hasPlacementPreview());
  assert(history.document().nodes().empty());
  QKeyEvent circle_escape(QEvent::KeyPress, Qt::Key_Escape, {});
  QApplication::sendEvent(&canvas, &circle_escape);
  assert(!canvas.hasTransientInteraction());
  assert(!canvas.hasPlacementPreview());
  assert(history.document().nodes().empty());

  sendDocumentDrag(canvas, {0.0, 0.0}, {10.0, 0.0});
  assert(history.document().nodes().size() == 1);
  const auto* circle_node = history.document().findNode(1);
  assert(circle_node != nullptr);
  const auto* circle_primitive =
      std::get_if<signet::core::PrimitiveNode>(&circle_node->definition);
  assert(circle_primitive != nullptr);
  assert(std::holds_alternative<signet::core::Circle>(circle_primitive->primitive));

  canvas.setTool(signet::ui::CanvasView::Tool::rectangle);
  const QPointF rectangle_start = canvas.documentToView(QPointF{-5.0, -4.0});
  sendPressMove(canvas, rectangle_start,
                canvas.documentToView(QPointF{8.0, 6.0}) - rectangle_start);
  assert(canvas.hasPlacementPreview());
  QKeyEvent rectangle_escape(QEvent::KeyPress, Qt::Key_Escape, {});
  QApplication::sendEvent(&canvas, &rectangle_escape);
  assert(history.document().nodes().size() == 1);
  sendDocumentDrag(canvas, {-5.0, -4.0}, {8.0, 6.0});
  assert(history.document().nodes().size() == 2);

  canvas.setTool(signet::ui::CanvasView::Tool::arc);
  sendClick(canvas, canvas.documentToView(QPointF{0.0, 20.0}));
  assert(canvas.placementPointCount() == 1);
  sendClick(canvas, canvas.documentToView(QPointF{5.0, 25.0}));
  assert(canvas.placementPointCount() == 2);
  assert(!canvas.hasPlacementPreview());
  QKeyEvent arc_escape(QEvent::KeyPress, Qt::Key_Escape, {});
  QApplication::sendEvent(&canvas, &arc_escape);
  assert(canvas.placementPointCount() == 0);
  assert(history.document().nodes().size() == 2);

  sendClick(canvas, canvas.documentToView(QPointF{0.0, 20.0}));
  sendClick(canvas, canvas.documentToView(QPointF{5.0, 25.0}));
  sendClick(canvas, canvas.documentToView(QPointF{10.0, 20.0}));
  assert(canvas.placementPointCount() == 0);
  assert(history.document().nodes().size() == 3);
  const auto* arc_node = history.document().findNode(3);
  assert(arc_node != nullptr);
  assert(std::holds_alternative<signet::core::PrimitiveNode>(arc_node->definition));
  assert(std::holds_alternative<signet::core::Arc>(
      std::get<signet::core::PrimitiveNode>(arc_node->definition).primitive));

  sendClick(canvas, canvas.documentToView(QPointF{30.0, 20.0}));
  sendClick(canvas, canvas.documentToView(QPointF{35.0, 20.0}));
  sendClick(canvas, canvas.documentToView(QPointF{40.0, 20.0}));
  assert(canvas.placementPointCount() == 0);
  assert(!canvas.hasPlacementPreview());
  assert(history.document().nodes().size() == 3);

  canvas.setTool(signet::ui::CanvasView::Tool::circle);
  sendClick(canvas, canvas.documentToView(QPointF{50.0, 20.0}));
  assert(history.document().nodes().size() == 3);
  assert(!canvas.hasTransientInteraction());
}

void testSplitGesturesRegionsAndAtomicFiltering() {
  signet::core::DocumentHistory history(makeSplitUiDocument());
  signet::ui::CanvasView canvas(history);
  canvas.resize(800, 600);
  canvas.show();

  const signet::core::NodeId source = 1;
  const QPointF origin = canvas.documentToView(QPointF{0.0, 0.0});
  const QPointF direction_endpoint = canvas.documentToView(QPointF{0.0, 15.0});
  canvas.setSelectedNode(source);
  canvas.setTool(signet::ui::CanvasView::Tool::split);

  sendPressMove(canvas, origin, direction_endpoint - origin);
  assert(canvas.hasTransientInteraction());
  assert(canvas.splitPreviewAxis().has_value());
  assert(canvas.splitPreviewAxis()->origin == signet::core::Point{});
  assert((canvas.splitPreviewAxis()->direction == signet::core::Point{0.0, 15.0}));
  assert(history.document().nodes().size() == 1);

  QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, {});
  QApplication::sendEvent(&canvas, &escape);
  assert(!canvas.hasTransientInteraction());
  assert(!canvas.splitPreviewAxis().has_value());
  assert(history.document().nodes().size() == 1);
  assert(!history.canUndo());

  sendDrag(canvas, origin, QPointF{0.0, 0.0});
  assert(!canvas.hasTransientInteraction());
  assert(history.document().nodes().size() == 1);
  assert(!history.canUndo());

  sendPressMove(canvas, origin, direction_endpoint - origin);
  QEvent ungrab(QEvent::UngrabMouse);
  QApplication::sendEvent(&canvas, &ungrab);
  assert(!canvas.hasTransientInteraction());
  assert(!canvas.splitPreviewAxis().has_value());
  assert(history.document().nodes().size() == 1);
  assert(!history.canUndo());

  sendDocumentDrag(canvas, QPointF{0.0, 0.0}, QPointF{0.0, 15.0});
  assert(!canvas.hasTransientInteraction());
  assert(history.document().nodes().size() == 2);
  assert(canvas.evaluatedSplitCount() == 1);
  const auto* split_node = history.document().findNode(2);
  assert(split_node != nullptr);
  const auto* split_definition = std::get_if<signet::core::SplitNode>(&split_node->definition);
  assert(split_definition != nullptr);
  assert(split_definition->input == source);
  assert(split_definition->axis.origin == signet::core::Point{});
  assert((split_definition->axis.direction == signet::core::Point{0.0, 15.0}));
  assert(history.canUndo());

  assert(history.undo());
  canvas.refreshFromDocument();
  assert(history.document().findNode(2) == nullptr);
  assert(canvas.evaluatedSplitCount() == 0);
  assert(history.redo());
  canvas.refreshFromDocument();
  assert(history.document().findNode(2) != nullptr);
  assert(canvas.evaluatedSplitCount() == 1);

  canvas.setSelectedNode(2);
  canvas.setTool(signet::ui::CanvasView::Tool::select);
  const auto split = evaluatedSplit(history, 2);
  assert(split.status == signet::geometry::SplitStatus::success);
  assert(split.cells.size() == 2);
  std::vector<signet::core::RegionKey> keys;
  for (const auto& cell : split.cells) {
    keys.push_back(cell.key);
  }
  std::ranges::sort(keys);
  assert(!keys.empty());

  sendClick(canvas, canvas.documentToView(QPointF{-10.0, 0.0}));
  assert(canvas.selectedRegionSplitId() == std::optional<signet::core::NodeId>(2));
  assert(canvas.selectedRegionKeys().size() == 1);
  const auto first_interior_key = canvas.selectedRegionKeys().front();
  sendClick(canvas, canvas.documentToView(QPointF{-10.0, 0.0}));
  assert(canvas.selectedRegionKeys().empty());
  assert(!canvas.selectedRegionSplitId().has_value());

  sendClick(canvas, canvas.documentToView(QPointF{10.0, 0.0}));
  assert(canvas.selectedRegionKeys().size() == 1);
  const auto second_interior_key = canvas.selectedRegionKeys().front();
  assert(second_interior_key != first_interior_key);
  sendClick(canvas, canvas.documentToView(QPointF{-10.0, 0.0}), Qt::ShiftModifier);
  assert(canvas.selectedRegionKeys().size() == 2);
  assert(canvas.selectedRegionKeys() == keys);

  const auto clearRegionSelection = [&canvas, split_id = signet::core::NodeId{2}, source] {
    canvas.setSelectedNode(source);
    canvas.setSelectedNode(split_id);
  };
  clearRegionSelection();
  const QPointF boundary = canvas.documentToView(QPointF{0.0, 0.0});
  sendClick(canvas, boundary);
  assert(canvas.selectedRegionKeys() == std::vector<signet::core::RegionKey>{keys.front()});

  clearRegionSelection();
  sendClick(canvas, boundary + QPointF{4.0, 0.0});
  assert(canvas.selectedRegionKeys() == std::vector<signet::core::RegionKey>{keys.front()});

  QWheelEvent wheel(
      QPointF{400.0, 300.0}, QPointF{400.0, 300.0}, QPoint{0, 120}, QPoint{0, 120},
      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(&canvas, &wheel);
  clearRegionSelection();
  sendClick(canvas, canvas.documentToView(QPointF{0.0, 0.0}) + QPointF{4.0, 0.0});
  assert(canvas.selectedRegionKeys() == std::vector<signet::core::RegionKey>{keys.front()});

  clearRegionSelection();
  sendPan(canvas, QPointF{100.0, 100.0}, QPointF{30.0, -20.0});
  clearRegionSelection();
  sendClick(canvas, canvas.documentToView(QPointF{0.0, 0.0}) + QPointF{4.0, 0.0});
  assert(canvas.selectedRegionKeys() == std::vector<signet::core::RegionKey>{keys.front()});

  clearRegionSelection();
  sendClick(canvas, canvas.documentToView(QPointF{10.0, 0.0}));
  assert(canvas.selectedRegionKeys().size() == 1);
  const auto deleted_keys = canvas.selectedRegionKeys();
  const auto before_filter_nodes = history.document().nodes().size();
  QKeyEvent delete_key(QEvent::KeyPress, Qt::Key_Delete, {});
  QApplication::sendEvent(&canvas, &delete_key);
  assert(history.document().nodes().size() == before_filter_nodes + 2);
  assert(history.document().findNode(source) != nullptr);
  assert(history.document().findNode(2) != nullptr);
  const signet::core::Node* selection_node = nullptr;
  const signet::core::Node* filter_node = nullptr;
  for (const auto& node : history.document().nodes()) {
    if (std::holds_alternative<signet::core::RegionSelectionNode>(node.definition)) {
      selection_node = &node;
    }
    if (std::holds_alternative<signet::core::RegionFilterNode>(node.definition)) {
      filter_node = &node;
    }
  }
  assert(selection_node != nullptr);
  assert(filter_node != nullptr);
  const auto& selection_definition =
      std::get<signet::core::RegionSelectionNode>(selection_node->definition);
  const auto& filter_definition =
      std::get<signet::core::RegionFilterNode>(filter_node->definition);
  assert(selection_definition.input == 2);
  assert(selection_definition.region_keys == deleted_keys);
  assert(filter_definition.input == 2);
  assert(filter_definition.selection == selection_node->id);
  assert(filter_definition.mode == signet::core::RegionFilterMode::remove_selected);

  assert(history.undo());
  canvas.refreshFromDocument();
  assert(history.document().nodes().size() == before_filter_nodes);
  assert(history.document().findNode(source) != nullptr);
  assert(history.document().findNode(2) != nullptr);
  assert(std::ranges::none_of(
      history.document().nodes(), [](const signet::core::Node& node) {
        return std::holds_alternative<signet::core::RegionSelectionNode>(node.definition) ||
               std::holds_alternative<signet::core::RegionFilterNode>(node.definition);
      }));
  assert(history.redo());
  canvas.refreshFromDocument();
  assert(history.document().nodes().size() == before_filter_nodes + 2);

  assert(history.undo());
  canvas.refreshFromDocument();
  assert(history.undo());
  canvas.refreshFromDocument();
  assert(history.document().nodes().size() == 1);
  assert(!canvas.hasSelectedRegions());
  assert(!canvas.selectedRegionSplitId().has_value());
  QApplication::sendEvent(&canvas, &delete_key);
  assert(history.document().nodes().size() == 1);
  assert(!history.canUndo());
}

void testSplitRejectsInvalidTargets() {
  signet::core::Document open_document("Open split UI test");
  const auto open_arc = open_document.addPrimitive(
      "Open Arc", signet::core::Arc{10.0, 0.0, 90.0});
  signet::core::DocumentHistory open_history(std::move(open_document));
  signet::ui::CanvasView open_canvas(open_history);
  open_canvas.resize(800, 600);
  open_canvas.show();
  open_canvas.setSelectedNode(open_arc);
  open_canvas.setTool(signet::ui::CanvasView::Tool::split);
  sendDocumentDrag(open_canvas, QPointF{0.0, 0.0}, QPointF{0.0, 10.0});
  assert(open_history.document().nodes().size() == 1);
  assert(!open_history.canUndo());
  assert(!open_canvas.hasTransientInteraction());

  signet::core::Document multiple_document("Multiple split UI test");
  multiple_document.addPrimitive("First", signet::core::Rectangle{20.0, 10.0});
  multiple_document.addPrimitive(
      "Second", signet::core::Rectangle{20.0, 10.0},
      signet::core::Transform{signet::core::Point{30.0, 0.0}});
  signet::core::DocumentHistory multiple_history(std::move(multiple_document));
  signet::ui::CanvasView multiple_canvas(multiple_history);
  multiple_canvas.resize(800, 600);
  multiple_canvas.show();
  multiple_canvas.setSelectedNodes({1, 2});
  multiple_canvas.setTool(signet::ui::CanvasView::Tool::split);
  sendDocumentDrag(multiple_canvas, QPointF{0.0, 0.0}, QPointF{0.0, 10.0});
  assert(multiple_history.document().nodes().size() == 2);
  assert(!multiple_history.canUndo());
  assert(!multiple_canvas.hasTransientInteraction());
}

void testDuplicateDeleteAndObjectSelection() {
  signet::core::DocumentHistory duplicate_history(makeInteractionDocument());
  signet::ui::CanvasView duplicate_canvas(duplicate_history);
  duplicate_canvas.resize(800, 600);
  duplicate_canvas.show();
  duplicate_canvas.setSelectedNodes({2, 1});
  QKeyEvent duplicate(QEvent::KeyPress, Qt::Key_D, Qt::MetaModifier);
  QApplication::sendEvent(&duplicate_canvas, &duplicate);
  assert(duplicate_history.document().nodes().size() == 4);
  assert((duplicate_canvas.selectedNodeIds() ==
          std::vector<signet::core::NodeId>{3, 4}));
  assert(duplicate_history.canUndo());
  QKeyEvent backspace(QEvent::KeyPress, Qt::Key_Backspace, {});
  QApplication::sendEvent(&duplicate_canvas, &backspace);
  assert(duplicate_history.document().nodes().size() == 2);
  assert(duplicate_canvas.selectedNodeIds().empty());
  assert(duplicate_history.canUndo());

  signet::core::Document referenced_document("Referenced delete UI test");
  const auto source = referenced_document.addPrimitive("Source", signet::core::Circle{4.0});
  const auto independent = referenced_document.addPrimitive("Independent", signet::core::Circle{4.0},
                                                             signet::core::Transform{{20.0, 0.0}});
  const auto referenced_operation = referenced_document.addBoolean(
      "Union", signet::core::BooleanOperation::unite, source, independent);
  signet::core::DocumentHistory referenced_history(std::move(referenced_document));
  signet::ui::CanvasView referenced_canvas(referenced_history);
  referenced_canvas.resize(800, 600);
  referenced_canvas.show();
  referenced_canvas.setSelectedNode(source);
  QKeyEvent delete_key(QEvent::KeyPress, Qt::Key_Delete, {});
  QApplication::sendEvent(&referenced_canvas, &delete_key);
  assert(referenced_history.document().nodes().size() == 3);
  assert(!referenced_history.canUndo());
  referenced_canvas.setSelectedNode(referenced_operation);
  QApplication::sendEvent(&referenced_canvas, &backspace);
  assert(referenced_history.document().nodes().size() == 2);
  assert(referenced_canvas.selectedNodeIds().empty());
  assert(referenced_history.canUndo());

  signet::ui::MainWindow window;
  window.canvasView()->setSelectedNodes({2, 1, 2});
  assert((window.canvasView()->selectedNodeIds() ==
          std::vector<signet::core::NodeId>{1, 2}));
  assert(window.objectsList()->item(0)->isSelected());
  assert(window.objectsList()->item(1)->isSelected());
  window.objectsList()->clearSelection();
  assert(window.canvasView()->selectedNodeIds().empty());
  window.objectsList()->item(1)->setSelected(true);
  assert((window.canvasView()->selectedNodeIds() ==
          std::vector<signet::core::NodeId>{2}));
}

void testFlipSnapAndHighDpiContracts() {
  signet::core::Document flip_document("Flip UI test");
  const auto source = flip_document.addPrimitive(
      "Rectangle", signet::core::Rectangle{12.0, 8.0},
      signet::core::Transform{signet::core::Point{10.0, 3.0}});
  signet::core::DocumentHistory flip_history(std::move(flip_document));
  signet::ui::CanvasView flip_canvas(flip_history);
  flip_canvas.resize(800, 600);
  flip_canvas.show();
  flip_canvas.setSelectedNode(source);
  flip_canvas.flipSelectionHorizontal();
  assert(flip_history.document().nodes().size() == 2);
  const auto* horizontal_node = flip_history.document().findNode(2);
  assert(horizontal_node != nullptr);
  const auto* horizontal = std::get_if<signet::core::SymmetryNode>(
      &horizontal_node->definition);
  assert(horizontal != nullptr);
  assert(horizontal->input == source);
  assert((horizontal->axis.origin == signet::core::Point{10.0, 3.0}));
  assert((horizontal->axis.direction == signet::core::Point{0.0, 1.0}));
  assert(flip_history.canUndo());
  assert(flip_canvas.selectedNodeIds() == std::vector<signet::core::NodeId>{2});
  assert(flip_history.undo());
  flip_canvas.refreshFromDocument();
  assert(flip_history.document().nodes().size() == 1);
  assert(flip_history.redo());
  flip_canvas.refreshFromDocument();
  flip_canvas.setSelectedNode(source);
  flip_canvas.flipSelectionVertical();
  const auto* vertical_node = flip_history.document().findNode(3);
  assert(vertical_node != nullptr);
  const auto* vertical = std::get_if<signet::core::SymmetryNode>(
      &vertical_node->definition);
  assert(vertical != nullptr);
  assert((vertical->axis.origin == signet::core::Point{10.0, 3.0}));
  assert((vertical->axis.direction == signet::core::Point{1.0, 0.0}));
  const auto before_invalid_flip = flip_history.document().nodes().size();
  flip_canvas.setSelectedNode(2);
  flip_canvas.flipSelectionHorizontal();
  assert(flip_history.document().nodes().size() == before_invalid_flip);

  signet::core::Document snap_document("Snap UI test");
  const auto moving = snap_document.addPrimitive(
      "Moving", signet::core::Circle{3.0}, signet::core::Transform{});
  snap_document.addPrimitive(
      "Geometry target", signet::core::Circle{3.0},
      signet::core::Transform{signet::core::Point{30.0, 0.0}});
  signet::core::DocumentHistory snap_history(std::move(snap_document));
  signet::ui::CanvasView snap_canvas(snap_history);
  snap_canvas.resize(800, 600);
  snap_canvas.show();
  snap_canvas.setSelectedNode(moving);
  const QPointF snap_start = snap_canvas.documentToView(QPointF{0.0, 0.0});
  const QPointF snap_end = snap_canvas.documentToView(QPointF{9.6, 0.0});
  sendPressMove(snap_canvas, snap_start, snap_end - snap_start);
  assert(snap_canvas.snapGuideOverlays().size() == 1);
  assert(snap_canvas.snapGuideOverlays().front().kind == signet::ui::SnapGuideKind::grid);
  QMouseEvent snap_release(
      QEvent::MouseButtonRelease, snap_end, snap_end, Qt::LeftButton, {}, {});
  QApplication::sendEvent(&snap_canvas, &snap_release);
  assert((transformOf(snap_history, moving).translation == signet::core::Point{10.0, 0.0}));
  assert(snap_history.undo());
  snap_canvas.refreshFromDocument();
  snap_canvas.setSelectedNode(moving);
  sendPressMoveWithModifiers(
      snap_canvas, snap_start, snap_end - snap_start, Qt::AltModifier);
  assert(snap_canvas.snapGuideOverlays().empty());
  QMouseEvent unsnapped_release(
      QEvent::MouseButtonRelease, snap_end, snap_end, Qt::LeftButton, {}, Qt::AltModifier);
  QApplication::sendEvent(&snap_canvas, &unsnapped_release);
  assert(std::abs(transformOf(snap_history, moving).translation.x - 9.6) < 1.0e-9);
  assert(snap_history.undo());
  snap_canvas.refreshFromDocument();
  snap_canvas.setSelectedNode(moving);
  QImage snap_dpi_view(QSize{1600, 1200}, QImage::Format_ARGB32_Premultiplied);
  snap_dpi_view.setDevicePixelRatio(2.0);
  snap_canvas.render(&snap_dpi_view);
  sendPressMove(snap_canvas, snap_start, snap_end - snap_start);
  QMouseEvent dpi_snap_release(
      QEvent::MouseButtonRelease, snap_end, snap_end, Qt::LeftButton, {}, {});
  QApplication::sendEvent(&snap_canvas, &dpi_snap_release);
  assert((transformOf(snap_history, moving).translation == signet::core::Point{10.0, 0.0}));

  signet::core::Document placement_document("Snap placement UI test");
  signet::core::DocumentHistory placement_history(std::move(placement_document));
  signet::ui::CanvasView placement_canvas(placement_history);
  placement_canvas.resize(800, 600);
  placement_canvas.show();
  placement_canvas.setTool(signet::ui::CanvasView::Tool::circle);
  const QPointF placement_start = placement_canvas.documentToView(QPointF{0.0, 0.0});
  const QPointF placement_end = placement_canvas.documentToView(QPointF{9.6, 0.0});
  sendPressMove(placement_canvas, placement_start, placement_end - placement_start);
  assert(placement_canvas.snapGuideOverlays().size() == 1);
  QMouseEvent placement_release(
      QEvent::MouseButtonRelease, placement_end, placement_end, Qt::LeftButton, {}, {});
  QApplication::sendEvent(&placement_canvas, &placement_release);
  assert(placement_history.document().nodes().size() == 1);
  const auto* placed = std::get_if<signet::core::PrimitiveNode>(
      &placement_history.document().nodes().front().definition);
  assert(placed != nullptr);
  assert(placed->transform.translation == signet::core::Point{});
  assert(std::get<signet::core::Circle>(placed->primitive).radius == 10.0);

  signet::core::Document transform_document("Resize rotate UI test");
  const auto transform_rectangle = transform_document.addPrimitive(
      "Rectangle", signet::core::Rectangle{20.0, 10.0});
  signet::core::DocumentHistory transform_history(std::move(transform_document));
  signet::ui::CanvasView transform_canvas(transform_history);
  transform_canvas.resize(800, 600);
  transform_canvas.show();
  transform_canvas.setSelectedNode(transform_rectangle);
  const auto resize_layout = transform_canvas.selectionHandleLayout();
  assert(resize_layout.has_value());
  const auto resize_point = resize_layout->resize_handles[static_cast<std::size_t>(
      signet::ui::interaction::ResizeHandle::bottom_right)];
  const QPointF resize_handle(resize_point.x, resize_point.y);
  sendDrag(transform_canvas, resize_handle, QPointF{40.0, 20.0});
  assert((transformOf(transform_history, transform_rectangle).scale ==
          signet::core::Point{2.0, 2.0}));
  assert(transform_history.undo());
  transform_canvas.refreshFromDocument();
  transform_canvas.setSelectedNode(transform_rectangle);
  const auto rotate_layout = transform_canvas.selectionHandleLayout();
  assert(rotate_layout.has_value());
  const QPointF pivot(
      (rotate_layout->bounds.min.x + rotate_layout->bounds.max.x) / 2.0,
      (rotate_layout->bounds.min.y + rotate_layout->bounds.max.y) / 2.0);
  sendDrag(transform_canvas, QPointF{rotate_layout->rotate_handle.x,
                                     rotate_layout->rotate_handle.y},
           QPointF{pivot.x() + 40.0 - rotate_layout->rotate_handle.x,
                   pivot.y() - rotate_layout->rotate_handle.y});
  assert(std::abs(transformOf(transform_history, transform_rectangle)
                      .rotation_degrees) > 45.0);
  assert(transform_history.undo());
  transform_canvas.refreshFromDocument();

  signet::core::Document dpi_document("DPR UI test");
  const auto dpi_rectangle = dpi_document.addPrimitive(
      "Rectangle", signet::core::Rectangle{20.0, 10.0});
  signet::core::DocumentHistory dpi_history(std::move(dpi_document));
  signet::ui::CanvasView dpi_canvas(dpi_history);
  dpi_canvas.resize(120, 80);
  dpi_canvas.show();
  QImage one_x_view(QSize{120, 80}, QImage::Format_ARGB32_Premultiplied);
  one_x_view.setDevicePixelRatio(1.0);
  dpi_canvas.render(&one_x_view);
  QImage two_x_view(QSize{240, 160}, QImage::Format_ARGB32_Premultiplied);
  two_x_view.setDevicePixelRatio(2.0);
  dpi_canvas.render(&two_x_view);
  assert(closeEnough(
      dpi_canvas.viewToDocument(dpi_canvas.documentToView(QPointF{4.25, -2.5})),
      QPointF{4.25, -2.5}));
  const QPointF edge = dpi_canvas.documentToView(QPointF{-10.0, 0.0});
  QMouseEvent dpi_hit(
      QEvent::MouseButtonPress, edge + QPointF{7.0, 0.0}, edge + QPointF{7.0, 0.0},
      Qt::LeftButton, Qt::LeftButton, {});
  QApplication::sendEvent(&dpi_canvas, &dpi_hit);
  assert(dpi_canvas.selectedNodeIds() == std::vector<signet::core::NodeId>{dpi_rectangle});
  dpi_canvas.setSelectedNode(std::nullopt);
  QMouseEvent dpi_miss(
      QEvent::MouseButtonPress, edge - QPointF{9.0, 0.0}, edge - QPointF{9.0, 0.0},
      Qt::LeftButton, Qt::LeftButton, {});
  QApplication::sendEvent(&dpi_canvas, &dpi_miss);
  assert(dpi_canvas.selectedNodeIds().empty());

  signet::ui::MainWindow action_window;
  action_window.canvasView()->setSelectedNode(1);
  assert(action_window.flipHorizontalAction()->isEnabled());
  assert(action_window.flipHorizontalAction()->shortcut() ==
         QKeySequence(Qt::META | Qt::ALT | Qt::Key_H));
  action_window.flipHorizontalAction()->trigger();
  assert(action_window.history().document().nodes().size() == 3);
  assert(std::holds_alternative<signet::core::SymmetryNode>(
      action_window.history().document().nodes().back().definition));
  assert(action_window.undoAction()->isEnabled());
  action_window.undoAction()->trigger();
  assert(action_window.history().document().nodes().size() == 2);
  action_window.redoAction()->trigger();
  assert(action_window.history().document().nodes().size() == 3);
}

}  // namespace

int main(int argc, char** argv) {
  QApplication application(argc, argv);

  signet::core::DocumentHistory history(makeDocument());
  signet::ui::CanvasView canvas(history);
  canvas.resize(800, 600);
  canvas.show();

  assert(canvas.evaluatedCircleCount() == 2);
  assert(canvas.diagnosticCount() == 0);
  const signet::core::NodeId first = 1;
  const signet::core::NodeId second = 2;
  const auto initial_transform = transformOf(history, first);
  const QPointF document(12.0, -7.0);
  assert(closeEnough(canvas.viewToDocument(canvas.documentToView(document)), document));

  const QPointF first_center = canvas.circleCenter(first);
  const QPointF first_view = canvas.documentToView(first_center);
  QMouseEvent press(
      QEvent::MouseButtonPress, first_view, first_view, Qt::LeftButton, Qt::LeftButton, {});
  QApplication::sendEvent(&canvas, &press);
  assert((canvas.selectedNodeIds() == std::vector<signet::core::NodeId>{first}));
  assert(canvas.focusPolicy() == Qt::StrongFocus);
  assert(canvas.hasTransientInteraction());

  const QPointF moved_view = first_view + QPointF(20.0, -10.0);
  QMouseEvent move(
      QEvent::MouseMove, moved_view, moved_view, Qt::NoButton, Qt::LeftButton, {});
  QApplication::sendEvent(&canvas, &move);
  assert(canvas.hasTransientInteraction());
  assert(transformOf(history, first) == initial_transform);
  QMouseEvent release(
      QEvent::MouseButtonRelease, moved_view, moved_view, Qt::LeftButton, {}, {});
  QApplication::sendEvent(&canvas, &release);
  assert(!canvas.hasTransientInteraction());
  assert(history.canUndo());
  const auto dragged_transform = transformOf(history, first);
  assert((dragged_transform.translation == signet::core::Point{-45.0, 5.0}));
  assert(history.undo());
  assert(transformOf(history, first) == initial_transform);
  canvas.refreshFromDocument();
  assert(!history.canUndo());

  const QPointF restored_center = canvas.circleCenter(first);
  QMouseEvent press_again(
      QEvent::MouseButtonPress, canvas.documentToView(restored_center),
      canvas.documentToView(restored_center), Qt::LeftButton, Qt::LeftButton, {});
  QApplication::sendEvent(&canvas, &press_again);
  const QPointF moved_again_view = canvas.documentToView(restored_center) + QPointF(30.0, 30.0);
  QMouseEvent move_again(
      QEvent::MouseMove, moved_again_view, moved_again_view, Qt::NoButton, Qt::LeftButton, {});
  QApplication::sendEvent(&canvas, &move_again);
  assert(transformOf(history, first) == initial_transform);
  QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, {});
  QApplication::sendEvent(&canvas, &escape);
  assert(!canvas.hasTransientInteraction());
  assert(transformOf(history, first) == initial_transform);
  assert(!history.canUndo());

  QMouseEvent press_focus(
      QEvent::MouseButtonPress, canvas.documentToView(restored_center),
      canvas.documentToView(restored_center), Qt::LeftButton, Qt::LeftButton, {});
  QApplication::sendEvent(&canvas, &press_focus);
  QMouseEvent move_focus(
      QEvent::MouseMove, moved_again_view, moved_again_view, Qt::NoButton, Qt::LeftButton, {});
  QApplication::sendEvent(&canvas, &move_focus);
  QFocusEvent focus_out(QEvent::FocusOut, Qt::OtherFocusReason);
  QApplication::sendEvent(&canvas, &focus_out);
  assert(!canvas.hasTransientInteraction());
  assert(transformOf(history, first) == initial_transform);
  assert(!history.canUndo());

  QMouseEvent press_ungrab(
      QEvent::MouseButtonPress, canvas.documentToView(restored_center),
      canvas.documentToView(restored_center), Qt::LeftButton, Qt::LeftButton, {});
  QApplication::sendEvent(&canvas, &press_ungrab);
  QMouseEvent move_ungrab(
      QEvent::MouseMove, moved_again_view, moved_again_view, Qt::NoButton, Qt::LeftButton, {});
  QApplication::sendEvent(&canvas, &move_ungrab);
  QEvent ungrab(QEvent::UngrabMouse);
  QApplication::sendEvent(&canvas, &ungrab);
  assert(!canvas.hasTransientInteraction());
  assert(transformOf(history, first) == initial_transform);
  assert(!history.canUndo());

  canvas.setSelectedNode(first);
  const QPointF before_key = canvas.circleCenter(first);
  QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, {});
  QApplication::sendEvent(&canvas, &right);
  const auto keyboard_transform = transformOf(history, first);
  assert((keyboard_transform.translation == signet::core::Point{-54.0, 0.0}));
  assert(closeEnough(canvas.circleCenter(first), before_key + QPointF(1.0, 0.0)));
  assert(history.undo());
  assert(transformOf(history, first) == initial_transform);
  canvas.refreshFromDocument();
  assert(history.redo());
  assert(transformOf(history, first) == keyboard_transform);
  canvas.refreshFromDocument();

  const auto before_view_state = transformOf(history, first);
  const QPointF pan_before = canvas.panOffset();
  const QPointF pan_press_position(100.0, 100.0);
  QMouseEvent pan_press(
      QEvent::MouseButtonPress, pan_press_position, pan_press_position,
      Qt::MiddleButton, Qt::MiddleButton, {});
  QApplication::sendEvent(&canvas, &pan_press);
  const QPointF pan_move_position(130.0, 80.0);
  QMouseEvent pan_move(
      QEvent::MouseMove, pan_move_position, pan_move_position, Qt::NoButton, Qt::MiddleButton, {});
  QApplication::sendEvent(&canvas, &pan_move);
  assert(closeEnough(canvas.panOffset(), pan_before + QPointF(30.0, -20.0)));
  QMouseEvent pan_release(
      QEvent::MouseButtonRelease, pan_move_position, pan_move_position, Qt::MiddleButton, {}, {});
  QApplication::sendEvent(&canvas, &pan_release);

  const double old_zoom = canvas.zoom();
  const QPointF zoom_anchor(400.0, 300.0);
  const QPointF document_at_anchor = canvas.viewToDocument(zoom_anchor);
  QWheelEvent wheel(
      zoom_anchor, zoom_anchor, QPoint(0, 120), QPoint(0, 120), Qt::NoButton,
      Qt::NoModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(&canvas, &wheel);
  assert(canvas.zoom() > old_zoom);
  assert(closeEnough(canvas.viewToDocument(zoom_anchor), document_at_anchor));
  assert(transformOf(history, first) == before_view_state);
  assert(history.canUndo());

  signet::core::Document primitive_document("Primitive UI test");
  const signet::core::NodeId primitive_circle = primitive_document.addPrimitive(
      "Circle", signet::core::Circle{8.0},
      signet::core::Transform{signet::core::Point{0.0, -35.0}});
  const signet::core::NodeId rectangle = primitive_document.addPrimitive(
      "Rectangle", signet::core::Rectangle{20.0, 10.0},
      signet::core::Transform{signet::core::Point{45.0, 0.0}});
  const signet::core::NodeId golden_rectangle = primitive_document.addPrimitive(
      "Golden rectangle", signet::core::GoldenRectangle{10.0},
      signet::core::Transform{signet::core::Point{-45.0, 0.0}});
  const signet::core::NodeId arc = primitive_document.addPrimitive(
      "Arc", signet::core::Arc{15.0, 0.0, 90.0},
      signet::core::Transform{signet::core::Point{0.0, 45.0}});
  const signet::core::NodeId diagnostic_circle = primitive_document.addPrimitive(
      "Unsupported circle", signet::core::Circle{4.0},
      signet::core::Transform{signet::core::Point{0.0, 0.0}, 0.0, signet::core::Point{2.0, 3.0}});
  const signet::core::NodeId operand_circle = primitive_document.addPrimitive(
      "Boolean operand", signet::core::Circle{5.0},
      signet::core::Transform{signet::core::Point{5.0, -35.0}});
  const signet::core::NodeId operation = primitive_document.addBoolean(
      "Union", signet::core::BooleanOperation::unite, primitive_circle, operand_circle);
  signet::core::DocumentHistory primitive_history(std::move(primitive_document));
  signet::ui::CanvasView primitive_canvas(primitive_history);
  primitive_canvas.resize(800, 600);
  primitive_canvas.show();

  assert(primitive_circle == 1);
  assert(operand_circle == 6);
  assert(operation == 7);
  assert(primitive_canvas.evaluatedCircleCount() == 2);
  assert(primitive_canvas.evaluatedCurveSetCount() == 3);
  assert(primitive_canvas.diagnosticCount() == 1);
  assert(transformOf(primitive_history, diagnostic_circle).translation ==
         signet::core::Point{});

  primitive_canvas.setSelectedNode(primitive_circle);
  const auto initial_guide = primitive_canvas.circleGuideOverlay();
  assert(initial_guide.has_value());
  assert(initial_guide->radius == 8.0);
  assert(initial_guide->radius_label == QStringLiteral("r = 8"));
  assert(closeEnough(
      initial_guide->center_view,
      primitive_canvas.documentToView(QPointF{0.0, -35.0})));
  assert(closeEnough(
      initial_guide->radius_endpoint_view,
      primitive_canvas.documentToView(QPointF{8.0, -35.0})));
  assert(closeEnough(
      initial_guide->radius_endpoint_view - initial_guide->center_view,
      QPointF{initial_guide->radius * primitive_canvas.zoom(), 0.0}));

  const QPointF preview_delta(12.0, -8.0);
  sendPressMove(primitive_canvas, initial_guide->center_view, preview_delta);
  assert(primitive_canvas.hasTransientInteraction());
  const auto preview_guide = primitive_canvas.circleGuideOverlay();
  assert(preview_guide.has_value());
  assert(closeEnough(preview_guide->center_view,
                     initial_guide->center_view + preview_delta));
  assert(closeEnough(preview_guide->radius_endpoint_view,
                     initial_guide->radius_endpoint_view + preview_delta));
  assert((transformOf(primitive_history, primitive_circle).translation ==
          signet::core::Point{0.0, -35.0}));
  QKeyEvent circle_escape(QEvent::KeyPress, Qt::Key_Escape, {});
  QApplication::sendEvent(&primitive_canvas, &circle_escape);
  assert(!primitive_canvas.hasTransientInteraction());
  const auto cancelled_guide = primitive_canvas.circleGuideOverlay();
  assert(cancelled_guide.has_value());
  assert(closeEnough(cancelled_guide->center_view, initial_guide->center_view));
  assert(!primitive_history.canUndo());

  const QPointF commit_delta(-10.0, 6.0);
  sendDrag(primitive_canvas, initial_guide->center_view, commit_delta);
  const auto committed_guide = primitive_canvas.circleGuideOverlay();
  assert(committed_guide.has_value());
  assert(closeEnough(committed_guide->center_view,
                     initial_guide->center_view + commit_delta));
  assert(primitive_history.canUndo());
  assert(primitive_history.undo());
  primitive_canvas.refreshFromDocument();
  assert(closeEnough(primitive_canvas.circleGuideOverlay()->center_view,
                     initial_guide->center_view));
  assert(!primitive_history.canUndo());

  for (const auto node_id : {rectangle, golden_rectangle, arc, diagnostic_circle, operation}) {
    primitive_canvas.setSelectedNode(node_id);
    assert(!primitive_canvas.circleGuideOverlay().has_value());
  }
  primitive_canvas.setSelectedNode(std::nullopt);
  assert(!primitive_canvas.circleGuideOverlay().has_value());

  primitive_canvas.setSelectedNode(operation);
  sendPressMove(primitive_canvas, QPointF{200.0, 260.0}, QPointF{18.0, -12.0});
  assert(!primitive_canvas.hasTransientInteraction());
  assert(!primitive_canvas.circleGuideOverlay().has_value());
  QKeyEvent operation_right(QEvent::KeyPress, Qt::Key_Right, {});
  QApplication::sendEvent(&primitive_canvas, &operation_right);
  assert(!primitive_history.canUndo());

  const QPointF rectangle_edge = primitive_canvas.documentToView(QPointF{45.0, -5.0});
  sendDrag(primitive_canvas, rectangle_edge, QPointF{20.0, -10.0});
  assert((primitive_canvas.selectedNodeIds() == std::vector<signet::core::NodeId>{rectangle}));
  assert((transformOf(primitive_history, rectangle).translation ==
          signet::core::Point{55.0, 5.0}));
  assert(primitive_history.canUndo());
  assert(primitive_history.undo());
  primitive_canvas.refreshFromDocument();
  assert((transformOf(primitive_history, rectangle).translation ==
          signet::core::Point{45.0, 0.0}));
  assert(!primitive_history.canUndo());

  const QPointF golden_edge = primitive_canvas.documentToView(QPointF{-45.0, 5.0});
  sendDrag(primitive_canvas, golden_edge, QPointF{-16.0, 12.0});
  assert((primitive_canvas.selectedNodeIds() ==
          std::vector<signet::core::NodeId>{golden_rectangle}));
  assert((transformOf(primitive_history, golden_rectangle).translation ==
          signet::core::Point{-50.0, -5.0}));
  assert(primitive_history.canUndo());
  assert(primitive_history.undo());
  primitive_canvas.refreshFromDocument();
  assert((transformOf(primitive_history, golden_rectangle).translation ==
          signet::core::Point{-45.0, 0.0}));

  const QPointF arc_edge = primitive_canvas.documentToView(
      QPointF{15.0 / std::sqrt(2.0), 45.0 + 15.0 / std::sqrt(2.0)});
  sendDrag(primitive_canvas, arc_edge, QPointF{10.0, 8.0});
  assert((primitive_canvas.selectedNodeIds() == std::vector<signet::core::NodeId>{arc}));
  assert((transformOf(primitive_history, arc).translation ==
          signet::core::Point{5.0, 41.0}));
  assert(primitive_history.canUndo());
  assert(primitive_history.undo());
  primitive_canvas.refreshFromDocument();
  assert((transformOf(primitive_history, arc).translation ==
          signet::core::Point{0.0, 45.0}));

  sendPressMove(primitive_canvas, golden_edge, QPointF{18.0, -14.0});
  assert(primitive_canvas.hasTransientInteraction());
  QKeyEvent primitive_escape(QEvent::KeyPress, Qt::Key_Escape, {});
  QApplication::sendEvent(&primitive_canvas, &primitive_escape);
  assert(!primitive_canvas.hasTransientInteraction());
  assert((transformOf(primitive_history, golden_rectangle).translation ==
          signet::core::Point{-45.0, 0.0}));
  assert(!primitive_history.canUndo());

  sendPressMove(primitive_canvas, arc_edge, QPointF{-14.0, 16.0});
  assert(primitive_canvas.hasTransientInteraction());
  QFocusEvent primitive_focus_out(QEvent::FocusOut, Qt::OtherFocusReason);
  QApplication::sendEvent(&primitive_canvas, &primitive_focus_out);
  assert(!primitive_canvas.hasTransientInteraction());
  assert((transformOf(primitive_history, arc).translation ==
          signet::core::Point{0.0, 45.0}));
  assert(!primitive_history.canUndo());

  const double zoom_before_tolerance = primitive_canvas.zoom();
  for (int index = 0; index < 8; ++index) {
    const QPointF zoom_out_anchor(400.0, 300.0);
    QWheelEvent zoom_out(
        zoom_out_anchor, zoom_out_anchor, QPoint(0, -120), QPoint(0, -120), Qt::NoButton,
        Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&primitive_canvas, &zoom_out);
  }
  assert(primitive_canvas.zoom() < zoom_before_tolerance);
  const QPointF zoomed_rectangle_edge =
      primitive_canvas.documentToView(QPointF{45.0, -5.0}) + QPointF{0.0, 7.0};
  QMouseEvent zoomed_press(
      QEvent::MouseButtonPress, zoomed_rectangle_edge, zoomed_rectangle_edge,
      Qt::LeftButton, Qt::LeftButton, {});
  QApplication::sendEvent(&primitive_canvas, &zoomed_press);
  assert((primitive_canvas.selectedNodeIds() == std::vector<signet::core::NodeId>{rectangle}));
  QKeyEvent zoomed_escape(QEvent::KeyPress, Qt::Key_Escape, {});
  QApplication::sendEvent(&primitive_canvas, &zoomed_escape);

  primitive_canvas.setSelectedNode(primitive_circle);
  primitive_canvas.setMinimumSize(0, 0);
  primitive_canvas.resize(120, 80);
  assert(primitive_canvas.circleGuideOverlay().has_value());
  QImage high_dpi_view(QSize{240, 160}, QImage::Format_ARGB32_Premultiplied);
  high_dpi_view.setDevicePixelRatio(2.0);
  primitive_canvas.render(&high_dpi_view);
  assert(!high_dpi_view.isNull());

  signet::ui::MainWindow window;
  assert(window.history().document().nodes().size() == 2);
  assert(window.canvasView()->evaluatedCircleCount() == 2);
  assert(window.objectsList()->count() == 2);
  assert(!window.undoAction()->isEnabled());
  assert(!window.redoAction()->isEnabled());
  window.objectsList()->setCurrentRow(1);
  assert((window.canvasView()->selectedNodeIds() == std::vector<signet::core::NodeId>{second}));
  window.canvasView()->setSelectedNodes({1});
  assert(window.objectsList()->currentRow() == 0);
  const auto main_initial = transformOf(window.history(), first);
  QKeyEvent main_right(QEvent::KeyPress, Qt::Key_Right, {});
  QApplication::sendEvent(window.canvasView(), &main_right);
  const auto main_moved = transformOf(window.history(), first);
  assert(main_moved.translation.x == main_initial.translation.x + 1.0);
  assert(window.undoAction()->isEnabled());
  window.undoAction()->trigger();
  assert(transformOf(window.history(), first) == main_initial);
  assert(window.redoAction()->isEnabled());
  window.redoAction()->trigger();
  assert(transformOf(window.history(), first) == main_moved);
  assert((window.canvasView()->selectedNodeIds() == std::vector<signet::core::NodeId>{first}));

  testMultiSelectionAndCommands();
  testPlacementGestures();
  testSplitGesturesRegionsAndAtomicFiltering();
  testSplitRejectsInvalidTargets();
  testDuplicateDeleteAndObjectSelection();
  testFlipSnapAndHighDpiContracts();
}
