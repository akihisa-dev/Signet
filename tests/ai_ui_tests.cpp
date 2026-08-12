// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ai/provider.h"
#include "ui/ai_generation_dialog.h"
#include "ui/main_window.h"

#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QTest>

#include <utility>

namespace {

class FakeAiProvider final : public signet::ai::AiProvider {
  Q_OBJECT

 public:
  using AiProvider::AiProvider;

  enum class Mode { immediate, hold, authentication, rate_limited };

  signet::ai::AiRequestId request(const signet::ai::GenerationRequest& request) override {
    last_request = request;
    ++request_count;
    const auto id = request.request_id == 0U ? next_id_++ : request.request_id;
    active_id = id;
    if (mode == Mode::authentication) {
      emit failed(id, {signet::ai::AiErrorKind::authentication_required,
                       QStringLiteral("authentication required")});
    } else if (mode == Mode::rate_limited) {
      emit failed(id, {signet::ai::AiErrorKind::rate_limited,
                       QStringLiteral("rate limit reached")});
    } else if (mode == Mode::immediate) {
      QTimer::singleShot(0, this, [this, id] { emit succeeded(id, validPlan()); });
    }
    return id;
  }

  void cancel(const signet::ai::AiRequestId request_id) override {
    cancel_called = true;
    cancelled_id = request_id;
  }

  void emitSuccess() { emit succeeded(active_id, validPlan()); }
  void emitInvalidPlan() {
    auto plan = validPlan();
    plan.schema_version = 999;
    emit succeeded(active_id, std::move(plan));
  }

  Mode mode{Mode::immediate};
  int request_count{};
  bool cancel_called{};
  signet::ai::AiRequestId active_id{};
  signet::ai::AiRequestId cancelled_id{};

  signet::ai::GenerationRequest last_request;

 private:
  static signet::ai::LogoConstructionPlan validPlan() {
    signet::ai::LogoConstructionPlan plan;
    plan.nodes.push_back(signet::ai::PlanNode{
        "generated-circle", "Generated circle",
        signet::ai::PlanPrimitive{signet::core::Circle{18.0},
                                  signet::core::Transform{signet::core::Point{0.0, 30.0}}}});
    plan.roots.push_back("generated-circle");
    return plan;
  }

