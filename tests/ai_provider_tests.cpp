// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ai/codex_cli_provider.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileDevice>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include <cassert>

namespace {

constexpr auto kValidPlan = R"JSON({
  "schema_version":1,
  "coordinate_system":{"unit":"logical","origin":"center","x_axis":"right","y_axis":"up","bounds":[-100,-100,100,100]},
  "nodes":[{"id":"circle","name":"Circle","kind":"primitive","primitive":{"type":"circle","radius":10},"transform":{"translation":{"x":0,"y":0},"rotation_degrees":0,"scale":{"x":1,"y":1}}}],
  "roots":["circle"]
})JSON";

QString writeFakeCodex(QTemporaryDir& directory) {
  const auto path = directory.filePath(QStringLiteral("fake-codex.sh"));
  QFile file(path);
  assert(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
  const QByteArray script =
      QByteArrayLiteral("#!/bin/sh\n"
                        "last=\"\"\n"
                        "image=\"\"\n"
                        "schema=\"\"\n"
                        "while [ \"$#\" -gt 0 ]; do\n"
                        "  case \"$1\" in\n"
                        "    --output-last-message) shift; last=\"$1\" ;;\n"
                        "    --output-schema) shift; schema=\"$1\" ;;\n"
                        "    --image) shift; image=\"$1\" ;;\n"
                        "  esac\n"
                        "  shift\n"
                        "done\n"
                        "input=$(cat)\n"
                        "printf '%s' \"$input\" > \"$(dirname \"$0\")/captured-input.txt\"\n"
                        "if [ -n \"$schema\" ] && [ -f \"$schema\" ]; then cp \"$schema\" \"$(dirname \"$0\")/captured-schema.json\"; fi\n"
                        "case \"$input\" in\n"
                        "  *timeout*) sleep 3 ;;\n"
                        "  *cancel*) sleep 3 ;;\n"
                        "  *auth*) echo \"please login to continue\" >&2; exit 7 ;;\n"
                        "  *rate*) echo \"rate limit reached\" >&2; exit 8 ;;\n"
                        "  *malformed*) printf '{' > \"$last\" ;;\n"
                        "  *invalid-plan*) printf '{}' > \"$last\" ;;\n"
                        "  *inspect*) test -n \"$schema\" && test -f \"$schema\" || exit 12; "
                        "test -n \"$image\" && test -f \"$image\" || exit 13; printf '%s' '") +
      QByteArray(kValidPlan) +
      QByteArrayLiteral("' > \"$last\" ;;\n"
                        "  *) printf '%s' '") +
      QByteArray(kValidPlan) +
      QByteArrayLiteral("' > \"$last\" ;;\n"
                        "esac\n");
  assert(file.write(script) == script.size());
  file.close();
  assert(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                       QFileDevice::ExeOwner));
  return path;
}

signet::ai::AiError errorFrom(const QSignalSpy& spy) {
  assert(spy.count() == 1);
  return spy.at(0).at(1).value<signet::ai::AiError>();
}

