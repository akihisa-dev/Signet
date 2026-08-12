// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "ai/plan_compiler.h"
#include "ai/provider.h"
#include "core/document.h"

#include <QDialog>

#include <cstdint>
#include <memory>

class QCheckBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QVBoxLayout;

namespace signet::ui {

class CanvasView;

class AiGenerationDialog final : public QDialog {
  Q_OBJECT

 public:
  enum class State : std::uint8_t {
    idle,
    generating,
    preview_ready,
    error,
    cancelled,
    stale,
    applying,
  };

  explicit AiGenerationDialog(
      core::DocumentHistory& history,
      ai::AiProvider* provider,
      QWidget* parent = nullptr);
  ~AiGenerationDialog() override;

  void setProvider(ai::AiProvider* provider);
  [[nodiscard]] ai::AiProvider* provider() const noexcept { return provider_; }
  [[nodiscard]] State state() const noexcept { return state_; }
  [[nodiscard]] ai::AiRequestId activeRequestId() const noexcept { return request_id_; }

  [[nodiscard]] QPlainTextEdit* promptEdit() const noexcept { return prompt_edit_; }
  [[nodiscard]] QCheckBox* consentCheckBox() const noexcept { return consent_; }
  [[nodiscard]] QPushButton* imageButton() const noexcept { return image_button_; }
  [[nodiscard]] QPushButton* generateButton() const noexcept { return generate_button_; }
  [[nodiscard]] QPushButton* cancelButton() const noexcept { return cancel_button_; }
  [[nodiscard]] QPushButton* applyButton() const noexcept { return apply_button_; }
  [[nodiscard]] QLabel* statusLabel() const noexcept { return status_label_; }
  [[nodiscard]] QListWidget* imageList() const noexcept { return image_list_; }
  [[nodiscard]] QListWidget* nodeList() const noexcept { return node_list_; }
  [[nodiscard]] CanvasView* previewCanvas() const noexcept { return preview_canvas_; }

  // Primarily useful for deterministic UI tests; paths are immediately validated
  // and only their basenames are rendered in the dialog.
  void setImagePaths(QStringList paths);
  [[nodiscard]] QStringList imagePaths() const { return image_paths_; }

 signals:
  void applied();

 private slots:
  void chooseImages();
  void generate();
  void cancelGeneration();
  void applyPlan();
  void providerProgress(ai::AiRequestId request_id, ai::AiProgress progress);
  void providerSucceeded(ai::AiRequestId request_id, ai::LogoConstructionPlan plan);
  void providerFailed(ai::AiRequestId request_id, ai::AiError error);

 protected:
  void reject() override;
  void closeEvent(QCloseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;

 private:
  void buildUi();
  void disconnectProvider();
  void updateButtons();
  void setState(State state);
  void setDiagnostic(QString message);
  void clearPreview();
  bool validateInputs(QString& error) const;
  bool validateImagePath(const QString& path, QString& error) const;
  void showPlan(const ai::LogoConstructionPlan& plan, ai::PlanCompilationResult compilation);
  void resetForNewRequest();
  void cancelAndClose();

  core::DocumentHistory& history_;
  ai::AiProvider* provider_{};
  QMetaObject::Connection progress_connection_;
  QMetaObject::Connection success_connection_;
  QMetaObject::Connection failure_connection_;

  QPlainTextEdit* prompt_edit_{};
  QCheckBox* consent_{};
  QPushButton* image_button_{};
  QPushButton* generate_button_{};
  QPushButton* cancel_button_{};
  QPushButton* apply_button_{};
  QLabel* status_label_{};
  QListWidget* image_list_{};
  QListWidget* node_list_{};
  CanvasView* preview_canvas_{};
  QVBoxLayout* preview_layout_{};
  std::unique_ptr<core::DocumentHistory> preview_history_;

  QStringList image_paths_;
  std::optional<ai::LogoConstructionPlan> plan_;
  std::uint64_t base_revision_{};
  ai::AiRequestId request_id_{};
  ai::AiRequestId next_request_id_{1};
  State state_{State::idle};
};

}  // namespace signet::ui

Q_DECLARE_METATYPE(signet::ui::AiGenerationDialog::State)