  signet::ai::AiRequestId next_id_{1};
};

void testActionSingletonAndApply() {
  FakeAiProvider provider;
  signet::ui::MainWindow window(&provider);
  const auto initial_count = window.history().document().nodes().size();
  QVERIFY(window.aiLogoAction() != nullptr);
  window.aiLogoAction()->trigger();
  auto* dialog = window.aiGenerationDialog();
  QVERIFY(dialog != nullptr);
  window.aiLogoAction()->trigger();
  QCOMPARE(window.aiGenerationDialog(), dialog);
  QVERIFY(!dialog->generateButton()->isEnabled());
  dialog->promptEdit()->setPlainText(QStringLiteral("A round geometric mark"));
  QVERIFY(!dialog->generateButton()->isEnabled());
  dialog->consentCheckBox()->setChecked(true);
  QVERIFY(dialog->generateButton()->isEnabled());
  dialog->generateButton()->click();
  QTRY_COMPARE_WITH_TIMEOUT(
      static_cast<int>(dialog->state()),
      static_cast<int>(signet::ui::AiGenerationDialog::State::preview_ready), 1000);
  QCOMPARE(window.history().document().nodes().size(), initial_count);
  QVERIFY(dialog->previewCanvas() != nullptr);
  QVERIFY(dialog->nodeList()->count() == 1);
  QVERIFY(dialog->applyButton()->isEnabled());
  dialog->applyButton()->click();
  QCOMPARE(window.history().document().nodes().size(), initial_count + 1U);
  QVERIFY(window.history().canUndo());
  QVERIFY(window.history().undo());
  QCOMPARE(window.history().document().nodes().size(), initial_count);
}

void preparePrompt(signet::ui::AiGenerationDialog* dialog) {
  dialog->promptEdit()->setPlainText(QStringLiteral("A geometric round mark"));
  dialog->consentCheckBox()->setChecked(true);
}

void prepareConsent(signet::ui::AiGenerationDialog* dialog) {
  dialog->consentCheckBox()->setChecked(true);
}

void testPromptOrImageModes() {
  FakeAiProvider provider;
  signet::ui::MainWindow window(&provider);
  window.aiLogoAction()->trigger();
  auto* dialog = window.aiGenerationDialog();
  prepareConsent(dialog);
  QVERIFY(!dialog->generateButton()->isEnabled());
  dialog->generateButton()->click();
  QCOMPARE(provider.request_count, 0);

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto image_path = directory.filePath(QStringLiteral("reference.png"));
  QImage image(8, 8, QImage::Format_ARGB32);
  image.fill(Qt::white);
  QVERIFY(image.save(image_path, "PNG"));
  dialog->setImagePaths({image_path});
  QVERIFY(dialog->generateButton()->isEnabled());
  dialog->generateButton()->click();
  QTRY_COMPARE_WITH_TIMEOUT(
      static_cast<int>(dialog->state()),
      static_cast<int>(signet::ui::AiGenerationDialog::State::preview_ready), 1000);
  QVERIFY(provider.last_request.prompt.isEmpty());
  QCOMPARE(provider.last_request.image_paths, QStringList{image_path});
}

void testInvalidAndOversizeImages() {
  FakeAiProvider provider;
  signet::ui::MainWindow window(&provider);
  window.aiLogoAction()->trigger();
  auto* dialog = window.aiGenerationDialog();
  preparePrompt(dialog);

  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto invalid = directory.filePath(QStringLiteral("not-an-image.png"));
  QFile invalid_file(invalid);
  QVERIFY(invalid_file.open(QIODevice::WriteOnly));
  QVERIFY(invalid_file.write("not an image") > 0);
  invalid_file.close();
  dialog->setImagePaths({invalid});
  QVERIFY(!dialog->generateButton()->isEnabled());
  const int invalid_request_count = provider.request_count;
  dialog->generateButton()->click();
  QCOMPARE(provider.request_count, invalid_request_count);

  const auto oversized = directory.filePath(QStringLiteral("oversized.png"));
  QFile oversized_file(oversized);
  QVERIFY(oversized_file.open(QIODevice::WriteOnly));
  QVERIFY(oversized_file.resize(20LL * 1024LL * 1024LL + 1LL));
  oversized_file.close();
  dialog->setImagePaths({oversized});
  QVERIFY(!dialog->generateButton()->isEnabled());

  const auto valid = directory.filePath(QStringLiteral("reference.png"));
  QImage image(8, 8, QImage::Format_ARGB32);
  image.fill(Qt::white);
  QVERIFY(image.save(valid, "PNG"));
  dialog->setImagePaths({valid});
  QVERIFY(dialog->generateButton()->isEnabled());
  dialog->generateButton()->click();
  QTRY_COMPARE_WITH_TIMEOUT(
      static_cast<int>(dialog->state()),
      static_cast<int>(signet::ui::AiGenerationDialog::State::preview_ready), 1000);
  QCOMPARE(provider.last_request.image_paths, QStringList{valid});
}

void testCancelCloseAndLateSuccess() {
  FakeAiProvider provider;
  provider.mode = FakeAiProvider::Mode::hold;
  signet::ui::MainWindow window(&provider);
  window.aiLogoAction()->trigger();
  auto* dialog = window.aiGenerationDialog();
  preparePrompt(dialog);
  const auto count = window.history().document().nodes().size();
  dialog->generateButton()->click();
  const int request_count = provider.request_count;
  dialog->generateButton()->click();
  QCOMPARE(provider.request_count, request_count);
  QCOMPARE(static_cast<int>(dialog->state()),
           static_cast<int>(signet::ui::AiGenerationDialog::State::generating));
  QTest::keyClick(dialog, Qt::Key_Escape);
  QVERIFY(provider.cancel_called);
  QCOMPARE(static_cast<int>(dialog->state()),
           static_cast<int>(signet::ui::AiGenerationDialog::State::cancelled));
  provider.emitSuccess();
  QCoreApplication::processEvents();
  QCOMPARE(window.history().document().nodes().size(), count);

  dialog->cancelButton()->click();
  dialog->generateButton()->click();
  dialog->close();
  QCOMPARE(static_cast<int>(dialog->state()),
           static_cast<int>(signet::ui::AiGenerationDialog::State::cancelled));
  provider.emitSuccess();
  QCoreApplication::processEvents();
  QCOMPARE(window.history().document().nodes().size(), count);
}

void testInvalidPlanDoesNotMutateDocument() {
  FakeAiProvider provider;
  provider.mode = FakeAiProvider::Mode::hold;
  signet::ui::MainWindow window(&provider);
  window.aiLogoAction()->trigger();
  auto* dialog = window.aiGenerationDialog();
  preparePrompt(dialog);
  const auto count = window.history().document().nodes().size();
  dialog->generateButton()->click();
  provider.emitInvalidPlan();
  QCoreApplication::processEvents();
  QCOMPARE(static_cast<int>(dialog->state()),
           static_cast<int>(signet::ui::AiGenerationDialog::State::error));
  QCOMPARE(window.history().document().nodes().size(), count);
  QVERIFY(!dialog->applyButton()->isEnabled());
}

void testStaleAfterEditAndUndo() {
  FakeAiProvider provider;
  provider.mode = FakeAiProvider::Mode::hold;
  signet::ui::MainWindow window(&provider);
  window.aiLogoAction()->trigger();
  auto* dialog = window.aiGenerationDialog();
  preparePrompt(dialog);
  dialog->generateButton()->click();
  const auto before = window.history().document().nodes().size();
  window.history().addPrimitive("edit", signet::core::Circle{3.0});
  QVERIFY(window.history().undo());
  provider.emitSuccess();
  QCoreApplication::processEvents();
  QCOMPARE(static_cast<int>(dialog->state()),
           static_cast<int>(signet::ui::AiGenerationDialog::State::stale));
  QCOMPARE(window.history().document().nodes().size(), before);
  QVERIFY(!dialog->applyButton()->isEnabled());
}

void testProviderErrorsAndNarrowFocus() {
  FakeAiProvider provider;
  signet::ui::MainWindow window(&provider);
  window.aiLogoAction()->trigger();
  auto* dialog = window.aiGenerationDialog();
  preparePrompt(dialog);
  dialog->resize(320, 420);
  dialog->show();
  dialog->promptEdit()->setFocus();
  QCOMPARE(dialog->focusWidget(), dialog->promptEdit());
  QVERIFY(dialog->width() <= 320);

  provider.mode = FakeAiProvider::Mode::authentication;
  dialog->generateButton()->click();
  QCOMPARE(static_cast<int>(dialog->state()),
           static_cast<int>(signet::ui::AiGenerationDialog::State::error));
  QVERIFY(dialog->statusLabel()->text().contains(QStringLiteral("authentication")));
  dialog->cancelButton()->click();
  provider.mode = FakeAiProvider::Mode::rate_limited;
  dialog->generateButton()->click();
  QCOMPARE(static_cast<int>(dialog->state()),
           static_cast<int>(signet::ui::AiGenerationDialog::State::error));
  QVERIFY(dialog->statusLabel()->text().contains(QStringLiteral("rate limit")));
}

}  // namespace

#include "ai_ui_tests.moc"

int main(int argc, char** argv) {
  QApplication application(argc, argv);
  testActionSingletonAndApply();
  testPromptOrImageModes();
  testInvalidAndOversizeImages();
  testCancelCloseAndLateSuccess();
  testStaleAfterEditAndUndo();
  testProviderErrorsAndNarrowFocus();
  testInvalidPlanDoesNotMutateDocument();
}
