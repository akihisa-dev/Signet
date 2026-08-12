// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ai/logo_construction_plan.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace signet::ai {

namespace {

using StringSet = std::set<std::string, std::less<>>;

void addDiagnostic(
    std::vector<PlanDiagnostic>& diagnostics,
    std::string path,
    std::string message) {
  diagnostics.push_back(PlanDiagnostic{std::move(path), std::move(message)});
}

bool hasOnlyKeys(
    const QJsonObject& object,
    const std::initializer_list<std::string_view> allowed,
    std::vector<PlanDiagnostic>& diagnostics,
    const std::string_view path) {
  StringSet allowed_set;
  for (const auto key : allowed) {
    allowed_set.emplace(key);
  }
  bool valid = true;
  for (const auto& key : object.keys()) {
    if (!allowed_set.contains(key.toStdString())) {
      addDiagnostic(diagnostics, std::string(path) + "." + key.toStdString(), "unknown field");
      valid = false;
    }
  }
  return valid;
}

const QJsonValue* valueAt(const QJsonObject& object, const QString& key) {
  const auto found = object.find(key);
  if (found == object.end()) {
    return nullptr;
  }
  static thread_local QJsonValue value;
  value = found.value();
  return &value;
}

bool requireObject(
    const QJsonValue* value,
    std::vector<PlanDiagnostic>& diagnostics,
    const std::string_view path) {
  if (value == nullptr || !value->isObject()) {
    addDiagnostic(diagnostics, std::string(path), "must be an object");
    return false;
  }
  return true;
}

bool requireString(
    const QJsonValue* value,
    std::string& output,
    std::vector<PlanDiagnostic>& diagnostics,
    const std::string_view path) {
  if (value == nullptr || !value->isString()) {
    addDiagnostic(diagnostics, std::string(path), "must be a string");
    return false;
  }
  output = value->toString().toStdString();
  return true;
}

bool requireNumber(
    const QJsonValue* value,
    double& output,
    std::vector<PlanDiagnostic>& diagnostics,
    const std::string_view path) {
  if (value == nullptr || !value->isDouble()) {
    addDiagnostic(diagnostics, std::string(path), "must be a finite number");
    return false;
  }
  output = value->toDouble(std::numeric_limits<double>::quiet_NaN());
  if (!std::isfinite(output)) {
    addDiagnostic(diagnostics, std::string(path), "must be finite");
    return false;
  }
  return true;
}

bool requireInteger(
    const QJsonValue* value,
    std::uint32_t& output,
    std::vector<PlanDiagnostic>& diagnostics,
    const std::string_view path) {
  double number = 0.0;
  if (!requireNumber(value, number, diagnostics, path)) {
    return false;
  }
  if (number < 0.0 || number > static_cast<double>(std::numeric_limits<std::uint32_t>::max()) ||
      std::floor(number) != number) {
    addDiagnostic(diagnostics, std::string(path), "must be an unsigned integer");
    return false;
  }
  output = static_cast<std::uint32_t>(number);
  return true;
}

bool requirePoint(
    const QJsonValue* value,
    core::Point& output,
    std::vector<PlanDiagnostic>& diagnostics,
    const std::string_view path) {
  if (!requireObject(value, diagnostics, path)) {
    return false;
  }
  const auto object = value->toObject();
  if (!hasOnlyKeys(object, {"x", "y"}, diagnostics, path)) {
    return false;
  }
  bool valid = true;
  valid = requireNumber(valueAt(object, QStringLiteral("x")), output.x, diagnostics,
                        std::string(path) + ".x") &&
          valid;
  valid = requireNumber(valueAt(object, QStringLiteral("y")), output.y, diagnostics,
                        std::string(path) + ".y") &&
          valid;
  return valid;
}

bool requireTransform(
    const QJsonValue* value,
    core::Transform& output,
    std::vector<PlanDiagnostic>& diagnostics,
    const std::string_view path) {
  if (value == nullptr) {
    addDiagnostic(diagnostics, std::string(path), "required field is missing");
    return false;
  }
  if (!requireObject(value, diagnostics, path)) {
    return false;
  }
  const auto object = value->toObject();
  if (!hasOnlyKeys(object, {"translation", "rotation_degrees", "scale"}, diagnostics, path)) {
    return false;
  }
  bool valid = true;
  valid = requirePoint(valueAt(object, QStringLiteral("translation")), output.translation,
                       diagnostics, std::string(path) + ".translation") &&
          valid;
  valid = requireNumber(valueAt(object, QStringLiteral("rotation_degrees")),
                        output.rotation_degrees, diagnostics,
                        std::string(path) + ".rotation_degrees") &&
          valid;
  valid = requirePoint(valueAt(object, QStringLiteral("scale")), output.scale, diagnostics,
                       std::string(path) + ".scale") &&
          valid;
  return valid;
}

bool parsePrimitive(
    const QJsonValue* value,
    PlanPrimitive& output,
    std::vector<PlanDiagnostic>& diagnostics,
    const std::string_view path) {
  if (!requireObject(value, diagnostics, path)) {
    return false;
  }
  const auto object = value->toObject();
  std::string type;
  if (!requireString(valueAt(object, QStringLiteral("type")), type, diagnostics,
                     std::string(path) + ".type")) {
    return false;
  }

  bool valid = true;
  if (type == "circle") {
    valid = hasOnlyKeys(object, {"type", "radius"}, diagnostics, path) && valid;
    core::Circle circle;
    valid = requireNumber(valueAt(object, QStringLiteral("radius")), circle.radius, diagnostics,
                          std::string(path) + ".radius") &&
            valid;
    output.primitive = circle;
  } else if (type == "rectangle") {
    valid = hasOnlyKeys(object, {"type", "width", "height"}, diagnostics, path) && valid;
    core::Rectangle rectangle;
    valid = requireNumber(valueAt(object, QStringLiteral("width")), rectangle.width, diagnostics,
                          std::string(path) + ".width") &&
            valid;
    valid = requireNumber(valueAt(object, QStringLiteral("height")), rectangle.height,
                          diagnostics, std::string(path) + ".height") &&
            valid;
    output.primitive = rectangle;
  } else if (type == "golden_rectangle") {
    valid = hasOnlyKeys(object, {"type", "short_side"}, diagnostics, path) && valid;
    core::GoldenRectangle rectangle;
    valid = requireNumber(valueAt(object, QStringLiteral("short_side")), rectangle.short_side,
                          diagnostics, std::string(path) + ".short_side") &&
            valid;
    output.primitive = rectangle;
  } else if (type == "arc") {
    valid = hasOnlyKeys(object, {"type", "radius", "start_degrees", "sweep_degrees"},
                        diagnostics, path) &&
            valid;
    core::Arc arc;
    valid = requireNumber(valueAt(object, QStringLiteral("radius")), arc.radius, diagnostics,
                          std::string(path) + ".radius") &&
            valid;
    valid = requireNumber(valueAt(object, QStringLiteral("start_degrees")), arc.start_degrees,
                          diagnostics, std::string(path) + ".start_degrees") &&
            valid;
    valid = requireNumber(valueAt(object, QStringLiteral("sweep_degrees")), arc.sweep_degrees,
                          diagnostics, std::string(path) + ".sweep_degrees") &&
            valid;
    output.primitive = arc;
  } else {
    addDiagnostic(diagnostics, std::string(path) + ".type", "unsupported primitive type");
    return false;
  }
  return valid;
}

bool parseNode(
    const QJsonValue& value,
    PlanNode& output,
    std::vector<PlanDiagnostic>& diagnostics,
    const std::string_view path) {
  if (!value.isObject()) {
    addDiagnostic(diagnostics, std::string(path), "must be an object");
    return false;
  }
  const auto object = value.toObject();
  std::string kind;
  if (!requireString(valueAt(object, QStringLiteral("kind")), kind, diagnostics,
                     std::string(path) + ".kind")) {
    return false;
  }

  const bool primitive_kind = kind == "primitive";
  const bool boolean_kind = kind == "boolean";
  const bool symmetry_kind = kind == "symmetry";
  if (!primitive_kind && !boolean_kind && !symmetry_kind) {
    addDiagnostic(diagnostics, std::string(path) + ".kind", "unsupported node kind");
    return false;
  }
  if ((primitive_kind && !hasOnlyKeys(object, {"id", "name", "kind", "primitive", "transform"},
                                      diagnostics, path)) ||
      (boolean_kind && !hasOnlyKeys(object, {"id", "name", "kind", "operation", "left", "right"},
                                     diagnostics, path)) ||
      (symmetry_kind && !hasOnlyKeys(object, {"id", "name", "kind", "input", "axis"},
                                      diagnostics, path))) {
    return false;
  }

  bool valid = requireString(valueAt(object, QStringLiteral("id")), output.id, diagnostics,
                             std::string(path) + ".id");
  valid = requireString(valueAt(object, QStringLiteral("name")), output.name, diagnostics,
                        std::string(path) + ".name") &&
          valid;
  if (primitive_kind) {
    PlanPrimitive primitive;
    valid = parsePrimitive(valueAt(object, QStringLiteral("primitive")), primitive, diagnostics,
                           std::string(path) + ".primitive") &&
            valid;
    valid = requireTransform(valueAt(object, QStringLiteral("transform")), primitive.transform,
                             diagnostics, std::string(path) + ".transform") &&
            valid;
    output.definition = std::move(primitive);
  } else if (boolean_kind) {
    PlanBoolean boolean;
    std::string operation;
    valid = requireString(valueAt(object, QStringLiteral("operation")), operation, diagnostics,
                          std::string(path) + ".operation") &&
            valid;
    valid = requireString(valueAt(object, QStringLiteral("left")), boolean.left, diagnostics,
                          std::string(path) + ".left") &&
            valid;
    valid = requireString(valueAt(object, QStringLiteral("right")), boolean.right, diagnostics,
                          std::string(path) + ".right") &&
            valid;
    if (operation == "unite") {
      boolean.operation = core::BooleanOperation::unite;
    } else if (operation == "intersect") {
      boolean.operation = core::BooleanOperation::intersect;
    } else if (operation == "subtract") {
      boolean.operation = core::BooleanOperation::subtract;
    } else if (operation == "exclusive_or") {
      boolean.operation = core::BooleanOperation::exclusive_or;
    } else {
      addDiagnostic(diagnostics, std::string(path) + ".operation", "unsupported Boolean operation");
      valid = false;
    }
    output.definition = std::move(boolean);
  } else {
    PlanSymmetry symmetry;
    valid = requireString(valueAt(object, QStringLiteral("input")), symmetry.input, diagnostics,
                          std::string(path) + ".input") &&
            valid;
    valid = requireObject(valueAt(object, QStringLiteral("axis")), diagnostics,
                          std::string(path) + ".axis") &&
            valid;
    if (const auto* axis = valueAt(object, QStringLiteral("axis"));
        axis != nullptr && axis->isObject()) {
      const auto axis_object = axis->toObject();
      if (!hasOnlyKeys(axis_object, {"origin", "direction"}, diagnostics,
                       std::string(path) + ".axis")) {
        valid = false;
      }
      valid = requirePoint(valueAt(axis_object, QStringLiteral("origin")), symmetry.axis.origin,
                           diagnostics, std::string(path) + ".axis.origin") &&
              valid;
      valid = requirePoint(valueAt(axis_object, QStringLiteral("direction")),
                           symmetry.axis.direction, diagnostics,
                           std::string(path) + ".axis.direction") &&
              valid;
    }
    output.definition = std::move(symmetry);
  }
  return valid;
}

bool validIdentifier(const std::string& identifier) {
  if (identifier.empty() || identifier.size() > 64U) {
    return false;
  }
  return std::ranges::all_of(identifier, [](const char value) {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
           (value >= '0' && value <= '9') || value == '_' || value == '-';
  });
}

bool finiteAndBounded(const double value) {
  return std::isfinite(value) && std::abs(value) <= kLogoConstructionPlanMaxCoordinate;
}

bool primitiveIsClosed(const PlanPrimitive& primitive) {
  if (const auto* arc = std::get_if<core::Arc>(&primitive.primitive); arc != nullptr) {
    return std::abs(arc->sweep_degrees) == 360.0;
  }
  return true;
}

}  // namespace

