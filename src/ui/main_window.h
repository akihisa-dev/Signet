// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "core/document.h"

#include <QMainWindow>

class QAction;
class QListWidget;

namespace signet::ui {

class CanvasView;

class MainWindow final : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);

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

 private:
  void refreshDocumentUi();
  void refreshObjects();
  void syncObjectsSelection();
  void selectObject(int row);
  void selectObjectsFromList();
  void updateSelectionActions();

  core::DocumentHistory history_;
  CanvasView* canvas_{};
  QListWidget* objects_{};
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
  bool syncing_selection_{};
};

}  // namespace signet::ui
