// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ai/codex_cli_provider.h"

#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QStandardPaths>

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <string_view>

namespace signet::ai {

namespace {

constexpr qsizetype kMaxPromptBytes = 32 * 1024;
constexpr qsizetype kMaxOutputBytes = 1024 * 1024;
constexpr qsizetype kMaxErrorBytes = 512 * 1024;
constexpr qsizetype kMaxOutputLineBytes = 64 * 1024;
constexpr std::size_t kMaxOutputLines = 8192;
constexpr qsizetype kMaxImageBytes = 20 * 1024 * 1024;
constexpr int kMaxImageDimension = 4096;
constexpr std::uint64_t kMaxImagePixels = 16ULL * 1024ULL * 1024ULL;
constexpr int kCancelGracePeriodMs = 300;

constexpr auto kConstructionInstruction =
    "You are Signet's geometric logo construction planner.\n"
    "Return exactly one JSON object that conforms to the supplied output schema; do not return "
    "Markdown, commentary, or any other text.\n"
    "Build an editable parametric construction, not a raster trace: represent the logo with "
    "only circle, rectangle, golden_rectangle, and arc primitives, transforms, Boolean nodes "
    "(unite, intersect, subtract, exclusive_or), and symmetry nodes.\n"
    "Use stable ASCII node IDs and ASCII human-readable node names, with explicit references. Keep coordinates finite and within the "
    "schema limits, use a non-zero symmetry direction, and make Boolean operands closed.\n"
    "When reference images are supplied, infer their structure and recreate it with these "
    "geometric primitives and relationships rather than tracing pixels.\n"
    "If no text request is supplied, infer the logo intent from the reference image(s).\n"
    "The schema enforces local numeric and shape constraints; the parser additionally checks "
    "node references, duplicate IDs, cycles, distinct Boolean operands, closed Boolean inputs, "
    "and unsupported Boolean-to-Symmetry relationships.\n"
    "The JSON object must contain only schema_version, coordinate_system, nodes, and roots.\n"
    "User request (may be empty):\n";

QString buildConstructionPrompt(const QString& user_prompt) {
  return QString::fromUtf8(kConstructionInstruction) + user_prompt;
}

QString errorText(const AiErrorKind kind, const QString& detail = {}) {
  const auto prefix = [&] {
    switch (kind) {
      case AiErrorKind::busy: return QStringLiteral("AI request is already running");
      case AiErrorKind::missing_binary: return QStringLiteral("Codex CLI was not found");
      case AiErrorKind::invalid_input: return QStringLiteral("The AI request is invalid");
      case AiErrorKind::invalid_image: return QStringLiteral("The reference image is invalid");
      case AiErrorKind::authentication_required:
        return QStringLiteral("Codex CLI requires ChatGPT authentication");
      case AiErrorKind::rate_limited: return QStringLiteral("Codex CLI rate limit or quota reached");
      case AiErrorKind::timeout: return QStringLiteral("The AI request timed out");
      case AiErrorKind::cancelled: return QStringLiteral("The AI request was cancelled");
      case AiErrorKind::process_failure: return QStringLiteral("Codex CLI failed");
      case AiErrorKind::invalid_response: return QStringLiteral("Codex CLI returned invalid JSON");
      case AiErrorKind::invalid_plan: return QStringLiteral("Codex CLI returned an invalid logo plan");
    }
    return QStringLiteral("AI request failed");
  }();
  return detail.isEmpty() ? prefix : prefix + QStringLiteral(": ") + detail;
}

bool containsAny(const QString& text, const std::initializer_list<QStringView> needles) {
  const auto lower = text.toLower();
  return std::ranges::any_of(needles, [&lower](const QStringView needle) {
    return lower.contains(needle);
  });
}

}  // namespace

CodexCliProvider::CodexCliProvider(QString executable_override, QObject* parent)
    : AiProvider(parent), executable_path_(discoverExecutable(std::move(executable_override))) {
  qRegisterMetaType<AiErrorKind>();
  qRegisterMetaType<AiError>();
  qRegisterMetaType<GenerationRequest>();
  qRegisterMetaType<AiProgress>();
  qRegisterMetaType<LogoConstructionPlan>();

  process_.setProcessChannelMode(QProcess::SeparateChannels);
  connect(&process_, &QProcess::readyReadStandardOutput, this,
          &CodexCliProvider::readStandardOutput);
  connect(&process_, &QProcess::readyReadStandardError, this,
          &CodexCliProvider::readStandardError);
  connect(&process_, &QProcess::finished, this, &CodexCliProvider::processFinished);
  connect(&process_, &QProcess::errorOccurred, this, &CodexCliProvider::processError);
  connect(&process_, &QProcess::started, this, &CodexCliProvider::processStarted);
  timeout_timer_.setSingleShot(true);
  cancel_grace_timer_.setSingleShot(true);
  connect(&timeout_timer_, &QTimer::timeout, this, &CodexCliProvider::timeout);
  connect(&cancel_grace_timer_, &QTimer::timeout, this,
          &CodexCliProvider::killAfterCancelGracePeriod);
}

CodexCliProvider::~CodexCliProvider() {
  timeout_timer_.stop();
  cancel_grace_timer_.stop();
  if (process_.state() != QProcess::NotRunning) {
    process_.kill();
  }
  process_.closeWriteChannel();
  temporary_directory_.reset();
}

QString CodexCliProvider::discoverExecutable(QString override_path) {
  if (!override_path.isEmpty()) {
    const QFileInfo info(override_path);
    if (info.isFile() && info.isExecutable()) {
      return info.canonicalFilePath();
    }
    return {};
  }
  const auto from_path = QStandardPaths::findExecutable(QStringLiteral("codex"));
  if (!from_path.isEmpty()) {
    return QFileInfo(from_path).canonicalFilePath();
  }
  // The ChatGPT desktop bundle may ship a private CLI.  These are read-only
  // discovery candidates; authentication data is never inspected.
  const QStringList bundle_candidates{
      QStringLiteral("/Applications/ChatGPT.app/Contents/Resources/codex"),
      QStringLiteral("/Applications/ChatGPT.app/Contents/Resources/bin/codex"),
      QStringLiteral("/Applications/ChatGPT.app/Contents/Helpers/codex"),
  };
  for (const auto& candidate : bundle_candidates) {
    const QFileInfo info(candidate);
    if (info.isFile() && info.isExecutable()) {
      return info.canonicalFilePath();
    }
  }
  return {};
}

AiRequestId CodexCliProvider::request(const GenerationRequest& request_data) {
  const auto request_id = request_data.request_id != 0U ? request_data.request_id : next_request_id_++;
  if (request_id >= next_request_id_ && request_id != std::numeric_limits<AiRequestId>::max()) {
    next_request_id_ = request_id + 1U;
  }
  if (active_request_id_ != 0U) {
    emit failed(request_id, AiError{AiErrorKind::busy, errorText(AiErrorKind::busy)});
    return request_id;
  }
  emit progress(request_id, AiProgress{AiProgress::Stage::validating,
                                        QStringLiteral("Validating AI request")});
  AiError error;
  const auto prepared = prepareRequest(request_data, error);
  if (!prepared.has_value()) {
    emit failed(request_id, std::move(error));
    return request_id;
  }
  if (executable_path_.isEmpty()) {
    emit failed(request_id,
                AiError{AiErrorKind::missing_binary, errorText(AiErrorKind::missing_binary)});
    return request_id;
  }

  active_request_id_ = request_id;
  prepared_request_ = prepared;
  standard_output_.clear();
  standard_error_.clear();
  cancel_requested_ = false;
  timeout_requested_ = false;
  output_limit_exceeded_ = false;
  process_error_seen_ = false;
  output_unfinished_line_bytes_ = 0;
  error_unfinished_line_bytes_ = 0;
  output_line_count_ = 0;
  error_line_count_ = 0;

  emit progress(request_id, AiProgress{AiProgress::Stage::starting,
                                       QStringLiteral("Starting Codex CLI")});
  QStringList arguments{
      QStringLiteral("exec"), QStringLiteral("--json"), QStringLiteral("--ephemeral"),
      QStringLiteral("--sandbox"), QStringLiteral("read-only"), QStringLiteral("--output-schema"),
      prepared->schema_path, QStringLiteral("--output-last-message"), prepared->last_message_path,
      QStringLiteral("--skip-git-repo-check"), QStringLiteral("-C"), prepared->working_directory};
  for (const auto& image : prepared->copied_images) {
    arguments << QStringLiteral("--image") << image;
  }
  arguments << QStringLiteral("-");
  process_.setWorkingDirectory(prepared->working_directory);
  process_.start(executable_path_, arguments, QIODevice::ReadWrite);
  if (active_request_id_ != request_id) {
    return request_id;
  }
  timeout_timer_.start(static_cast<int>(std::min<std::uint32_t>(request_data.timeout_ms,
                                                                  std::numeric_limits<int>::max())));
  return request_id;
}

void CodexCliProvider::cancel(const AiRequestId request_id) {
  if (request_id == 0U || request_id != active_request_id_) {
    return;
  }
  cancel_requested_ = true;
  timeout_timer_.stop();
  process_.closeWriteChannel();
  if (process_.state() != QProcess::NotRunning) {
    process_.terminate();
    cancel_grace_timer_.start(kCancelGracePeriodMs);
  }
}

std::optional<CodexCliProvider::PreparedRequest> CodexCliProvider::prepareRequest(
    const GenerationRequest& request_data,
    AiError& error) {
  const auto prompt = request_data.prompt.toUtf8();
  if ((prompt.trimmed().isEmpty() && request_data.image_paths.isEmpty()) ||
      prompt.size() > kMaxPromptBytes) {
    error = {AiErrorKind::invalid_input,
             errorText(AiErrorKind::invalid_input,
                       QStringLiteral("prompt and reference images are empty, or prompt is too large"))};
    return std::nullopt;
  }
  if (request_data.timeout_ms == 0U) {
    error = {AiErrorKind::invalid_input,
             errorText(AiErrorKind::invalid_input, QStringLiteral("timeout must be positive"))};
    return std::nullopt;
  }
  if (request_data.image_paths.size() > 8) {
    error = {AiErrorKind::invalid_input,
             errorText(AiErrorKind::invalid_input, QStringLiteral("too many reference images"))};
    return std::nullopt;
  }

  auto temporary = std::make_unique<QTemporaryDir>();
  temporary->setAutoRemove(true);
  if (!temporary->isValid()) {
    error = {AiErrorKind::process_failure,
             errorText(AiErrorKind::process_failure, QStringLiteral("cannot create temporary directory"))};
    return std::nullopt;
  }
  if (!QFile::setPermissions(temporary->path(), QFileDevice::ReadOwner |
                                                   QFileDevice::WriteOwner |
                                                   QFileDevice::ExeOwner)) {
    error = {AiErrorKind::process_failure,
             errorText(AiErrorKind::process_failure, QStringLiteral("cannot secure temporary directory"))};
    return std::nullopt;
  }
  PreparedRequest prepared{buildConstructionPrompt(request_data.prompt),
                           temporary->filePath(QStringLiteral("schema.json")),
                           temporary->filePath(QStringLiteral("last-message.json")), temporary->path(), {}};
  if (!writeSchema(prepared.schema_path, error)) {
    return std::nullopt;
  }
  for (int index = 0; index < request_data.image_paths.size(); ++index) {
    QString copied;
    if (!validateImage(request_data.image_paths.at(index), index, temporary->path(), copied, error)) {
      return std::nullopt;
    }
    prepared.copied_images.push_back(std::move(copied));
  }
  temporary_directory_ = std::move(temporary);
  return prepared;
}

bool CodexCliProvider::validateImage(
    const QString& path,
    const int index,
    const QString& destination_directory,
    QString& copied_path,
    AiError& error) const {
  const QFileInfo input(path);
  if (!input.exists() || !input.isFile()) {
    error = {AiErrorKind::invalid_image,
             errorText(AiErrorKind::invalid_image, QStringLiteral("file does not exist"))};
    return false;
  }
  const auto canonical = input.canonicalFilePath();
  QFile source(canonical);
  if (canonical.isEmpty() || !source.open(QIODevice::ReadOnly) || source.size() > kMaxImageBytes) {
    error = {AiErrorKind::invalid_image,
             errorText(AiErrorKind::invalid_image, QStringLiteral("file is unreadable or too large"))};
    return false;
  }
  QImageReader reader(&source);
  const auto format = reader.format().toLower();
  if (!reader.canRead() || (format != QByteArrayLiteral("png") && format != QByteArrayLiteral("jpg") &&
      format != QByteArrayLiteral("jpeg"))) {
    error = {AiErrorKind::invalid_image,
             errorText(AiErrorKind::invalid_image, QStringLiteral("only PNG and JPEG are supported"))};
    return false;
  }
  const auto size = reader.size();
  if (!size.isValid() || size.width() <= 0 || size.height() <= 0 ||
      size.width() > kMaxImageDimension || size.height() > kMaxImageDimension ||
      static_cast<std::uint64_t>(size.width()) * static_cast<std::uint64_t>(size.height()) >
          kMaxImagePixels) {
    error = {AiErrorKind::invalid_image,
             errorText(AiErrorKind::invalid_image, QStringLiteral("image dimensions are unsupported"))};
    return false;
  }
  const auto suffix = format == QByteArrayLiteral("png") ? QStringLiteral("png") : QStringLiteral("jpg");
  copied_path = destination_directory + QStringLiteral("/image-%1.%2").arg(index).arg(suffix);
  source.close();
  if (!QFile::copy(canonical, copied_path)) {
    error = {AiErrorKind::invalid_image,
             errorText(AiErrorKind::invalid_image, QStringLiteral("cannot copy image to private input directory"))};
    return false;
  }
  if (!QFile::setPermissions(copied_path, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
    error = {AiErrorKind::invalid_image,
             errorText(AiErrorKind::invalid_image, QStringLiteral("cannot secure copied image"))};
    return false;
  }
  return true;
}

bool CodexCliProvider::writeSchema(const QString& path, AiError& error) const {
  const QByteArray schema = R"JSON({
  "type":"object", "additionalProperties":false,
  "required":["schema_version","coordinate_system","nodes","roots"],
  "properties":{
    "schema_version":{"type":"integer","const":1},
    "coordinate_system":{
      "type":"object", "additionalProperties":false,
      "required":["unit","origin","x_axis","y_axis","bounds"],
      "properties":{
        "unit":{"type":"string","const":"logical"},
        "origin":{"type":"string","const":"center"},
        "x_axis":{"type":"string","const":"right"},
        "y_axis":{"type":"string","const":"up"},
        "bounds":{"type":"array","minItems":4,"maxItems":4,"items":{"type":"number","minimum":-1000000,"maximum":1000000}}
      }
    },
    "nodes":{
      "type":"array", "minItems":1, "maxItems":64,
      "items":{
        "oneOf":[
          {
            "type":"object", "additionalProperties":false,
                "required":["id","name","kind","primitive","transform"],
            "properties":{
              "id":{"type":"string","minLength":1,"maxLength":64,"pattern":"^[A-Za-z0-9_-]+$"},
              "name":{"type":"string","minLength":1,"maxLength":64,"pattern":"^[A-Za-z0-9 _.-]+$"},
              "kind":{"type":"string","const":"primitive"},
              "primitive":{
                "oneOf":[
                  {"type":"object","additionalProperties":false,"required":["type","radius"],"properties":{"type":{"const":"circle"},"radius":{"type":"number","exclusiveMinimum":0,"maximum":1000000}}},
                  {"type":"object","additionalProperties":false,"required":["type","width","height"],"properties":{"type":{"const":"rectangle"},"width":{"type":"number","exclusiveMinimum":0,"maximum":1000000},"height":{"type":"number","exclusiveMinimum":0,"maximum":1000000}}},
                  {"type":"object","additionalProperties":false,"required":["type","short_side"],"properties":{"type":{"const":"golden_rectangle"},"short_side":{"type":"number","exclusiveMinimum":0,"maximum":1000000}}},
                  {"type":"object","additionalProperties":false,"required":["type","radius","start_degrees","sweep_degrees"],"properties":{"type":{"const":"arc"},"radius":{"type":"number","exclusiveMinimum":0,"maximum":1000000},"start_degrees":{"type":"number","minimum":-1000000,"maximum":1000000},"sweep_degrees":{"anyOf":[{"type":"number","exclusiveMinimum":0,"maximum":360},{"type":"number","minimum":-360,"exclusiveMaximum":0}]}}}
                ]
              },
              "transform":{
                "type":"object", "additionalProperties":false,
                "required":["translation","rotation_degrees","scale"],
                "properties":{
                  "translation":{"$ref":"#/$defs/point"},
                  "rotation_degrees":{"type":"number","minimum":-1000000,"maximum":1000000},
                  "scale":{"$ref":"#/$defs/nonzero_point"}
                }
              }
            }
          },
          {
            "type":"object", "additionalProperties":false,
            "required":["id","name","kind","operation","left","right"],
            "properties":{
              "id":{"type":"string","minLength":1,"maxLength":64,"pattern":"^[A-Za-z0-9_-]+$"},
              "name":{"type":"string","minLength":1,"maxLength":64,"pattern":"^[A-Za-z0-9 _.-]+$"},
              "kind":{"type":"string","const":"boolean"},
              "operation":{"type":"string","enum":["unite","intersect","subtract","exclusive_or"]},
              "left":{"type":"string","minLength":1,"maxLength":64},
              "right":{"type":"string","minLength":1,"maxLength":64}
            }
          },
          {
            "type":"object", "additionalProperties":false,
            "required":["id","name","kind","input","axis"],
            "properties":{
              "id":{"type":"string","minLength":1,"maxLength":64,"pattern":"^[A-Za-z0-9_-]+$"},
              "name":{"type":"string","minLength":1,"maxLength":64,"pattern":"^[A-Za-z0-9 _.-]+$"},
              "kind":{"type":"string","const":"symmetry"},
              "input":{"type":"string","minLength":1,"maxLength":64},
              "axis":{"type":"object","additionalProperties":false,"required":["origin","direction"],"properties":{"origin":{"$ref":"#/$defs/point"},"direction":{"allOf":[{"$ref":"#/$defs/point"},{"anyOf":[{"properties":{"x":{"not":{"const":0}}}},{"properties":{"y":{"not":{"const":0}}}}]}]}}}
            }
          }
        ]
      }
    },
    "roots":{"type":"array","minItems":1,"items":{"type":"string","minLength":1,"maxLength":64}}
  },
  "$defs":{
    "point":{"type":"object","additionalProperties":false,"required":["x","y"],"properties":{"x":{"type":"number","minimum":-1000000,"maximum":1000000},"y":{"type":"number","minimum":-1000000,"maximum":1000000}}},
    "nonzero_point":{"allOf":[{"$ref":"#/$defs/point"},{"type":"object","properties":{"x":{"not":{"const":0}},"y":{"not":{"const":0}}}}]}
  }
})JSON";
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || file.write(schema) != schema.size() ||
      !file.flush()) {
    error = {AiErrorKind::process_failure,
             errorText(AiErrorKind::process_failure, QStringLiteral("cannot create output schema"))};
    return false;
  }
  file.close();
  if (!QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
    error = {AiErrorKind::process_failure,
             errorText(AiErrorKind::process_failure, QStringLiteral("cannot secure output schema"))};
    return false;
  }
  return true;
}

