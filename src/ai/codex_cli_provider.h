// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "ai/provider.h"

#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>

#include <optional>

namespace signet::ai {

class CodexCliProvider final : public AiProvider {
  Q_OBJECT

 public:
  explicit CodexCliProvider(QString executable_override = {}, QObject* parent = nullptr);
  ~CodexCliProvider() override;

  [[nodiscard]] AiRequestId request(const GenerationRequest& request) override;
  void cancel(AiRequestId request_id) override;

  [[nodiscard]] QString executablePath() const { return executable_path_; }
  [[nodiscard]] static QString discoverExecutable(QString override_path = {});

 private slots:
  void readStandardOutput();
  void readStandardError();
  void processStarted();
  void processFinished(int exit_code, QProcess::ExitStatus exit_status);
  void processError(QProcess::ProcessError error);
  void timeout();
  void killAfterCancelGracePeriod();

 private:
  struct PreparedRequest final {
    QString prompt;
    QString schema_path;
    QString last_message_path;
    QString working_directory;
    QStringList copied_images;
  };

  [[nodiscard]] std::optional<PreparedRequest> prepareRequest(
      const GenerationRequest& request,
      AiError& error);
  [[nodiscard]] bool validateImage(
      const QString& path,
      int index,
      const QString& destination_directory,
      QString& copied_path,
      AiError& error) const;
  [[nodiscard]] bool writeSchema(const QString& path, AiError& error) const;
  [[nodiscard]] AiError classifyFailure(int exit_code, QProcess::ExitStatus status) const;
  void finishWithError(AiError error);
  void resetProcessState();
  void stopProcess(bool kill);

  QString executable_path_;
  QProcess process_;
  QTimer timeout_timer_;
  QTimer cancel_grace_timer_;
  std::unique_ptr<QTemporaryDir> temporary_directory_;
  std::optional<PreparedRequest> prepared_request_;
  QByteArray standard_output_;
  QByteArray standard_error_;
  AiRequestId next_request_id_{1};
  AiRequestId active_request_id_{};
  bool cancel_requested_{};
  bool timeout_requested_{};
  bool output_limit_exceeded_{};
  bool process_error_seen_{};
  qsizetype output_unfinished_line_bytes_{};
  qsizetype error_unfinished_line_bytes_{};
  std::size_t output_line_count_{};
  std::size_t error_line_count_{};
};

}  // namespace signet::ai