void testSuccessAndPromptContract() {
  QTemporaryDir directory;
  assert(directory.isValid());
  const auto image_path = directory.filePath(QStringLiteral("reference.png"));
  QImage reference(2, 2, QImage::Format_ARGB32);
  reference.fill(Qt::white);
  assert(reference.save(image_path, "PNG"));
  signet::ai::CodexCliProvider provider(writeFakeCodex(directory));
  QSignalSpy success(&provider, &signet::ai::AiProvider::succeeded);
  QSignalSpy failure(&provider, &signet::ai::AiProvider::failed);
  const auto id = provider.request({QStringLiteral("inspect"), {image_path}, 2000});
  QTRY_COMPARE_WITH_TIMEOUT(success.count(), 1, 2000);
  assert(failure.count() == 0);
  assert(success.at(0).at(0).toULongLong() == id);
  const auto plan = success.at(0).at(1).value<signet::ai::LogoConstructionPlan>();
  assert(plan.nodes.size() == 1);

  QFile captured_input(directory.filePath(QStringLiteral("captured-input.txt")));
  assert(captured_input.open(QIODevice::ReadOnly));
  const auto input = captured_input.readAll();
  assert(input.contains("You are Signet's geometric logo construction planner."));
  assert(input.contains("inspect"));

  QFile captured_schema(directory.filePath(QStringLiteral("captured-schema.json")));
  assert(captured_schema.open(QIODevice::ReadOnly));
  QJsonParseError parse_error;
  const auto schema_document = QJsonDocument::fromJson(captured_schema.readAll(), &parse_error);
  assert(parse_error.error == QJsonParseError::NoError);
  const auto schema = schema_document.object();
  const auto required = schema.value(QStringLiteral("required")).toArray();
  assert(required.contains(QStringLiteral("schema_version")));
  assert(required.contains(QStringLiteral("coordinate_system")));
  assert(required.contains(QStringLiteral("nodes")));
  assert(required.contains(QStringLiteral("roots")));
  const auto coordinate = schema.value(QStringLiteral("properties")).toObject()
                              .value(QStringLiteral("coordinate_system")).toObject();
  assert(coordinate.value(QStringLiteral("required")).toArray().contains(QStringLiteral("bounds")));
  const auto bounds_items = coordinate.value(QStringLiteral("properties")).toObject()
                                .value(QStringLiteral("bounds")).toObject()
                                .value(QStringLiteral("items")).toObject();
  assert(bounds_items.value(QStringLiteral("minimum")).toDouble() == -1000000.0);
  assert(bounds_items.value(QStringLiteral("maximum")).toDouble() == 1000000.0);
  const auto nodes = schema.value(QStringLiteral("properties")).toObject()
                         .value(QStringLiteral("nodes")).toObject();
  const auto alternatives = nodes.value(QStringLiteral("items")).toObject()
                                .value(QStringLiteral("oneOf")).toArray();
  assert(alternatives.size() == 3);
  const auto primitive_properties = alternatives.at(0).toObject()
                                       .value(QStringLiteral("properties")).toObject();
  assert(primitive_properties.contains(QStringLiteral("transform")));
  assert(primitive_properties.value(QStringLiteral("id")).toObject()
             .value(QStringLiteral("pattern")).toString() == QStringLiteral("^[A-Za-z0-9_-]+$"));
  assert(primitive_properties.value(QStringLiteral("name")).toObject()
             .value(QStringLiteral("pattern")).toString() == QStringLiteral("^[A-Za-z0-9 _.-]+$"));
  assert(alternatives.at(0).toObject().value(QStringLiteral("required")).toArray()
             .contains(QStringLiteral("transform")));
  const auto primitive_alternatives = primitive_properties.value(QStringLiteral("primitive"))
                                          .toObject().value(QStringLiteral("oneOf")).toArray();
  assert(primitive_alternatives.size() == 4);
  const auto circle_schema = primitive_alternatives.at(0).toObject()
                                 .value(QStringLiteral("properties")).toObject()
                                 .value(QStringLiteral("radius")).toObject();
  assert(circle_schema.value(QStringLiteral("exclusiveMinimum")).toDouble() == 0.0);
  assert(circle_schema.value(QStringLiteral("maximum")).toDouble() == 1000000.0);
  const auto arc_schema = primitive_alternatives.at(3).toObject()
                              .value(QStringLiteral("properties")).toObject();
  assert(arc_schema.value(QStringLiteral("sweep_degrees")).toObject()
             .value(QStringLiteral("anyOf")).toArray().size() == 2);
  const auto transform_schema = primitive_properties.value(QStringLiteral("transform")).toObject();
  assert(transform_schema.value(QStringLiteral("properties")).toObject()
             .value(QStringLiteral("scale")).toObject().value(QStringLiteral("$ref")) ==
         QStringLiteral("#/$defs/nonzero_point"));
  const auto symmetry_schema = alternatives.at(2).toObject()
                                   .value(QStringLiteral("properties")).toObject()
                                   .value(QStringLiteral("axis")).toObject()
                                   .value(QStringLiteral("properties")).toObject()
                                   .value(QStringLiteral("direction")).toObject();
  assert(symmetry_schema.value(QStringLiteral("allOf")).toArray().at(1).toObject()
             .value(QStringLiteral("anyOf")).toArray().size() == 2);
  assert(alternatives.at(1).toObject().value(QStringLiteral("properties")).toObject()
             .value(QStringLiteral("operation")).toObject().value(QStringLiteral("enum"))
             .toArray().contains(QStringLiteral("exclusive_or")));

  success.clear();
  (void)provider.request({QString{}, {image_path}, 2000});
  QTRY_COMPARE_WITH_TIMEOUT(success.count(), 1, 2000);
  QFile image_only_input(directory.filePath(QStringLiteral("captured-input.txt")));
  assert(image_only_input.open(QIODevice::ReadOnly));
  const auto image_only_prompt = image_only_input.readAll();
  assert(image_only_prompt.contains("You are Signet's geometric logo construction planner."));
  assert(!image_only_prompt.contains("inspect"));
}