void CodexCliProvider::readStandardOutput() {
  const auto data = process_.readAllStandardOutput();
  output_unfinished_line_bytes_ += data.size();
  output_line_count_ += static_cast<std::size_t>(data.count('\n'));
  if (standard_output_.size() + data.size() > kMaxOutputBytes ||
      output_unfinished_line_bytes_ > kMaxOutputLineBytes || output_line_count_ > kMaxOutputLines) {
    output_limit_exceeded_ = true;
    process_.kill();
    return;
  }
  if (data.contains('\n')) {
    output_unfinished_line_bytes_ = data.size() - data.lastIndexOf('\n') - 1;
  }
  standard_output_.append(data);
}

void CodexCliProvider::readStandardError() {
  const auto data = process_.readAllStandardError();
  error_unfinished_line_bytes_ += data.size();
  error_line_count_ += static_cast<std::size_t>(data.count('\n'));
  if (standard_error_.size() + data.size() > kMaxErrorBytes ||
      error_unfinished_line_bytes_ > kMaxOutputLineBytes || error_line_count_ > kMaxOutputLines) {
    process_.kill();
    process_error_seen_ = true;
    return;
  }
  if (data.contains('\n')) {
    error_unfinished_line_bytes_ = data.size() - data.lastIndexOf('\n') - 1;
  }
  standard_error_.append(data);
}

