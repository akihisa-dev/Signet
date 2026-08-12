// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/main_window.h"

#include "ui/canvas_view.h"

#include <QAction>
#include <QActionGroup>
#include <QAbstractItemView>
#include <QDockWidget>
#include <QGuiApplication>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QToolBar>
#include <QVariant>

#include <ranges>
#include <utility>
#include <vector>

namespace signet::ui {

namespace {

core::Document makeInitialDocument() {
  core::Document document("Untitled");
  document.addPrimitive("Circle 1", core::Circle{90.0}, core::Transform{core::Point{-55.0, 0.0}});
  document.addPrimitive("Circle 2", core::Circle{90.0}, core::Transform{core::Point{55.0, 0.0}});
  return document;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), history_(makeInitialDocument()) {
  setWindowTitle(tr("Signet"));
  resize(1180, 760);

  canvas_ = new CanvasView(history_, this);
  setCentralWidget(canvas_);

  objects_ = new QListWidget(this);
  objects_->setAccessibleName(tr("Construction objects"));
  objects_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  objects_->setSelectionBehavior(QAbstractItemView::SelectRows);
  auto* dock = new QDockWidget(tr("Objects"), this);
  dock->setObjectName(QStringLiteral("objectsDock"));
  dock->setWidget(objects_);
  addDockWidget(Qt::RightDockWidgetArea, dock);

  select_action_ = new QAction(tr("Select"), this);
  select_action_->setCheckable(true);
  select_action_->setChecked(true);
  circle_action_ = new QAction(tr("Circle"), this);
  circle_action_->setCheckable(true);
  rectangle_action_ = new QAction(tr("Rectangle"), this);
  rectangle_action_->setCheckable(true);
  arc_action_ = new QAction(tr("Arc"), this);
  arc_action_->setCheckable(true);
  golden_rectangle_action_ = new QAction(tr("Golden Rectangle"), this);
  golden_rectangle_action_->setCheckable(true);
  split_action_ = new QAction(tr("Split"), this);
  split_action_->setCheckable(true);
  auto* tool_group = new QActionGroup(this);
  tool_group->setExclusive(true);
  tool_group->addAction(select_action_);
  tool_group->addAction(circle_action_);
  tool_group->addAction(rectangle_action_);
  tool_group->addAction(arc_action_);
  tool_group->addAction(golden_rectangle_action_);
  tool_group->addAction(split_action_);

  duplicate_action_ = new QAction(tr("Duplicate"), this);
  duplicate_action_->setShortcut(QKeySequence(Qt::META | Qt::Key_D));
  duplicate_action_->setShortcutContext(Qt::WindowShortcut);
  delete_action_ = new QAction(tr("Delete"), this);
  delete_action_->setShortcuts(
      {QKeySequence(Qt::Key_Delete), QKeySequence(Qt::Key_Backspace)});
  delete_action_->setShortcutContext(Qt::WindowShortcut);
  flip_horizontal_action_ = new QAction(tr("Flip Horizontal"), this);
  flip_horizontal_action_->setShortcut(QKeySequence(Qt::META | Qt::ALT | Qt::Key_H));
  flip_horizontal_action_->setShortcutContext(Qt::WindowShortcut);
  flip_vertical_action_ = new QAction(tr("Flip Vertical"), this);
  flip_vertical_action_->setShortcut(QKeySequence(Qt::META | Qt::ALT | Qt::Key_V));
  flip_vertical_action_->setShortcutContext(Qt::WindowShortcut);

  auto* toolbar = addToolBar(tr("Tools"));
  toolbar->setMovable(false);
  toolbar->addAction(select_action_);
  toolbar->addAction(circle_action_);
  toolbar->addAction(rectangle_action_);
  toolbar->addAction(arc_action_);
  toolbar->addAction(golden_rectangle_action_);
  toolbar->addAction(split_action_);
  toolbar->addAction(flip_horizontal_action_);
  toolbar->addAction(flip_vertical_action_);

  auto* file_menu = menuBar()->addMenu(tr("File"));
  auto* quit_action = file_menu->addAction(tr("Quit Signet"));
  quit_action->setShortcut(QKeySequence::Quit);
  connect(quit_action, &QAction::triggered, this, &QWidget::close);

  auto* edit_menu = menuBar()->addMenu(tr("Edit"));
  edit_menu->addAction(select_action_);
  edit_menu->addAction(circle_action_);
  edit_menu->addAction(rectangle_action_);
  edit_menu->addAction(arc_action_);
  edit_menu->addAction(golden_rectangle_action_);
  edit_menu->addAction(split_action_);
  edit_menu->addAction(duplicate_action_);
  edit_menu->addAction(delete_action_);
  edit_menu->addAction(flip_horizontal_action_);
  edit_menu->addAction(flip_vertical_action_);
  undo_action_ = edit_menu->addAction(tr("Undo"));
  undo_action_->setShortcut(QKeySequence::Undo);
  redo_action_ = edit_menu->addAction(tr("Redo"));
  redo_action_->setShortcut(QKeySequence::Redo);

  connect(select_action_, &QAction::triggered, this, [this] {
    canvas_->setTool(CanvasView::Tool::select);
  });
  connect(circle_action_, &QAction::triggered, this, [this] {
    canvas_->setTool(CanvasView::Tool::circle);
  });
  connect(rectangle_action_, &QAction::triggered, this, [this] {
    canvas_->setTool(CanvasView::Tool::rectangle);
  });
  connect(arc_action_, &QAction::triggered, this, [this] {
    canvas_->setTool(CanvasView::Tool::arc);
  });
  connect(golden_rectangle_action_, &QAction::triggered, this, [this] {
    canvas_->setTool(CanvasView::Tool::golden_rectangle);
  });
  connect(split_action_, &QAction::triggered, this, [this] {
    canvas_->setTool(CanvasView::Tool::split);
  });
  connect(duplicate_action_, &QAction::triggered, canvas_, &CanvasView::duplicateSelection);
  connect(delete_action_, &QAction::triggered, canvas_, &CanvasView::deleteSelection);
  connect(flip_horizontal_action_, &QAction::triggered,
          canvas_, &CanvasView::flipSelectionHorizontal);
  connect(flip_vertical_action_, &QAction::triggered,
          canvas_, &CanvasView::flipSelectionVertical);

  connect(canvas_, &CanvasView::selectionChanged, this, [this] {
    syncObjectsSelection();
    updateSelectionActions();
  });
  connect(canvas_, &CanvasView::documentChanged, this, &MainWindow::refreshDocumentUi);
  connect(objects_, &QListWidget::currentRowChanged, this, &MainWindow::selectObject);
  connect(objects_, &QListWidget::itemSelectionChanged, this, &MainWindow::selectObjectsFromList);
  connect(canvas_, &CanvasView::statusMessage, this, [this](const QString& message) {
    statusBar()->showMessage(message, 3000);
  });
  connect(undo_action_, &QAction::triggered, this, [this] {
    if (history_.undo()) {
      refreshDocumentUi();
    }
  });
  connect(redo_action_, &QAction::triggered, this, [this] {
    if (history_.redo()) {
      refreshDocumentUi();
    }
  });

  refreshDocumentUi();
}

void MainWindow::refreshDocumentUi() {
  canvas_->refreshFromDocument();
  refreshObjects();
  undo_action_->setEnabled(history_.canUndo());
  redo_action_->setEnabled(history_.canRedo());
  updateSelectionActions();
}

void MainWindow::refreshObjects() {
  const auto& selected = canvas_->selectedNodeIds();
  QSignalBlocker blocker(objects_);
  syncing_selection_ = true;
  objects_->clear();
  int first_selected_row = -1;
  int row = 0;
  for (const auto& node : history_.document().nodes()) {
    auto* item = new QListWidgetItem(QString::fromStdString(node.name));
    item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(node.id));
    objects_->addItem(item);
    if (std::ranges::find(selected, node.id) != selected.end()) {
      item->setSelected(true);
      if (first_selected_row < 0) {
        first_selected_row = row;
      }
    }
    ++row;
  }
  objects_->setCurrentRow(first_selected_row, QItemSelectionModel::NoUpdate);
  syncing_selection_ = false;
}

void MainWindow::syncObjectsSelection() {
  if (syncing_selection_) {
    return;
  }
  const auto& selected = canvas_->selectedNodeIds();
  QSignalBlocker blocker(objects_);
  syncing_selection_ = true;
  int first_selected_row = -1;
  for (int row = 0; row < objects_->count(); ++row) {
    const auto node_id = objects_->item(row)->data(Qt::UserRole).toULongLong();
    const bool is_selected = std::ranges::find(selected, node_id) != selected.end();
    objects_->item(row)->setSelected(is_selected);
    if (is_selected && first_selected_row < 0) {
      first_selected_row = row;
    }
  }
  objects_->setCurrentRow(first_selected_row, QItemSelectionModel::NoUpdate);
  syncing_selection_ = false;
}

void MainWindow::selectObject(const int row) {
  if (syncing_selection_) {
    return;
  }
  const auto modifiers = QGuiApplication::keyboardModifiers();
  const bool extending = modifiers.testFlag(Qt::ShiftModifier) ||
                         modifiers.testFlag(Qt::ControlModifier) ||
                         modifiers.testFlag(Qt::MetaModifier);
  if (!extending) {
    QSignalBlocker blocker(objects_);
    objects_->clearSelection();
    if (row >= 0 && row < objects_->count()) {
      objects_->item(row)->setSelected(true);
    }
  }
  selectObjectsFromList();
}

void MainWindow::selectObjectsFromList() {
  if (syncing_selection_) {
    return;
  }
  syncing_selection_ = true;
  std::vector<core::NodeId> selected;
  selected.reserve(static_cast<std::size_t>(objects_->count()));
  for (int row = 0; row < objects_->count(); ++row) {
    if (objects_->item(row)->isSelected()) {
      selected.push_back(objects_->item(row)->data(Qt::UserRole).toULongLong());
    }
  }
  canvas_->setSelectedNodes(std::move(selected));
  syncing_selection_ = false;
  updateSelectionActions();
}

void MainWindow::updateSelectionActions() {
  const bool has_node_selection = !canvas_->selectedNodeIds().empty();
  duplicate_action_->setEnabled(has_node_selection);
  delete_action_->setEnabled(has_node_selection || canvas_->hasSelectedRegions());
  const bool can_flip = canvas_->selectedNodeIds().size() == 1 &&
                        canvas_->selectionHandleLayout().has_value();
  flip_horizontal_action_->setEnabled(can_flip);
  flip_vertical_action_->setEnabled(can_flip);
}

}  // namespace signet::ui