std::vector<PlanDiagnostic> validateLogoConstructionPlan(const LogoConstructionPlan& plan) {
  std::vector<PlanDiagnostic> diagnostics;
  if (plan.schema_version != kLogoConstructionPlanSchemaVersion) {
    addDiagnostic(diagnostics, "schema_version", "unsupported schema version");
  }
  const auto& coordinate = plan.coordinate_system;
  if (coordinate.unit != "logical" || coordinate.origin != "center" ||
      coordinate.x_axis != "right" || coordinate.y_axis != "up") {
    addDiagnostic(diagnostics, "coordinate_system", "unsupported coordinate system");
  }
  for (const auto [path, value] : {std::pair{"min_x", coordinate.min_x},
                                   std::pair{"min_y", coordinate.min_y},
                                   std::pair{"max_x", coordinate.max_x},
                                   std::pair{"max_y", coordinate.max_y}}) {
    if (!finiteAndBounded(value)) {
      addDiagnostic(diagnostics, std::string("coordinate_system.") + path,
                    "coordinate must be finite and bounded");
    }
  }
  if (!(coordinate.min_x < coordinate.max_x) || !(coordinate.min_y < coordinate.max_y)) {
    addDiagnostic(diagnostics, "coordinate_system.bounds", "bounds must have positive extent");
  }
  if (plan.nodes.empty() || plan.nodes.size() > kLogoConstructionPlanMaxNodes) {
    addDiagnostic(diagnostics, "nodes", "node count is outside the supported range");
  }

  std::unordered_map<std::string, std::size_t> indices;
  indices.reserve(plan.nodes.size());
  for (std::size_t index = 0; index < plan.nodes.size(); ++index) {
    const auto& node = plan.nodes[index];
    const auto path = "nodes[" + std::to_string(index) + "]";
    if (!validIdentifier(node.id)) {
      addDiagnostic(diagnostics, path + ".id", "identifier must be 1..64 ASCII letters, digits, '_' or '-'");
    }
    if (node.name.empty() || node.name.size() > 128U) {
      addDiagnostic(diagnostics, path + ".name", "name must contain 1..128 characters");
    }
    if (!indices.emplace(node.id, index).second) {
      addDiagnostic(diagnostics, path + ".id", "duplicate node identifier");
    }
    std::visit(
        [&diagnostics, &path](const auto& definition) {
          using Definition = std::decay_t<decltype(definition)>;
          if constexpr (std::is_same_v<Definition, PlanPrimitive>) {
            const auto check = [&diagnostics, &path](const double value, const char* name) {
              if (!finiteAndBounded(value)) {
                addDiagnostic(diagnostics, path + ".primitive." + name,
                              "value must be finite and bounded");
              }
            };
            std::visit(
                [&check, &diagnostics, &path](const auto& primitive) {
                  using Primitive = std::decay_t<decltype(primitive)>;
                  if constexpr (std::is_same_v<Primitive, core::Circle>) {
                    check(primitive.radius, "radius");
                    if (primitive.radius <= 0.0) {
                      addDiagnostic(diagnostics, path + ".primitive.radius",
                                    "radius must be positive");
                    }
                  } else if constexpr (std::is_same_v<Primitive, core::Rectangle>) {
                    check(primitive.width, "width");
                    check(primitive.height, "height");
                    if (primitive.width <= 0.0 || primitive.height <= 0.0) {
                      addDiagnostic(diagnostics, path + ".primitive", "dimensions must be positive");
                    }
                  } else if constexpr (std::is_same_v<Primitive, core::GoldenRectangle>) {
                    check(primitive.short_side, "short_side");
                    if (primitive.short_side <= 0.0) {
                      addDiagnostic(diagnostics, path + ".primitive.short_side",
                                    "short_side must be positive");
                    }
                  } else {
                    check(primitive.radius, "radius");
                    check(primitive.start_degrees, "start_degrees");
                    check(primitive.sweep_degrees, "sweep_degrees");
                    if (primitive.radius <= 0.0 || primitive.sweep_degrees == 0.0 ||
                        std::abs(primitive.sweep_degrees) > 360.0) {
                      addDiagnostic(diagnostics, path + ".primitive", "arc values are invalid");
                    }
                  }
                },
                definition.primitive);
            check(definition.transform.translation.x, "transform.translation.x");
            check(definition.transform.translation.y, "transform.translation.y");
            check(definition.transform.rotation_degrees, "transform.rotation_degrees");
            check(definition.transform.scale.x, "transform.scale.x");
            check(definition.transform.scale.y, "transform.scale.y");
            if (definition.transform.scale.x == 0.0 || definition.transform.scale.y == 0.0) {
              addDiagnostic(diagnostics, path + ".transform.scale", "scale must be non-zero");
            }
          } else if constexpr (std::is_same_v<Definition, PlanSymmetry>) {
            const auto check = [&diagnostics, &path](const double value, const char* name) {
              if (!finiteAndBounded(value)) {
                addDiagnostic(diagnostics, path + ".axis." + name,
                              "value must be finite and bounded");
              }
            };
            check(definition.axis.origin.x, "origin.x");
            check(definition.axis.origin.y, "origin.y");
            check(definition.axis.direction.x, "direction.x");
            check(definition.axis.direction.y, "direction.y");
            if (definition.axis.direction.x == 0.0 && definition.axis.direction.y == 0.0) {
              addDiagnostic(diagnostics, path + ".axis.direction", "direction must be non-zero");
            }
          } else if constexpr (std::is_same_v<Definition, PlanBoolean>) {
            switch (definition.operation) {
              case core::BooleanOperation::unite:
              case core::BooleanOperation::intersect:
              case core::BooleanOperation::subtract:
              case core::BooleanOperation::exclusive_or:
                break;
              default:
                addDiagnostic(diagnostics, path + ".operation", "unsupported Boolean operation");
                break;
            }
          }
        },
        node.definition);
  }

  if (plan.roots.empty()) {
    addDiagnostic(diagnostics, "roots", "at least one root is required");
  }
  std::unordered_set<std::string> roots;
  roots.reserve(plan.roots.size());
  for (std::size_t index = 0; index < plan.roots.size(); ++index) {
    const auto& root = plan.roots[index];
    if (!roots.insert(root).second) {
      addDiagnostic(diagnostics, "roots[" + std::to_string(index) + "]", "duplicate root");
    }
    if (!indices.contains(root)) {
      addDiagnostic(diagnostics, "roots[" + std::to_string(index) + "]", "dangling root reference");
    }
  }

  std::vector<std::vector<std::size_t>> edges(plan.nodes.size());
  for (std::size_t index = 0; index < plan.nodes.size(); ++index) {
    const auto path = "nodes[" + std::to_string(index) + "]";
    std::visit(
        [&edges, &indices, &diagnostics, &path, index](const auto& definition) {
          using Definition = std::decay_t<decltype(definition)>;
          auto addReference = [&](const std::string& reference, const char* field) {
            const auto found = indices.find(reference);
            if (found == indices.end()) {
              addDiagnostic(diagnostics, path + "." + field, "dangling node reference");
            } else {
              edges[index].push_back(found->second);
            }
          };
          if constexpr (std::is_same_v<Definition, PlanBoolean>) {
            if (definition.left == definition.right) {
              addDiagnostic(diagnostics, path, "Boolean operands must be distinct");
            }
            addReference(definition.left, "left");
            addReference(definition.right, "right");
          } else if constexpr (std::is_same_v<Definition, PlanSymmetry>) {
            addReference(definition.input, "input");
          }
        },
        plan.nodes[index].definition);
  }

  enum class Visit : std::uint8_t { unseen, visiting, done };
  std::vector<Visit> visits(plan.nodes.size(), Visit::unseen);
  std::vector<std::size_t> depths(plan.nodes.size(), 0U);
  std::function<std::size_t(std::size_t)> visit = [&](const std::size_t index) {
    if (visits[index] == Visit::visiting) {
      addDiagnostic(diagnostics, "nodes[" + std::to_string(index) + "]", "node graph contains a cycle");
      return std::size_t{0};
    }
    if (visits[index] == Visit::done) {
      return depths[index];
    }
    visits[index] = Visit::visiting;
    std::size_t depth = 1U;
    for (const auto child : edges[index]) {
      const auto child_depth = visit(child);
      if (child_depth != 0U) {
        depth = std::max(depth, child_depth + 1U);
      }
    }
    visits[index] = Visit::done;
    depths[index] = depth;
    if (depth > kLogoConstructionPlanMaxDepth) {
      addDiagnostic(diagnostics, "nodes[" + std::to_string(index) + "]", "DAG depth exceeds limit");
    }
    return depth;
  };
  for (std::size_t index = 0; index < plan.nodes.size(); ++index) {
    visit(index);
  }

  std::function<bool(const std::string&, std::unordered_set<std::string>&)> closed =
      [&](const std::string& id, std::unordered_set<std::string>& active) {
        const auto found = indices.find(id);
        if (found == indices.end() || !active.insert(id).second) {
          return false;
        }
        const auto& definition = plan.nodes[found->second].definition;
        bool result = std::visit(
            [&](const auto& value) {
              using Definition = std::decay_t<decltype(value)>;
              if constexpr (std::is_same_v<Definition, PlanPrimitive>) {
                return primitiveIsClosed(value);
              } else if constexpr (std::is_same_v<Definition, PlanBoolean>) {
                return closed(value.left, active) && closed(value.right, active);
              } else {
                return closed(value.input, active);
              }
            },
            definition);
        active.erase(id);
        return result;
      };
  for (std::size_t index = 0; index < plan.nodes.size(); ++index) {
    if (const auto* boolean = std::get_if<PlanBoolean>(&plan.nodes[index].definition);
        boolean != nullptr) {
      std::unordered_set<std::string> active;
      if (!closed(boolean->left, active) || !closed(boolean->right, active)) {
        addDiagnostic(diagnostics, "nodes[" + std::to_string(index) + "]",
                      "Boolean operands must resolve to closed primitives");
      }
    }
    if (const auto* symmetry = std::get_if<PlanSymmetry>(&plan.nodes[index].definition);
        symmetry != nullptr) {
      const auto found = indices.find(symmetry->input);
      if (found != indices.end() && std::holds_alternative<PlanBoolean>(
                                        plan.nodes[found->second].definition)) {
        addDiagnostic(diagnostics, "nodes[" + std::to_string(index) + "]",
                      "Symmetry of a Boolean result is unsupported");
      }
    }
  }
  return diagnostics;
}