void CodexCliProvider::processStarted() {
  if (active_request_id_ == 0U || !prepared_request_.has_value()) {
    return;
  }
  emit progress(active_request_id_, AiProgress{AiProgress::Stage::generating,
                                                QStringLiteral("Generating logo construction plan")});
  const auto prompt = prepared_request_->prompt.toUtf8();
  if (process_.write(prompt) != prompt.size()) {
    process_error_seen_ = true;
    process_.kill();
    return;
  }
  process_.closeWriteChannel();
}

void CodexCliProvider::processError(const QProcess::ProcessError error) {
  if (error != QProcess::UnknownError) {
    process_error_seen_ = true;
  }
  if (error == QProcess::FailedToStart && active_request_id_ != 0U) {
    finishWithError({AiErrorKind::process_failure,
                     errorText(AiErrorKind::process_failure, QStringLiteral("cannot start Codex CLI"))});
  }
}

AiError CodexCliProvider::classifyFailure(const int exit_code, const QProcess::ExitStatus status) const {
  if (cancel_requested_) {
    return {AiErrorKind::cancelled, errorText(AiErrorKind::cancelled)};
  }
  if (timeout_requested_) {
    return {AiErrorKind::timeout, errorText(AiErrorKind::timeout)};
  }
  if (output_limit_exceeded_) {
    return {AiErrorKind::invalid_response, errorText(AiErrorKind::invalid_response, QStringLiteral("output is too large"))};
  }
  const auto stderr_text = QString::fromUtf8(standard_error_);
  if (containsAny(stderr_text, {u"login", u"authenticate", u"sign in", u"authentication"})) {
    return {AiErrorKind::authentication_required, errorText(AiErrorKind::authentication_required)};
  }
  if (containsAny(stderr_text, {u"rate limit", u"quota", u"too many requests"})) {
    return {AiErrorKind::rate_limited, errorText(AiErrorKind::rate_limited)};
  }
  const auto detail = QStringLiteral("exit code %1 (%2)").arg(exit_code).arg(static_cast<int>(status));
  return {AiErrorKind::process_failure, errorText(AiErrorKind::process_failure, detail)};
}

