// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "ai/logo_construction_plan.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <cstdint>

namespace signet::ai {

using AiRequestId = std::uint64_t;

enum class AiErrorKind : std::uint8_t {
  busy,
  missing_binary,
  invalid_input,
  invalid_image,
  authentication_required,
  rate_limited,
  timeout,
  cancelled,
  process_failure,
  invalid_response,
  invalid_plan,
};

struct AiError final {
  AiErrorKind kind{AiErrorKind::process_failure};
  QString message;

  friend bool operator==(const AiError&, const AiError&) = default;
};

struct GenerationRequest final {
  QString prompt;
  QStringList image_paths;
  std::uint32_t timeout_ms{120'000U};
  AiRequestId request_id{};
};

struct AiProgress final {
  enum class Stage : std::uint8_t { validating, starting, generating, parsing };

  Stage stage{Stage::validating};
  QString message;
};

class AiProvider : public QObject {
  Q_OBJECT

 public:
  using QObject::QObject;
  ~AiProvider() override = default;

  [[nodiscard]] virtual AiRequestId request(const GenerationRequest& request) = 0;
  virtual void cancel(AiRequestId request_id) = 0;

 signals:
  void progress(AiRequestId request_id, signet::ai::AiProgress progress);
  void succeeded(AiRequestId request_id, signet::ai::LogoConstructionPlan plan);
  void failed(AiRequestId request_id, signet::ai::AiError error);
};

}  // namespace signet::ai

Q_DECLARE_METATYPE(signet::ai::AiErrorKind)
Q_DECLARE_METATYPE(signet::ai::AiError)
Q_DECLARE_METATYPE(signet::ai::GenerationRequest)
Q_DECLARE_METATYPE(signet::ai::AiProgress)
Q_DECLARE_METATYPE(signet::ai::LogoConstructionPlan)