void testTypedResponseAndProcessErrors() {
  QTemporaryDir directory;
  assert(directory.isValid());
  signet::ai::CodexCliProvider provider(writeFakeCodex(directory));
  QSignalSpy failure(&provider, &signet::ai::AiProvider::failed);

  (void)provider.request({QStringLiteral("malformed"), {}, 2000});
  QTRY_COMPARE_WITH_TIMEOUT(failure.count(), 1, 2000);
  assert(errorFrom(failure).kind == signet::ai::AiErrorKind::invalid_response);

  failure.clear();
  (void)provider.request({QStringLiteral("invalid-plan"), {}, 2000});
  QTRY_COMPARE_WITH_TIMEOUT(failure.count(), 1, 2000);
  assert(errorFrom(failure).kind == signet::ai::AiErrorKind::invalid_plan);

  failure.clear();
  (void)provider.request({QStringLiteral("auth"), {}, 2000});
  QTRY_COMPARE_WITH_TIMEOUT(failure.count(), 1, 2000);
  assert(errorFrom(failure).kind == signet::ai::AiErrorKind::authentication_required);

  failure.clear();
  (void)provider.request({QStringLiteral("rate"), {}, 2000});
  QTRY_COMPARE_WITH_TIMEOUT(failure.count(), 1, 2000);
  assert(errorFrom(failure).kind == signet::ai::AiErrorKind::rate_limited);
}

void testTimeoutCancelBusyAndValidation() {
  QTemporaryDir directory;
  assert(directory.isValid());
  signet::ai::CodexCliProvider provider(writeFakeCodex(directory));
  QSignalSpy failure(&provider, &signet::ai::AiProvider::failed);

  const auto timeout_id = provider.request({QStringLiteral("timeout"), {}, 40});
  const auto busy_id = provider.request({QStringLiteral("another"), {}, 2000});
  assert(timeout_id != busy_id);
  QTRY_COMPARE_WITH_TIMEOUT(failure.count(), 2, 2000);
  assert(failure.at(0).at(0).toULongLong() == busy_id ||
         failure.at(1).at(0).toULongLong() == busy_id);
  const auto first_error = failure.at(0).at(1).value<signet::ai::AiError>().kind;
  const auto second_error = failure.at(1).at(1).value<signet::ai::AiError>().kind;
  assert(first_error == signet::ai::AiErrorKind::busy ||
         second_error == signet::ai::AiErrorKind::busy);

  failure.clear();
  const auto cancel_id = provider.request({QStringLiteral("cancel"), {}, 2000});
  provider.cancel(cancel_id);
  QTRY_COMPARE_WITH_TIMEOUT(failure.count(), 1, 2000);
  assert(errorFrom(failure).kind == signet::ai::AiErrorKind::cancelled);

  failure.clear();
  (void)provider.request({QString{}, {}, 2000});
  QTRY_COMPARE_WITH_TIMEOUT(failure.count(), 1, 500);
  assert(errorFrom(failure).kind == signet::ai::AiErrorKind::invalid_input);
}

void testMissingBinaryAndInvalidImage() {
  signet::ai::CodexCliProvider provider(QStringLiteral("/definitely/missing/codex"));
  QSignalSpy failure(&provider, &signet::ai::AiProvider::failed);
  (void)provider.request({QStringLiteral("prompt"), {QStringLiteral("/missing/image.png")}, 2000});
  QTRY_COMPARE_WITH_TIMEOUT(failure.count(), 1, 500);
  assert(errorFrom(failure).kind == signet::ai::AiErrorKind::invalid_image);

  failure.clear();
  (void)provider.request({QStringLiteral("prompt"), {}, 2000});
  QTRY_COMPARE_WITH_TIMEOUT(failure.count(), 1, 500);
  assert(errorFrom(failure).kind == signet::ai::AiErrorKind::missing_binary);
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  testSuccessAndPromptContract();
  testTypedResponseAndProcessErrors();
  testTimeoutCancelBusyAndValidation();
  testMissingBinaryAndInvalidImage();
}
