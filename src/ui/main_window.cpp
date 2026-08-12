// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/main_window.h"

#include "ai/codex_cli_provider.h"
#include "ui/ai_generation_dialog.h"
#include "ui/canvas_view.h"

#include <QAction>
#include <QActionGroup>
#include <QAbstractItemView>
#include <QCoreApplication>
#include <QDockWidget>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStyle>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPalette>
#include <QToolButton>
#include <QToolBar>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>

#include <ranges>
#include <utility>
#include <vector>
#include <type_traits>

// QObject::tr() uses the fully-qualified C++ class name for a namespaced type,
// while the catalog keeps the stable user-facing MainWindow context.
#define tr(source) QCoreApplication::translate("MainWindow", source)

namespace signet::ui {

namespace {

core::Document makeInitialDocument() {
  core::Document document("Untitled");
  document.addPrimitive("Circle 1", core::Circle{90.0}, core::Transform{core::Point{-55.0, 0.0}});
  document.addPrimitive("Circle 2", core::Circle{90.0}, core::Transform{core::Point{55.0, 0.0}});
  return document;
}

QString nodeTypeLabel(const core::Node& node) {
  return std::visit(
      [](const auto& definition) -> QString {
        using Definition = std::decay_t<decltype(definition)>;
        if constexpr (std::is_same_v<Definition, core::PrimitiveNode>) {
          return std::visit(
              [](const auto& primitive) -> QString {
                using Primitive = std::decay_t<decltype(primitive)>;
                if constexpr (std::is_same_v<Primitive, core::Circle>) {
                  return QCoreApplication::translate("signet::ui::MainWindow", "Circle");
                } else if constexpr (std::is_same_v<Primitive, core::Rectangle>) {
                  return QCoreApplication::translate("signet::ui::MainWindow", "Rectangle");
                } else if constexpr (std::is_same_v<Primitive, core::GoldenRectangle>) {
                  return QCoreApplication::translate(
                      "signet::ui::MainWindow", "Golden Rectangle");
                }
                return QCoreApplication::translate("signet::ui::MainWindow", "Arc");
              },
              definition.primitive);
        } else if constexpr (std::is_same_v<Definition, core::BooleanNode>) {
          return QCoreApplication::translate("signet::ui::MainWindow", "Boolean");
        } else if constexpr (std::is_same_v<Definition, core::SymmetryNode>) {
          return QCoreApplication::translate("signet::ui::MainWindow", "Symmetry");
        } else if constexpr (std::is_same_v<Definition, core::SplitNode>) {
          return QCoreApplication::translate("signet::ui::MainWindow", "Split");
        } else if constexpr (std::is_same_v<Definition, core::RegionSelectionNode>) {
          return QCoreApplication::translate("signet::ui::MainWindow", "Region selection");
        }
        return QCoreApplication::translate("signet::ui::MainWindow", "Region filter");
      },
      node.definition);
}

QString nodeSummary(const core::Node& node) {
  return QStringLiteral("%1 · %2")
      .arg(nodeTypeLabel(node), QString::fromStdString(node.name));
}

enum class ToolGlyph {
  select,
  circle,
  rectangle,
  arc,
  golden_rectangle,
  split,
  duplicate,
  remove,
  undo,
  redo,
  flip_horizontal,
  flip_vertical,
};

QIcon glyphIcon(const QWidget* widget, const ToolGlyph glyph) {
  QIcon icon;
  const QColor foreground = widget->palette().color(QPalette::Text);
  for (const int size : {16, 24, 32}) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(foreground, std::max(1.0, size / 10.0), Qt::SolidLine, Qt::RoundCap,
             Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    const auto line = [&painter](const double x1, const double y1, const double x2,
                                 const double y2) {
      painter.drawLine(QPointF{x1, y1}, QPointF{x2, y2});
    };
    const QRectF bounds(size * 0.2, size * 0.2, size * 0.6, size * 0.6);
    switch (glyph) {
      case ToolGlyph::select:
        line(size * 0.25, size * 0.18, size * 0.25, size * 0.78);
        line(size * 0.25, size * 0.18, size * 0.72, size * 0.45);
        line(size * 0.25, size * 0.78, size * 0.45, size * 0.58);
        break;
      case ToolGlyph::circle:
        painter.drawEllipse(bounds);
        break;
      case ToolGlyph::rectangle:
        painter.drawRect(bounds);
        break;
      case ToolGlyph::arc: {
        QPainterPath path;
        path.arcMoveTo(bounds, 35.0);
        path.arcTo(bounds, 35.0, 210.0);
        painter.drawPath(path);
        break;
      }
      case ToolGlyph::golden_rectangle:
        painter.drawRect(QRectF(size * 0.15, size * 0.3, size * 0.7, size * 0.4));
        line(size * 0.56, size * 0.3, size * 0.56, size * 0.7);
        break;
      case ToolGlyph::split:
        painter.drawRect(bounds);
        painter.setPen(QPen(foreground, std::max(1.0, size / 12.0), Qt::DashLine));
        line(size * 0.15, size * 0.82, size * 0.85, size * 0.18);
        break;
      case ToolGlyph::duplicate:
        painter.drawRect(QRectF(size * 0.28, size * 0.18, size * 0.48, size * 0.48));
        painter.drawRect(QRectF(size * 0.18, size * 0.3, size * 0.48, size * 0.48));
        break;
      case ToolGlyph::remove:
        line(size * 0.25, size * 0.25, size * 0.75, size * 0.75);
        line(size * 0.75, size * 0.25, size * 0.25, size * 0.75);
        break;
      case ToolGlyph::undo:
        painter.drawArc(bounds, 45 * 16, 230 * 16);
        line(size * 0.2, size * 0.42, size * 0.2, size * 0.7);
        line(size * 0.2, size * 0.42, size * 0.48, size * 0.42);
        break;
      case ToolGlyph::redo:
        painter.drawArc(bounds, -45 * 16, 230 * 16);
        line(size * 0.8, size * 0.42, size * 0.8, size * 0.7);
        line(size * 0.8, size * 0.42, size * 0.52, size * 0.42);
        break;
      case ToolGlyph::flip_horizontal:
        line(size * 0.5, size * 0.15, size * 0.5, size * 0.85);
        line(size * 0.25, size * 0.5, size * 0.42, size * 0.34);
        line(size * 0.25, size * 0.5, size * 0.42, size * 0.66);
        line(size * 0.75, size * 0.5, size * 0.58, size * 0.34);
        line(size * 0.75, size * 0.5, size * 0.58, size * 0.66);
        break;
      case ToolGlyph::flip_vertical:
        line(size * 0.15, size * 0.5, size * 0.85, size * 0.5);
        line(size * 0.5, size * 0.25, size * 0.34, size * 0.42);
        line(size * 0.5, size * 0.25, size * 0.66, size * 0.42);
        line(size * 0.5, size * 0.75, size * 0.34, size * 0.58);
        line(size * 0.5, size * 0.75, size * 0.66, size * 0.58);
        break;
    }
    icon.addPixmap(pixmap);
  }
  return icon;
}

void configureAction(QAction* action, const QString& accessible_name, const QString& tooltip) {
  action->setProperty("accessibleName", accessible_name);
  action->setToolTip(tooltip);
  action->setStatusTip(tooltip);
}

void configureToolButton(QToolBar* toolbar, QAction* action, const QString& object_name) {
  auto* button = qobject_cast<QToolButton*>(toolbar->widgetForAction(action));
  if (button == nullptr) {
    return;
  }
  button->setObjectName(object_name);
  button->setAccessibleName(action->property("accessibleName").toString());
  button->setAccessibleDescription(action->statusTip());
  button->setToolTip(action->toolTip());
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : MainWindow(static_cast<ai::AiProvider*>(nullptr), parent) {}

MainWindow::MainWindow(ai::AiProvider* provider, QWidget* parent)
    : QMainWindow(parent), history_(makeInitialDocument()), ai_provider_(provider) {
  if (ai_provider_ == nullptr) {
    ai_provider_ = new ai::CodexCliProvider({}, this);
  }
  setObjectName(QStringLiteral("mainWindow"));
  setWindowTitle(tr("Signet"));
  resize(1180, 760);

  canvas_ = new CanvasView(history_, this);
  canvas_->setObjectName(QStringLiteral("constructionCanvas"));
  setCentralWidget(canvas_);

  objects_ = new QListWidget(this);
  objects_->setObjectName(QStringLiteral("shapesList"));
  objects_->setAccessibleName(tr("Shapes"));
  objects_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  objects_->setSelectionBehavior(QAbstractItemView::SelectRows);
  objects_->setMinimumWidth(240);
  auto* dock_content = new QWidget(this);
  dock_content->setObjectName(QStringLiteral("shapesDockContent"));
  auto* dock_layout = new QVBoxLayout(dock_content);
  dock_layout->setContentsMargins(10, 8, 10, 8);
  dock_layout->setSpacing(8);

  auto* count_row = new QHBoxLayout;
  object_count_ = new QLabel(dock_content);
  object_count_->setObjectName(QStringLiteral("shapeCount"));
  diagnostics_count_ = new QLabel(dock_content);
  diagnostics_count_->setObjectName(QStringLiteral("shapeDiagnostics"));
  diagnostics_count_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  count_row->addWidget(object_count_);
  count_row->addStretch(1);
  count_row->addWidget(diagnostics_count_);
  dock_layout->addLayout(count_row);

  auto* list_actions = new QHBoxLayout;
  list_actions->setSpacing(4);
  auto* duplicate_button = new QToolButton(dock_content);
  duplicate_button->setObjectName(QStringLiteral("duplicateShapeButton"));
  duplicate_button->setToolButtonStyle(Qt::ToolButtonIconOnly);
  auto* delete_button = new QToolButton(dock_content);
  delete_button->setObjectName(QStringLiteral("deleteShapeButton"));
  delete_button->setToolButtonStyle(Qt::ToolButtonIconOnly);
  list_actions->addWidget(duplicate_button);
  list_actions->addWidget(delete_button);
  list_actions->addStretch(1);
  dock_layout->addLayout(list_actions);
  dock_layout->addWidget(objects_, 1);

  selection_summary_ = new QLabel(dock_content);
  selection_summary_->setObjectName(QStringLiteral("selectionSummary"));
  selection_summary_->setWordWrap(true);
  selection_summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  dock_layout->addWidget(selection_summary_);

  shapes_dock_ = new QDockWidget(tr("Shapes"), this);
  shapes_dock_->setObjectName(QStringLiteral("shapesDock"));
  shapes_dock_->setMinimumWidth(240);
  shapes_dock_->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
  shapes_dock_->setWidget(dock_content);
  addDockWidget(Qt::RightDockWidgetArea, shapes_dock_);
  resizeDocks({shapes_dock_}, {300}, Qt::Horizontal);

  select_action_ = new QAction(tr("Select"), this);
  select_action_->setIcon(glyphIcon(this, ToolGlyph::select));
  configureAction(select_action_, tr("Select tool"), tr("Select and move shapes"));
  select_action_->setShortcut(QKeySequence(Qt::Key_V));
  select_action_->setShortcutContext(Qt::WindowShortcut);
  select_action_->setCheckable(true);
  select_action_->setChecked(true);
  circle_action_ = new QAction(tr("Circle"), this);
  circle_action_->setIcon(glyphIcon(this, ToolGlyph::circle));
  configureAction(circle_action_, tr("Circle tool"), tr("Place a circle"));
  circle_action_->setShortcut(QKeySequence(Qt::Key_C));
  circle_action_->setShortcutContext(Qt::WindowShortcut);
  circle_action_->setCheckable(true);
  rectangle_action_ = new QAction(tr("Rectangle"), this);
  rectangle_action_->setIcon(glyphIcon(this, ToolGlyph::rectangle));
  configureAction(rectangle_action_, tr("Rectangle tool"), tr("Place a rectangle"));
  rectangle_action_->setShortcut(QKeySequence(Qt::Key_R));
  rectangle_action_->setShortcutContext(Qt::WindowShortcut);
  rectangle_action_->setCheckable(true);
  arc_action_ = new QAction(tr("Arc"), this);
  arc_action_->setIcon(glyphIcon(this, ToolGlyph::arc));
  configureAction(arc_action_, tr("Arc tool"), tr("Place a three-point arc"));
  arc_action_->setShortcut(QKeySequence(Qt::Key_A));
  arc_action_->setShortcutContext(Qt::WindowShortcut);
  arc_action_->setCheckable(true);
  golden_rectangle_action_ = new QAction(tr("Golden Rectangle"), this);
  golden_rectangle_action_->setIcon(glyphIcon(this, ToolGlyph::golden_rectangle));
  configureAction(golden_rectangle_action_, tr("Golden rectangle tool"),
                  tr("Place a golden rectangle"));
  golden_rectangle_action_->setShortcut(QKeySequence(Qt::Key_G));
  golden_rectangle_action_->setShortcutContext(Qt::WindowShortcut);
  golden_rectangle_action_->setCheckable(true);
  split_action_ = new QAction(tr("Split"), this);
  split_action_->setIcon(glyphIcon(this, ToolGlyph::split));
  configureAction(split_action_, tr("Split tool"), tr("Split a closed shape"));
  split_action_->setShortcut(QKeySequence(Qt::Key_X));
  split_action_->setShortcutContext(Qt::WindowShortcut);
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
  duplicate_action_->setIcon(glyphIcon(this, ToolGlyph::duplicate));
  configureAction(duplicate_action_, tr("Duplicate shape"), tr("Duplicate the selection"));
  duplicate_action_->setShortcut(QKeySequence(Qt::META | Qt::Key_D));
  duplicate_action_->setShortcutContext(Qt::WindowShortcut);
  delete_action_ = new QAction(tr("Delete"), this);
  delete_action_->setIcon(glyphIcon(this, ToolGlyph::remove));
  configureAction(delete_action_, tr("Delete shape"), tr("Delete the selection"));
  delete_action_->setShortcuts(
      {QKeySequence(Qt::Key_Delete), QKeySequence(Qt::Key_Backspace)});
  delete_action_->setShortcutContext(Qt::WindowShortcut);
  duplicate_button->setDefaultAction(duplicate_action_);
  delete_button->setDefaultAction(delete_action_);
  duplicate_button->setAccessibleName(tr("Duplicate shape"));
  duplicate_button->setAccessibleDescription(tr("Duplicate the selection"));
  duplicate_button->setToolTip(duplicate_action_->toolTip());
  delete_button->setAccessibleName(tr("Delete shape"));
  delete_button->setAccessibleDescription(tr("Delete the selection"));
  delete_button->setToolTip(delete_action_->toolTip());
  flip_horizontal_action_ = new QAction(tr("Flip Horizontal"), this);
  flip_horizontal_action_->setIcon(glyphIcon(this, ToolGlyph::flip_horizontal));
  configureAction(flip_horizontal_action_, tr("Flip horizontally"), tr("Flip across the vertical axis"));
  flip_horizontal_action_->setShortcut(QKeySequence(Qt::META | Qt::ALT | Qt::Key_H));
  flip_horizontal_action_->setShortcutContext(Qt::WindowShortcut);
  flip_vertical_action_ = new QAction(tr("Flip Vertical"), this);
  flip_vertical_action_->setIcon(glyphIcon(this, ToolGlyph::flip_vertical));
  configureAction(flip_vertical_action_, tr("Flip vertically"), tr("Flip across the horizontal axis"));
  flip_vertical_action_->setShortcut(QKeySequence(Qt::META | Qt::ALT | Qt::Key_V));
  flip_vertical_action_->setShortcutContext(Qt::WindowShortcut);

  command_bar_ = addToolBar(tr("Tools"));
  command_bar_->setObjectName(QStringLiteral("commandBar"));
  command_bar_->setMovable(false);
  command_bar_->setFloatable(false);
  command_bar_->setIconSize(QSize(18, 18));
  command_bar_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  command_bar_->addAction(select_action_);
  command_bar_->addAction(circle_action_);
  command_bar_->addAction(rectangle_action_);
  command_bar_->addAction(arc_action_);
  command_bar_->addAction(golden_rectangle_action_);
  command_bar_->addAction(split_action_);
  command_bar_->addSeparator();

  auto* file_menu = menuBar()->addMenu(tr("File"));
  auto* quit_action = file_menu->addAction(tr("Quit Signet"));
  quit_action->setShortcut(QKeySequence::Quit);
  connect(quit_action, &QAction::triggered, this, &QWidget::close);

  auto* edit_menu = menuBar()->addMenu(tr("Edit"));
  undo_action_ = edit_menu->addAction(tr("Undo"));
  undo_action_->setIcon(glyphIcon(this, ToolGlyph::undo));
  configureAction(undo_action_, tr("Undo"), tr("Undo the last change"));
  undo_action_->setShortcut(QKeySequence::Undo);
  redo_action_ = edit_menu->addAction(tr("Redo"));
  redo_action_->setIcon(glyphIcon(this, ToolGlyph::redo));
  configureAction(redo_action_, tr("Redo"), tr("Redo the last change"));
  redo_action_->setShortcut(QKeySequence::Redo);
  edit_menu->addSeparator();
  edit_menu->addAction(duplicate_action_);
  edit_menu->addAction(delete_action_);

  auto* ai_menu = menuBar()->addMenu(tr("AI"));
  ai_logo_action_ = ai_menu->addAction(tr("AI Logo…"));
  configureAction(ai_logo_action_, tr("Generate AI logo"),
                  tr("Generate an editable geometric logo from a prompt or image"));
  ai_logo_action_->setObjectName(QStringLiteral("aiLogoAction"));
  connect(ai_logo_action_, &QAction::triggered, this, &MainWindow::openAiGenerationDialog);

  auto* shapes_menu = menuBar()->addMenu(tr("Shapes"));
  shapes_menu->addAction(select_action_);
  shapes_menu->addAction(circle_action_);
  shapes_menu->addAction(rectangle_action_);
  shapes_menu->addAction(arc_action_);
  shapes_menu->addAction(golden_rectangle_action_);
  shapes_menu->addAction(split_action_);

  auto* transform_menu = menuBar()->addMenu(tr("Transform"));
  transform_menu->addAction(flip_horizontal_action_);
  transform_menu->addAction(flip_vertical_action_);

  auto* view_menu = menuBar()->addMenu(tr("View"));
  view_menu->addAction(shapes_dock_->toggleViewAction());

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
  command_bar_->addAction(undo_action_);
  command_bar_->addAction(redo_action_);
  command_bar_->addAction(duplicate_action_);
  command_bar_->addAction(delete_action_);
  configureToolButton(command_bar_, select_action_, QStringLiteral("selectToolButton"));
  configureToolButton(command_bar_, circle_action_, QStringLiteral("circleToolButton"));
  configureToolButton(command_bar_, rectangle_action_, QStringLiteral("rectangleToolButton"));
  configureToolButton(command_bar_, arc_action_, QStringLiteral("arcToolButton"));
  configureToolButton(command_bar_, golden_rectangle_action_,
                      QStringLiteral("goldenRectangleToolButton"));
  configureToolButton(command_bar_, split_action_, QStringLiteral("splitToolButton"));
  configureToolButton(command_bar_, undo_action_, QStringLiteral("undoToolButton"));
  configureToolButton(command_bar_, redo_action_, QStringLiteral("redoToolButton"));
  configureToolButton(command_bar_, duplicate_action_, QStringLiteral("duplicateToolButton"));
  configureToolButton(command_bar_, delete_action_, QStringLiteral("deleteToolButton"));
  connect(duplicate_action_, &QAction::triggered, canvas_, &CanvasView::duplicateSelection);
  connect(delete_action_, &QAction::triggered, canvas_, &CanvasView::deleteSelection);
  connect(flip_horizontal_action_, &QAction::triggered,
          canvas_, &CanvasView::flipSelectionHorizontal);
  connect(flip_vertical_action_, &QAction::triggered,
          canvas_, &CanvasView::flipSelectionVertical);

  connect(canvas_, &CanvasView::selectionChanged, this, [this] {
    syncObjectsSelection();
    updateSelectionActions();
    updateSelectionSummary();
  });
  connect(canvas_, &CanvasView::documentChanged, this, &MainWindow::refreshDocumentUi);
  connect(canvas_, &CanvasView::viewportChanged, this, &MainWindow::updateStatusIndicators);
  connect(canvas_, &CanvasView::toolChanged, this, &MainWindow::updateToolHint);
  connect(objects_, &QListWidget::currentRowChanged, this, &MainWindow::selectObject);
  connect(objects_, &QListWidget::itemSelectionChanged, this, &MainWindow::selectObjectsFromList);
  connect(canvas_, &CanvasView::statusMessage, this, [this](const QString& message) {
    statusBar()->showMessage(message, 3000);
  });
  connect(statusBar(), &QStatusBar::messageChanged, this, [this](const QString& message) {
    if (message.isEmpty()) {
      updateToolHint(canvas_->tool());
    }
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

  zoom_status_ = new QLabel(this);
  zoom_status_->setObjectName(QStringLiteral("zoomStatus"));
  zoom_status_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  diagnostics_status_ = new QLabel(this);
  diagnostics_status_->setObjectName(QStringLiteral("diagnosticsStatus"));
  diagnostics_status_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  statusBar()->addPermanentWidget(zoom_status_);
  statusBar()->addPermanentWidget(diagnostics_status_);

  refreshDocumentUi();
  updateToolHint(canvas_->tool());
}

AiGenerationDialog* MainWindow::aiGenerationDialog() const noexcept {
  return ai_generation_dialog_.data();
}

void MainWindow::refreshDocumentUi() {
  canvas_->refreshFromDocument();
  refreshObjects();
  object_count_->setText(tr("%1 shapes").arg(history_.document().nodes().size()));
  diagnostics_count_->setText(
      canvas_->diagnosticCount() == 0
          ? tr("No issues")
          : tr("Issues: %1").arg(canvas_->diagnosticCount()));
  undo_action_->setEnabled(history_.canUndo());
  redo_action_->setEnabled(history_.canRedo());
  updateSelectionActions();
  updateSelectionSummary();
  updateStatusIndicators();
}

void MainWindow::setAiProvider(ai::AiProvider* provider) {
  if (provider == nullptr) {
    provider = new ai::CodexCliProvider({}, this);
  }
  ai_provider_ = provider;
  if (ai_generation_dialog_ != nullptr) {
    ai_generation_dialog_->setProvider(ai_provider_);
  }
}

void MainWindow::openAiGenerationDialog() {
  if (ai_generation_dialog_ == nullptr) {
    ai_generation_dialog_ = new AiGenerationDialog(history_, ai_provider_, this);
    connect(ai_generation_dialog_, &AiGenerationDialog::applied, this,
            &MainWindow::refreshDocumentUi);
  }
  ai_generation_dialog_->show();
  ai_generation_dialog_->raise();
  ai_generation_dialog_->activateWindow();
  ai_generation_dialog_->promptEdit()->setFocus();
}

void MainWindow::refreshObjects() {
  const auto& selected = canvas_->selectedNodeIds();
  QSignalBlocker blocker(objects_);
  syncing_selection_ = true;
  objects_->clear();
  int first_selected_row = -1;
  int row = 0;
  for (const auto& node : history_.document().nodes()) {
    auto* item = new QListWidgetItem(nodeSummary(node));
    item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(node.id));
    item->setData(Qt::UserRole + 1, nodeTypeLabel(node));
    item->setToolTip(nodeTypeLabel(node));
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

void MainWindow::updateSelectionSummary() {
  const auto& selected = canvas_->selectedNodeIds();
  if (selected.empty()) {
    selection_summary_->setText(tr("No shape selected"));
    return;
  }
  if (selected.size() == 1) {
    const auto* node = history_.document().findNode(selected.front());
    selection_summary_->setText(node == nullptr ? tr("1 shape selected")
                                                : tr("Selected: %1").arg(nodeSummary(*node)));
    return;
  }
  selection_summary_->setText(tr("%1 shapes selected").arg(selected.size()));
}

void MainWindow::updateStatusIndicators() {
  if (zoom_status_ == nullptr || diagnostics_status_ == nullptr) {
    return;
  }
  zoom_status_->setText(tr("Zoom %1%").arg(qRound(canvas_->zoom() * 100.0)));
  diagnostics_status_->setText(
      canvas_->diagnosticCount() == 0
          ? tr("No diagnostics")
          : tr("Diagnostics: %1").arg(canvas_->diagnosticCount()));
}

void MainWindow::updateToolHint(const CanvasView::Tool tool) {
  QString hint;
  switch (tool) {
    case CanvasView::Tool::select:
      hint = tr("Select: click or drag a shape; Esc cancels");
      break;
    case CanvasView::Tool::circle:
      hint = tr("Place a circle: drag to set the radius; Esc cancels");
      break;
    case CanvasView::Tool::rectangle:
      hint = tr("Place a rectangle: drag opposite corners; Esc cancels");
      break;
    case CanvasView::Tool::arc:
      hint = tr("Place an arc: click three points; Esc cancels");
      break;
    case CanvasView::Tool::golden_rectangle:
      hint = tr("Place a golden rectangle: drag its short side; Esc cancels");
      break;
    case CanvasView::Tool::split:
      hint = tr("Split: drag across a closed shape; Esc cancels");
      break;
  }
  statusBar()->showMessage(hint);
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

#undef tr