void CodexCliProvider::processFinished(const int exit_code, const QProcess::ExitStatus exit_status) {
  if (active_request_id_ == 0U) {
    return;
  }
  readStandardOutput();
  readStandardError();
  timeout_timer_.stop();
  cancel_grace_timer_.stop();
  const auto request_id = active_request_id_;
  const auto last_message_path = prepared_request_.has_value() ? prepared_request_->last_message_path : QString{};
  if (cancel_requested_ || timeout_requested_ || output_limit_exceeded_ || process_error_seen_ ||
      exit_status != QProcess::NormalExit || exit_code != 0) {
    finishWithError(classifyFailure(exit_code, exit_status));
    return;
  }
  emit progress(request_id, AiProgress{AiProgress::Stage::parsing,
                                       QStringLiteral("Parsing generated logo plan")});
  QFile file(last_message_path);
  if (!file.open(QIODevice::ReadOnly) ||
      static_cast<std::uint64_t>(file.size()) > kLogoConstructionPlanMaxBytes) {
    finishWithError({AiErrorKind::invalid_response,
                     errorText(AiErrorKind::invalid_response, QStringLiteral("final message is missing or too large"))});
    return;
  }
  const auto payload = file.readAll();
  QJsonParseError parse_error;
  QJsonDocument::fromJson(payload, &parse_error);
  if (parse_error.error != QJsonParseError::NoError) {
    finishWithError({AiErrorKind::invalid_response,
                     errorText(AiErrorKind::invalid_response, parse_error.errorString())});
    return;
  }
  const auto parsed = parseLogoConstructionPlan(
      std::string_view(payload.constData(), static_cast<std::size_t>(payload.size())));
  if (!parsed) {
    QString detail;
    if (!parsed.diagnostics.empty()) {
      detail = QString::fromStdString(parsed.diagnostics.front().path + QStringLiteral(": ").toStdString() +
                                      parsed.diagnostics.front().message);
    }
    finishWithError({AiErrorKind::invalid_plan, errorText(AiErrorKind::invalid_plan, detail)});
    return;
  }
  resetProcessState();
  emit succeeded(request_id, *parsed.plan);
}

