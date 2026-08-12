// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/ai_generation_dialog.h"

#include "ui/canvas_view.h"

#include <QCheckBox>
#include <QAbstractItemView>
#include <QCoreApplication>
#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include <algorithm>
#include <cstdint>

namespace signet::ui {

namespace {

constexpr int kPromptMaxLength = 4000;
constexpr qint64 kMaxImageBytes = 20LL * 1024LL * 1024LL;
constexpr int kMaxImageDimension = 4096;
constexpr std::uint64_t kMaxImagePixels = 16ULL * 1024ULL * 1024ULL;

QString stateText(const AiGenerationDialog::State state) {
  switch (state) {
    case AiGenerationDialog::State::idle:
      return QCoreApplication::translate("AiGenerationDialog", "Ready");
    case AiGenerationDialog::State::generating:
      return QCoreApplication::translate("AiGenerationDialog", "Generating…");
    case AiGenerationDialog::State::preview_ready:
      return QCoreApplication::translate("AiGenerationDialog", "Preview ready");
    case AiGenerationDialog::State::error:
      return QCoreApplication::translate("AiGenerationDialog", "Generation failed");
    case AiGenerationDialog::State::cancelled:
      return QCoreApplication::translate("AiGenerationDialog", "Cancelled");
    case AiGenerationDialog::State::stale:
      return QCoreApplication::translate("AiGenerationDialog", "Document changed");
    case AiGenerationDialog::State::applying:
      return QCoreApplication::translate("AiGenerationDialog", "Applying…");
  }
  return QCoreApplication::translate("AiGenerationDialog", "Ready");
}

QString diagnosticText(const std::vector<ai::PlanDiagnostic>& diagnostics) {
  if (diagnostics.empty()) {
    return {};
  }
  QStringList lines;
  lines.reserve(static_cast<qsizetype>(diagnostics.size()));
  for (const auto& diagnostic : diagnostics) {
    const auto path = QString::fromStdString(diagnostic.path);
    const auto message = QString::fromStdString(diagnostic.message);
    lines.append(path.isEmpty() ? message : path + QStringLiteral(": ") + message);
  }
  return lines.join(QStringLiteral("\n"));
}

}  // namespace

AiGenerationDialog::AiGenerationDialog(
    core::DocumentHistory& history,
    ai::AiProvider* provider,
    QWidget* parent)
    : QDialog(parent), history_(history) {
  setObjectName(QStringLiteral("aiGenerationDialog"));
  setWindowTitle(tr("Generate editable logo"));
  setModal(false);
  setAttribute(Qt::WA_DeleteOnClose, false);
  setMinimumSize(620, 500);
  buildUi();
  setProvider(provider);
  setState(State::idle);
}

AiGenerationDialog::~AiGenerationDialog() {
  disconnectProvider();
  if (state_ == State::generating && provider_ != nullptr && request_id_ != 0U) {
    provider_->cancel(request_id_);
  }
}

void AiGenerationDialog::buildUi() {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(12, 12, 12, 12);
  root->setSpacing(8);

  auto* prompt_label = new QLabel(tr("Describe the logo (optional when using an image)"), this);
  prompt_label->setObjectName(QStringLiteral("aiPromptLabel"));
  root->addWidget(prompt_label);
  prompt_edit_ = new QPlainTextEdit(this);
  prompt_edit_->setObjectName(QStringLiteral("aiPromptEdit"));
  prompt_edit_->setPlaceholderText(tr("Describe an editable geometric logo, or add a reference image…"));
  prompt_edit_->setMaximumHeight(100);
  prompt_edit_->setTabChangesFocus(false);
  prompt_edit_->setAccessibleName(tr("Logo prompt"));
  root->addWidget(prompt_edit_);
  connect(prompt_edit_, &QPlainTextEdit::textChanged, this, [this] {
    updateButtons();
  });

  auto* input_row = new QHBoxLayout;
  image_button_ = new QPushButton(tr("Choose reference images…"), this);
  image_button_->setObjectName(QStringLiteral("chooseAiImagesButton"));
  input_row->addWidget(image_button_);
  input_row->addStretch(1);
  root->addLayout(input_row);
  image_list_ = new QListWidget(this);
  image_list_->setObjectName(QStringLiteral("aiImageList"));
  image_list_->setMaximumHeight(65);
  image_list_->setSelectionMode(QAbstractItemView::NoSelection);
  image_list_->setAccessibleName(tr("Reference images"));
  root->addWidget(image_list_);
  connect(image_button_, &QPushButton::clicked, this, &AiGenerationDialog::chooseImages);

  consent_ = new QCheckBox(
      tr("I agree that the prompt and selected images will be sent to Codex."), this);
  consent_->setObjectName(QStringLiteral("aiConsentCheckBox"));
  consent_->setChecked(false);
  root->addWidget(consent_);
  connect(consent_, &QCheckBox::toggled, this, [this] { updateButtons(); });

  auto* splitter = new QSplitter(Qt::Horizontal, this);
  splitter->setObjectName(QStringLiteral("aiPreviewSplitter"));
  auto* node_panel = new QWidget(splitter);
  auto* node_layout = new QVBoxLayout(node_panel);
  node_layout->setContentsMargins(0, 0, 4, 0);
  node_layout->addWidget(new QLabel(tr("Construction nodes"), node_panel));
  node_list_ = new QListWidget(node_panel);
  node_list_->setObjectName(QStringLiteral("aiNodeList"));
  node_list_->setSelectionMode(QAbstractItemView::NoSelection);
  node_list_->setAccessibleName(tr("Construction nodes"));
  node_layout->addWidget(node_list_);
  splitter->addWidget(node_panel);

  auto* preview_panel = new QWidget(splitter);
  preview_layout_ = new QVBoxLayout(preview_panel);
  preview_layout_->setContentsMargins(4, 0, 0, 0);
  preview_layout_->addWidget(new QLabel(tr("Geometry preview appears here after generation."),
                                        preview_panel));
  splitter->addWidget(preview_panel);
  splitter->setStretchFactor(1, 1);
  splitter->setSizes({220, 460});
  root->addWidget(splitter, 1);

  status_label_ = new QLabel(this);
  status_label_->setObjectName(QStringLiteral("aiStatusLabel"));
  status_label_->setWordWrap(true);
  status_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  root->addWidget(status_label_);

  auto* button_row = new QHBoxLayout;
  button_row->addStretch(1);
  cancel_button_ = new QPushButton(tr("Cancel"), this);
  cancel_button_->setObjectName(QStringLiteral("aiCancelButton"));
  generate_button_ = new QPushButton(tr("Generate"), this);
  generate_button_->setObjectName(QStringLiteral("aiGenerateButton"));
  apply_button_ = new QPushButton(tr("Apply"), this);
  apply_button_->setObjectName(QStringLiteral("aiApplyButton"));
  button_row->addWidget(cancel_button_);
  button_row->addWidget(generate_button_);
  button_row->addWidget(apply_button_);
  root->addLayout(button_row);
  connect(generate_button_, &QPushButton::clicked, this, &AiGenerationDialog::generate);
  connect(cancel_button_, &QPushButton::clicked, this, &AiGenerationDialog::cancelGeneration);
  connect(apply_button_, &QPushButton::clicked, this, &AiGenerationDialog::applyPlan);
}

void AiGenerationDialog::setProvider(ai::AiProvider* provider) {
  if (provider_ == provider) {
    return;
  }
  if (state_ == State::generating) {
    cancelGeneration();
  }
  disconnectProvider();
  provider_ = provider;
  if (provider_ == nullptr) {
    setDiagnostic(tr("No AI provider is configured."));
    updateButtons();
    return;
  }
  progress_connection_ = connect(provider_, &ai::AiProvider::progress, this,
                                 &AiGenerationDialog::providerProgress);
  success_connection_ = connect(provider_, &ai::AiProvider::succeeded, this,
                                 &AiGenerationDialog::providerSucceeded);
  failure_connection_ = connect(provider_, &ai::AiProvider::failed, this,
                                &AiGenerationDialog::providerFailed);
  updateButtons();
}

void AiGenerationDialog::disconnectProvider() {
  QObject::disconnect(progress_connection_);
  QObject::disconnect(success_connection_);
  QObject::disconnect(failure_connection_);
  progress_connection_ = {};
  success_connection_ = {};
  failure_connection_ = {};
}

void AiGenerationDialog::setImagePaths(QStringList paths) {
  if (paths.size() > 8) {
    paths = paths.mid(0, 8);
  }
  image_paths_ = std::move(paths);
  image_list_->clear();
  for (const auto& path : image_paths_) {
    auto* item = new QListWidgetItem(QFileInfo(path).fileName(), image_list_);
    item->setToolTip(QFileInfo(path).fileName());
  }
  QString error;
  for (const auto& path : image_paths_) {
    if (!validateImagePath(path, error)) {
      setDiagnostic(error);
      break;
    }
  }
  updateButtons();
}

bool AiGenerationDialog::validateImagePath(const QString& path, QString& error) const {
  const QFileInfo info(path);
  if (!info.isFile() || !info.isReadable()) {
    error = tr("Reference image is not readable.");
    return false;
  }
  if (info.size() <= 0 || info.size() > kMaxImageBytes) {
    error = tr("Reference image is too large.");
    return false;
  }
  QImageReader reader(info.filePath());
  const QByteArray format = reader.format().toLower();
  if (format != QByteArrayLiteral("png") && format != QByteArrayLiteral("jpg") &&
      format != QByteArrayLiteral("jpeg")) {
    error = tr("Only PNG and JPEG reference images are supported.");
    return false;
  }
  const QSize size = reader.size();
  if (!size.isValid() || size.width() <= 0 || size.height() <= 0 ||
      size.width() > kMaxImageDimension || size.height() > kMaxImageDimension ||
      static_cast<std::uint64_t>(size.width()) * static_cast<std::uint64_t>(size.height()) >
          kMaxImagePixels) {
    error = tr("Reference image dimensions are unsupported.");
    return false;
  }
  const QImage image = reader.read();
  if (image.isNull()) {
    error = tr("Reference image could not be decoded.");
    return false;
  }
  return true;
}

bool AiGenerationDialog::validateInputs(QString& error) const {
  if (provider_ == nullptr) {
    error = tr("No AI provider is configured.");
    return false;
  }
  const QString prompt = prompt_edit_->toPlainText();
  if (prompt.trimmed().isEmpty() && image_paths_.isEmpty()) {
    error = tr("Enter a logo description or choose a reference image first.");
    return false;
  }
  if (prompt.size() > kPromptMaxLength) {
    error = tr("The logo description is too long.");
    return false;
  }
  if (!consent_->isChecked()) {
    error = tr("Consent is required before sending the prompt or images to Codex.");
    return false;
  }
  if (image_paths_.size() > 8) {
    error = tr("Select no more than eight reference images.");
    return false;
  }
  for (const auto& path : image_paths_) {
    if (!validateImagePath(path, error)) {
      return false;
    }
  }
  return true;
}

void AiGenerationDialog::setState(const State state) {
  state_ = state;
  if (status_label_->text().isEmpty() || state == State::idle || state == State::generating) {
    status_label_->setText(stateText(state));
  }
  updateButtons();
}

void AiGenerationDialog::setDiagnostic(QString message) {
  status_label_->setText(std::move(message));
}

void AiGenerationDialog::updateButtons() {
  const bool generating = state_ == State::generating || state_ == State::applying;
  QString input_error;
  const bool valid_inputs = !generating && validateInputs(input_error);
  generate_button_->setEnabled(valid_inputs);
  cancel_button_->setEnabled(generating || state_ == State::preview_ready || state_ == State::error ||
                             state_ == State::stale || state_ == State::cancelled);
  apply_button_->setEnabled(state_ == State::preview_ready && plan_.has_value() &&
                            history_.revision() == base_revision_);
  image_button_->setEnabled(!generating);
}

void AiGenerationDialog::clearPreview() {
  if (preview_canvas_ != nullptr) {
    preview_layout_->removeWidget(preview_canvas_);
    delete preview_canvas_;
    preview_canvas_ = nullptr;
  }
  preview_history_.reset();
  node_list_->clear();
}

void AiGenerationDialog::resetForNewRequest() {
  plan_.reset();
  clearPreview();
  base_revision_ = history_.revision();
  setDiagnostic(tr("Validating request…"));
}

void AiGenerationDialog::generate() {
  if (state_ == State::generating || state_ == State::applying) {
    return;
  }
  QString error;
  if (!validateInputs(error)) {
    setState(State::error);
    setDiagnostic(std::move(error));
    return;
  }
  resetForNewRequest();
  setState(State::generating);
  const auto request_id = next_request_id_++;
  request_id_ = request_id;
  const ai::GenerationRequest request{
      prompt_edit_->toPlainText(), image_paths_, 120'000U, request_id};
  const auto returned_id = provider_->request(request);
  if (returned_id != request_id && state_ == State::generating) {
    request_id_ = returned_id;
  }
}

void AiGenerationDialog::cancelGeneration() {
  if (state_ == State::generating) {
    const auto request_id = request_id_;
    request_id_ = 0;
    setState(State::cancelled);
    setDiagnostic(tr("Generation cancelled."));
    if (provider_ != nullptr && request_id != 0U) {
      provider_->cancel(request_id);
    }
    return;
  }
  if (state_ == State::preview_ready || state_ == State::error || state_ == State::stale ||
      state_ == State::cancelled) {
    plan_.reset();
    clearPreview();
    setState(State::idle);
    setDiagnostic(tr("Ready"));
  }
}

void AiGenerationDialog::providerProgress(const ai::AiRequestId request_id,
                                          const ai::AiProgress progress) {
  if (state_ != State::generating || request_id != request_id_) {
    return;
  }
  setDiagnostic(progress.message.isEmpty() ? stateText(state_) : progress.message);
}

void AiGenerationDialog::providerSucceeded(
    const ai::AiRequestId request_id,
    ai::LogoConstructionPlan plan) {
  if (state_ != State::generating || request_id != request_id_) {
    return;
  }
  request_id_ = 0;
  if (history_.revision() != base_revision_) {
    plan_.reset();
    clearPreview();
    setState(State::stale);
    setDiagnostic(tr("The document changed while the logo was generating. Generate again."));
    return;
  }
  auto compilation = ai::PlanCompiler::preview(plan, history_.document());
  if (!compilation) {
    plan_.reset();
    clearPreview();
    setState(State::error);
    setDiagnostic(diagnosticText(compilation.diagnostics));
    return;
  }
  showPlan(plan, std::move(compilation));
}

void AiGenerationDialog::providerFailed(const ai::AiRequestId request_id,
                                        const ai::AiError error) {
  if (state_ != State::generating || request_id != request_id_) {
    return;
  }
  request_id_ = 0;
  if (error.kind == ai::AiErrorKind::cancelled) {
    setState(State::cancelled);
  } else {
    setState(State::error);
  }
  setDiagnostic(error.message.isEmpty() ? tr("AI request failed.") : error.message);
}

void AiGenerationDialog::showPlan(
    const ai::LogoConstructionPlan& plan,
    ai::PlanCompilationResult compilation) {
  if (!compilation.document.has_value() || compilation.diagnostics.size() != 0U) {
    setState(State::error);
    setDiagnostic(diagnosticText(compilation.diagnostics));
    return;
  }
  plan_ = plan;
  clearPreview();
  node_list_->clear();
  for (const auto& node : plan.nodes) {
    auto* item = new QListWidgetItem(
        QString::fromStdString(node.name) + QStringLiteral(" · ") + QString::fromStdString(node.id),
        node_list_);
    item->setToolTip(QString::fromStdString(node.id));
  }

  preview_history_ = std::make_unique<core::DocumentHistory>(*compilation.document);
  preview_canvas_ = new CanvasView(*preview_history_, this);
  preview_canvas_->setObjectName(QStringLiteral("aiGeometryPreview"));
  preview_canvas_->setEnabled(false);
  preview_canvas_->setFocusPolicy(Qt::NoFocus);
  preview_canvas_->setMinimumSize(300, 260);
  preview_layout_->addWidget(preview_canvas_);
  setState(State::preview_ready);
  setDiagnostic(tr("Preview ready. Apply to add the editable construction."));
}

void AiGenerationDialog::applyPlan() {
  if (state_ != State::preview_ready || !plan_.has_value()) {
    return;
  }
  if (history_.revision() != base_revision_) {
    setState(State::stale);
    setDiagnostic(tr("The document changed. Generate the logo again before applying."));
    apply_button_->setEnabled(false);
    return;
  }
  setState(State::applying);
  const auto result = ai::PlanCompiler::apply(*plan_, history_);
  if (!result) {
    setState(State::error);
    setDiagnostic(diagnosticText(result.diagnostics));
    return;
  }
  plan_.reset();
  clearPreview();
  setState(State::idle);
  setDiagnostic(tr("Logo applied as an editable construction."));
  emit applied();
}

void AiGenerationDialog::reject() {
  if (state_ == State::generating) {
    cancelGeneration();
    return;
  }
  QDialog::reject();
}

void AiGenerationDialog::closeEvent(QCloseEvent* event) {
  if (state_ == State::generating) {
    cancelGeneration();
  }
  event->accept();
  hide();
}

void AiGenerationDialog::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Escape) {
    if (state_ == State::generating) {
      cancelGeneration();
    } else {
      reject();
    }
    event->accept();
    return;
  }
  QDialog::keyPressEvent(event);
}

void AiGenerationDialog::chooseImages() {
  const auto paths = QFileDialog::getOpenFileNames(
      this, tr("Choose reference images"), {},
      QStringLiteral("Images (*.png *.jpg *.jpeg)"));
  if (!paths.isEmpty()) {
    setImagePaths(paths);
  }
}

}  // namespace signet::ui