PlanParseResult parseLogoConstructionPlan(const std::string_view json) {
  PlanParseResult result;
  if (json.empty() || json.size() > kLogoConstructionPlanMaxBytes) {
    addDiagnostic(result.diagnostics, "$", "JSON size is outside the supported range");
    return result;
  }
  const QByteArray bytes(json.data(), static_cast<qsizetype>(json.size()));
  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(bytes, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    addDiagnostic(result.diagnostics, "$", "invalid JSON object");
    return result;
  }

  const auto object = document.object();
  if (!hasOnlyKeys(object, {"schema_version", "coordinate_system", "nodes", "roots"},
                   result.diagnostics, "$")) {
    return result;
  }
  LogoConstructionPlan plan;
  bool valid = requireInteger(valueAt(object, QStringLiteral("schema_version")),
                              plan.schema_version, result.diagnostics, "$.schema_version");
  const auto* coordinate_value = valueAt(object, QStringLiteral("coordinate_system"));
  valid = requireObject(coordinate_value, result.diagnostics, "$.coordinate_system") && valid;
  if (coordinate_value != nullptr && coordinate_value->isObject()) {
    const auto coordinate = coordinate_value->toObject();
    valid = hasOnlyKeys(coordinate, {"unit", "origin", "x_axis", "y_axis", "bounds"},
                        result.diagnostics, "$.coordinate_system") &&
            valid;
    valid = requireString(valueAt(coordinate, QStringLiteral("unit")), plan.coordinate_system.unit,
                          result.diagnostics, "$.coordinate_system.unit") &&
            valid;
    valid = requireString(valueAt(coordinate, QStringLiteral("origin")),
                          plan.coordinate_system.origin, result.diagnostics,
                          "$.coordinate_system.origin") &&
            valid;
    valid = requireString(valueAt(coordinate, QStringLiteral("x_axis")),
                          plan.coordinate_system.x_axis, result.diagnostics,
                          "$.coordinate_system.x_axis") &&
            valid;
    valid = requireString(valueAt(coordinate, QStringLiteral("y_axis")),
                          plan.coordinate_system.y_axis, result.diagnostics,
                          "$.coordinate_system.y_axis") &&
            valid;
    const auto* bounds = valueAt(coordinate, QStringLiteral("bounds"));
    if (bounds == nullptr || !bounds->isArray() || bounds->toArray().size() != 4) {
      addDiagnostic(result.diagnostics, "$.coordinate_system.bounds", "must contain four numbers");
      valid = false;
    } else {
      const auto values = bounds->toArray();
      const auto min_x = values.at(0);
      const auto min_y = values.at(1);
      const auto max_x = values.at(2);
      const auto max_y = values.at(3);
      valid = requireNumber(&min_x, plan.coordinate_system.min_x, result.diagnostics,
                            "$.coordinate_system.bounds[0]") &&
              valid;
      valid = requireNumber(&min_y, plan.coordinate_system.min_y, result.diagnostics,
                            "$.coordinate_system.bounds[1]") &&
              valid;
      valid = requireNumber(&max_x, plan.coordinate_system.max_x, result.diagnostics,
                            "$.coordinate_system.bounds[2]") &&
              valid;
      valid = requireNumber(&max_y, plan.coordinate_system.max_y, result.diagnostics,
                            "$.coordinate_system.bounds[3]") &&
              valid;
    }
  }

  const auto* nodes = valueAt(object, QStringLiteral("nodes"));
  if (nodes == nullptr || !nodes->isArray()) {
    addDiagnostic(result.diagnostics, "$.nodes", "must be an array");
    valid = false;
  } else {
    const auto values = nodes->toArray();
    if (values.isEmpty() || values.size() > static_cast<qsizetype>(kLogoConstructionPlanMaxNodes)) {
      addDiagnostic(result.diagnostics, "$.nodes", "node count is outside the supported range");
      valid = false;
    }
    plan.nodes.reserve(static_cast<std::size_t>(values.size()));
    for (qsizetype index = 0; index < values.size(); ++index) {
      PlanNode node;
      const bool node_valid = parseNode(values[index], node, result.diagnostics,
                                        "$.nodes[" + std::to_string(index) + "]");
      valid = node_valid && valid;
      plan.nodes.push_back(std::move(node));
    }
  }

  const auto* roots = valueAt(object, QStringLiteral("roots"));
  if (roots == nullptr || !roots->isArray()) {
    addDiagnostic(result.diagnostics, "$.roots", "must be an array");
    valid = false;
  } else {
    const auto values = roots->toArray();
    plan.roots.reserve(static_cast<std::size_t>(values.size()));
    for (qsizetype index = 0; index < values.size(); ++index) {
      std::string root;
      const auto value = values.at(index);
      valid = requireString(&value, root, result.diagnostics,
                            "$.roots[" + std::to_string(index) + "]") &&
              valid;
      plan.roots.push_back(std::move(root));
    }
  }

  const auto semantic_diagnostics = validateLogoConstructionPlan(plan);
  result.diagnostics.insert(result.diagnostics.end(), semantic_diagnostics.begin(),
                            semantic_diagnostics.end());
  if (valid && result.diagnostics.empty()) {
    result.plan = std::move(plan);
  }
  return result;
}

}  // namespace signet::ai
