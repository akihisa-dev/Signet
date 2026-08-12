// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "core/document.h"
#include "ui/canvas_view.h"

#include <QMainWindow>
#include <QPointer>

class QAction;
class QLabel;
class QDockWidget;
class QListWidget;
class QToolBar;

namespace signet::ai {
class AiProvider;
}

namespace signet::ui {

class AiGenerationDialog;

class MainWindow final : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);
  explicit MainWindow(ai::AiProvider* provider, QWidget* parent = nullptr);

  void setAiProvider(ai::AiProvider* provider);
  [[nodiscard]] ai::AiProvider* aiProvider() const noexcept { return ai_provider_; }
  [[nodiscard]] AiGenerationDialog* aiGenerationDialog() const noexcept;

  [[nodiscard]] core::DocumentHistory& history() noexcept { return history_; }
  [[nodiscard]] const core::DocumentHistory& history() const noexcept { return history_; }
  [[nodiscard]] CanvasView* canvasView() const noexcept { return canvas_; }
  [[nodiscard]] QListWidget* objectsList() const noexcept { return objects_; }
  [[nodiscard]] QAction* undoAction() const noexcept { return undo_action_; }
  [[nodiscard]] QAction* redoAction() const noexcept { return redo_action_; }
  [[nodiscard]] QAction* selectAction() const noexcept { return select_action_; }
  [[nodiscard]] QAction* circleAction() const noexcept { return circle_action_; }
  [[nodiscard]] QAction* rectangleAction() const noexcept { return rectangle_action_; }
  [[nodiscard]] QAction* arcAction() const noexcept { return arc_action_; }
  [[nodiscard]] QAction* goldenRectangleAction() const noexcept {
    return golden_rectangle_action_;
  }
  [[nodiscard]] QAction* splitAction() const noexcept { return split_action_; }
  [[nodiscard]] QAction* duplicateAction() const noexcept { return duplicate_action_; }
  [[nodiscard]] QAction* deleteAction() const noexcept { return delete_action_; }
  [[nodiscard]] QAction* flipHorizontalAction() const noexcept {
    return flip_horizontal_action_;
  }
  [[nodiscard]] QAction* flipVerticalAction() const noexcept { return flip_vertical_action_; }
  [[nodiscard]] QToolBar* commandBar() const noexcept { return command_bar_; }
  [[nodiscard]] QDockWidget* shapesDock() const noexcept { return shapes_dock_; }
  [[nodiscard]] QLabel* selectionSummary() const noexcept { return selection_summary_; }
  [[nodiscard]] QLabel* shapeCountLabel() const noexcept { return object_count_; }
  [[nodiscard]] QLabel* shapeDiagnosticsLabel() const noexcept { return diagnostics_count_; }
  [[nodiscard]] QLabel* zoomStatusLabel() const noexcept { return zoom_status_; }
  [[nodiscard]] QLabel* diagnosticsStatusLabel() const noexcept {
    return diagnostics_status_;
  }
  [[nodiscard]] QAction* aiLogoAction() const noexcept { return ai_logo_action_; }

 private:
  void refreshDocumentUi();
  void refreshObjects();
  void syncObjectsSelection();
  void selectObject(int row);
  void selectObjectsFromList();
  void updateSelectionActions();
  void updateSelectionSummary();
  void updateStatusIndicators();
  void updateToolHint(CanvasView::Tool tool);
  void openAiGenerationDialog();

  core::DocumentHistory history_;
  CanvasView* canvas_{};
  QListWidget* objects_{};
  QDockWidget* shapes_dock_{};
  QToolBar* command_bar_{};
  QLabel* object_count_{};
  QLabel* diagnostics_count_{};
  QLabel* selection_summary_{};
  QLabel* zoom_status_{};
  QLabel* diagnostics_status_{};
  QAction* undo_action_{};
  QAction* redo_action_{};
  QAction* select_action_{};
  QAction* circle_action_{};
  QAction* rectangle_action_{};
  QAction* arc_action_{};
  QAction* golden_rectangle_action_{};
  QAction* split_action_{};
  QAction* duplicate_action_{};
  QAction* delete_action_{};
  QAction* flip_horizontal_action_{};
  QAction* flip_vertical_action_{};
  QAction* ai_logo_action_{};
  ai::AiProvider* ai_provider_{};
  QPointer<class AiGenerationDialog> ai_generation_dialog_;
  bool syncing_selection_{};
};

}  // namespace signet::ui