void CodexCliProvider::timeout() {
  if (active_request_id_ == 0U) {
    return;
  }
  timeout_requested_ = true;
  process_.terminate();
  cancel_grace_timer_.start(kCancelGracePeriodMs);
}

void CodexCliProvider::killAfterCancelGracePeriod() {
  if (active_request_id_ != 0U && process_.state() != QProcess::NotRunning) {
    process_.kill();
  }
}

void CodexCliProvider::finishWithError(AiError error) {
  const auto request_id = active_request_id_;
  resetProcessState();
  emit failed(request_id, std::move(error));
}

void CodexCliProvider::resetProcessState() {
  timeout_timer_.stop();
  cancel_grace_timer_.stop();
  process_.closeWriteChannel();
  prepared_request_.reset();
  temporary_directory_.reset();
  active_request_id_ = 0U;
  cancel_requested_ = false;
  timeout_requested_ = false;
  output_limit_exceeded_ = false;
  process_error_seen_ = false;
  output_unfinished_line_bytes_ = 0;
  error_unfinished_line_bytes_ = 0;
  output_line_count_ = 0;
  error_line_count_ = 0;
  standard_output_.clear();
  standard_error_.clear();
}

void CodexCliProvider::stopProcess(const bool kill) {
  if (process_.state() == QProcess::NotRunning) {
    return;
  }
  if (kill) {
    process_.kill();
  } else {
    process_.terminate();
  }
}

}  // namespace signet::ai
