// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ui/canvas_view.h"

#include "geometry/snap.h"

#include <QCoreApplication>
#include <QFocusEvent>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QLineF>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPalette>
#include <QPen>
#include <QPolygonF>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <numbers>
#include <ranges>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace signet::ui {

namespace {

constexpr double kHitTolerancePixels = 8.0;
constexpr double kGeometryEpsilon = 1.0e-9;
constexpr double kGuideCenterMarkHalfSizePixels = 6.0;
constexpr double kGuideLabelGapPixels = 8.0;
constexpr int kGuideFontPixelSize = 12;
// These are logical canvas pixels.  They are passed to the pure interaction
// layout API and deliberately never scaled by zoom or device pixel ratio.
constexpr double kSelectionHandleSizeLogicalPx = 14.0;
constexpr double kSelectionRotateOffsetLogicalPx = 9.0;
constexpr double kSelectionHitToleranceLogicalPx = 8.0;
constexpr double kSnapToleranceLogicalPx = 8.0;
constexpr double kSnapGridSpacing = 10.0;
constexpr double kPi = std::numbers::pi_v<double>;

QColor selectionAccent(const QPalette& palette) {
  const QColor highlight = palette.color(QPalette::Highlight);
  if (highlight != palette.color(QPalette::Base)) {
    return highlight;
  }
  return palette.color(QPalette::Link);
}

QColor splitAccent(const QPalette& palette) {
  const QColor link = palette.color(QPalette::Link);
  if (link != palette.color(QPalette::Base)) {
    return link;
  }
  return palette.color(QPalette::Highlight);
}

QColor guideAccent(const QPalette& palette, const SnapGuideKind kind) {
  const QColor base = palette.color(QPalette::Base);
  const QColor candidate = kind == SnapGuideKind::center
                               ? palette.color(QPalette::Link)
                               : palette.color(QPalette::Highlight);
  return candidate == base ? palette.color(QPalette::Text) : candidate;
}

struct ArcGeometry final {
  QPointF center;
  double radius{};
  double start_angle{};
  double sweep_angle{};
};

double positiveAngleDelta(const double from, const double to) noexcept {
  double delta = std::fmod(to - from, 2.0 * kPi);
  if (delta < 0.0) {
    delta += 2.0 * kPi;
  }
  return delta;
}

std::optional<ArcGeometry> arcGeometry(const geometry::ArcInput& arc) {
  const QPointF source(arc.source_x, arc.source_y);
  const QPointF interior(arc.interior_x, arc.interior_y);
  const QPointF target(arc.target_x, arc.target_y);
  const double determinant =
      2.0 * (source.x() * (interior.y() - target.y()) +
             interior.x() * (target.y() - source.y()) +
             target.x() * (source.y() - interior.y()));
  if (!std::isfinite(determinant) || std::abs(determinant) <= kGeometryEpsilon) {
    return std::nullopt;
  }

  const double source_squared = source.x() * source.x() + source.y() * source.y();
  const double interior_squared = interior.x() * interior.x() + interior.y() * interior.y();
  const double target_squared = target.x() * target.x() + target.y() * target.y();
  const QPointF center(
      (source_squared * (interior.y() - target.y()) +
       interior_squared * (target.y() - source.y()) +
       target_squared * (source.y() - interior.y())) /
          determinant,
      (source_squared * (target.x() - interior.x()) +
       interior_squared * (source.x() - target.x()) +
       target_squared * (interior.x() - source.x())) /
          determinant);
  const double radius = std::hypot(source.x() - center.x(), source.y() - center.y());
  if (!std::isfinite(center.x()) || !std::isfinite(center.y()) ||
      !std::isfinite(radius) || radius <= kGeometryEpsilon) {
    return std::nullopt;
  }

  const double start_angle = std::atan2(source.y() - center.y(), source.x() - center.x());
  const double interior_angle =
      std::atan2(interior.y() - center.y(), interior.x() - center.x());
  const double target_angle = std::atan2(target.y() - center.y(), target.x() - center.x());
  const double counterclockwise_sweep = positiveAngleDelta(start_angle, target_angle);
  const double counterclockwise_interior = positiveAngleDelta(start_angle, interior_angle);
  const bool counterclockwise =
      counterclockwise_interior <= counterclockwise_sweep + kGeometryEpsilon;
  const double sweep_angle = counterclockwise
                                 ? counterclockwise_sweep
                                 : counterclockwise_sweep - 2.0 * kPi;
  if (!std::isfinite(sweep_angle) || std::abs(sweep_angle) <= kGeometryEpsilon) {
    return std::nullopt;
  }
  return ArcGeometry{center, radius, start_angle, sweep_angle};
}

QPointF pointOnArc(const ArcGeometry& arc, const double fraction) {
  const double angle = arc.start_angle + arc.sweep_angle * fraction;
  return arc.center + QPointF(arc.radius * std::cos(angle), arc.radius * std::sin(angle));
}

double distanceSquaredToSegment(const QPointF point, const QPointF start, const QPointF end) {
  const QPointF edge = end - start;
  const double edge_length_squared = QPointF::dotProduct(edge, edge);
  if (edge_length_squared <= kGeometryEpsilon) {
    return QPointF::dotProduct(point - start, point - start);
  }
  const double projection =
      std::clamp(QPointF::dotProduct(point - start, edge) / edge_length_squared, 0.0, 1.0);
  const QPointF closest = start + projection * edge;
  return QPointF::dotProduct(point - closest, point - closest);
}

double distanceToArc(const QPointF point, const geometry::ArcInput& source_arc) {
  const auto arc = arcGeometry(source_arc);
  if (!arc.has_value()) {
    return std::numeric_limits<double>::infinity();
  }

  const double point_angle = std::atan2(point.y() - arc->center.y(), point.x() - arc->center.x());
  const double along_arc = arc->sweep_angle > 0.0
                               ? positiveAngleDelta(arc->start_angle, point_angle)
                               : positiveAngleDelta(point_angle, arc->start_angle);
  const double sweep = std::abs(arc->sweep_angle);
  if (along_arc <= sweep + kGeometryEpsilon) {
    return std::abs(std::hypot(point.x() - arc->center.x(), point.y() - arc->center.y()) -
                    arc->radius);
  }
  const QPointF start = pointOnArc(*arc, 0.0);
  const QPointF end = pointOnArc(*arc, 1.0);
  return std::min(std::hypot(point.x() - start.x(), point.y() - start.y()),
                  std::hypot(point.x() - end.x(), point.y() - end.y()));
}

void drawArc(
    QPainter& painter,
    const geometry::ArcInput& source_arc,
    const CanvasView& canvas,
    const QPointF offset) {
  const auto arc = arcGeometry(source_arc);
  if (!arc.has_value()) {
    painter.drawLine(
        canvas.documentToView(QPointF(source_arc.source_x, source_arc.source_y) + offset),
        canvas.documentToView(QPointF(source_arc.target_x, source_arc.target_y) + offset));
    return;
  }
  const int segment_count = std::clamp(
      static_cast<int>(std::ceil(std::abs(arc->sweep_angle) * 180.0 / kPi / 6.0)), 8, 720);
  QPainterPath path;
  path.moveTo(canvas.documentToView(pointOnArc(*arc, 0.0) + offset));
  for (int index = 1; index <= segment_count; ++index) {
    path.lineTo(canvas.documentToView(
        pointOnArc(*arc, static_cast<double>(index) / static_cast<double>(segment_count)) +
        offset));
  }
  painter.drawPath(path);
}

void drawPlacementPreview(
    QPainter& painter,
    const interaction::PrimitivePlacement& placement,
    const CanvasView& canvas) {
  const auto transformPoint = [&placement](const core::Point point) {
    const double radians = placement.transform.rotation_degrees * kPi / 180.0;
    const double x = point.x * placement.transform.scale.x;
    const double y = point.y * placement.transform.scale.y;
    return core::Point{
        placement.transform.translation.x + x * std::cos(radians) - y * std::sin(radians),
        placement.transform.translation.y + x * std::sin(radians) + y * std::cos(radians)};
  };
  std::visit(
      [&painter, &canvas, &placement, &transformPoint](const auto& primitive) {
        using Primitive = std::decay_t<decltype(primitive)>;
        if constexpr (std::is_same_v<Primitive, core::Circle>) {
          const auto center = transformPoint({});
          const double radius_x = std::abs(primitive.radius * placement.transform.scale.x) *
                                  canvas.zoom();
          const double radius_y = std::abs(primitive.radius * placement.transform.scale.y) *
                                  canvas.zoom();
          painter.drawEllipse(
              canvas.documentToView(QPointF(center.x, center.y)), radius_x, radius_y);
        } else if constexpr (std::is_same_v<Primitive, core::Rectangle>) {
          const std::array<core::Point, 4> corners{
              transformPoint({-primitive.width / 2.0, -primitive.height / 2.0}),
              transformPoint({primitive.width / 2.0, -primitive.height / 2.0}),
              transformPoint({primitive.width / 2.0, primitive.height / 2.0}),
              transformPoint({-primitive.width / 2.0, primitive.height / 2.0})};
          QPolygonF polygon;
          for (const auto corner : corners) {
            polygon << canvas.documentToView(QPointF(corner.x, corner.y));
          }
          painter.drawPolygon(polygon);
        } else if constexpr (std::is_same_v<Primitive, core::GoldenRectangle>) {
          const double half_width = primitive.longSide() / 2.0;
          const double half_height = primitive.short_side / 2.0;
          const std::array<core::Point, 4> corners{
              transformPoint({-half_width, -half_height}),
              transformPoint({half_width, -half_height}),
              transformPoint({half_width, half_height}),
              transformPoint({-half_width, half_height})};
          QPolygonF polygon;
          for (const auto corner : corners) {
            polygon << canvas.documentToView(QPointF(corner.x, corner.y));
          }
          painter.drawPolygon(polygon);
        } else if constexpr (std::is_same_v<Primitive, core::Arc>) {
          QPainterPath path;
          const int segments = std::clamp(
              static_cast<int>(std::ceil(std::abs(primitive.sweep_degrees) / 6.0)), 8, 720);
          for (int index = 0; index <= segments; ++index) {
            const double fraction = static_cast<double>(index) / static_cast<double>(segments);
            const double degrees = primitive.start_degrees + primitive.sweep_degrees * fraction;
            const double radians = degrees * kPi / 180.0;
            const auto point = transformPoint(
                {primitive.radius * std::cos(radians), primitive.radius * std::sin(radians)});
            if (index == 0) {
              path.moveTo(canvas.documentToView(QPointF(point.x, point.y)));
            } else {
              path.lineTo(canvas.documentToView(QPointF(point.x, point.y)));
            }
          }
          painter.drawPath(path);
        }
      },
      placement.primitive);
}

std::optional<core::Point> transformedPoint(
    const core::Point point,
    const core::Transform& transform) {
  const double radians = transform.rotation_degrees * kPi / 180.0;
  const double scaled_x = point.x * transform.scale.x;
  const double scaled_y = point.y * transform.scale.y;
  const core::Point result{
      transform.translation.x + scaled_x * std::cos(radians) - scaled_y * std::sin(radians),
      transform.translation.y + scaled_x * std::sin(radians) + scaled_y * std::cos(radians)};
  if (!std::isfinite(result.x) || !std::isfinite(result.y)) {
    return std::nullopt;
  }
  return result;
}

std::vector<core::Point> primitiveLocalBoundsPoints(const core::Primitive& primitive) {
  return std::visit(
      [](const auto& value) {
        using Primitive = std::decay_t<decltype(value)>;
        std::vector<core::Point> points;
        if constexpr (std::is_same_v<Primitive, core::Circle>) {
          points = {{-value.radius, 0.0}, {value.radius, 0.0},
                    {0.0, -value.radius}, {0.0, value.radius}};
        } else if constexpr (std::is_same_v<Primitive, core::Rectangle>) {
          points = {{-value.width / 2.0, -value.height / 2.0},
                    {value.width / 2.0, -value.height / 2.0},
                    {value.width / 2.0, value.height / 2.0},
                    {-value.width / 2.0, value.height / 2.0}};
        } else if constexpr (std::is_same_v<Primitive, core::GoldenRectangle>) {
          const double half_width = value.longSide() / 2.0;
          const double half_height = value.short_side / 2.0;
          points = {{-half_width, -half_height}, {half_width, -half_height},
                    {half_width, half_height}, {-half_width, half_height}};
        } else {
          constexpr int sample_count = 720;
          points.reserve(sample_count + 1);
          for (int index = 0; index <= sample_count; ++index) {
            const double fraction = static_cast<double>(index) / sample_count;
            const double degrees = value.start_degrees + value.sweep_degrees * fraction;
            const double radians = degrees * kPi / 180.0;
            points.push_back({value.radius * std::cos(radians),
                              value.radius * std::sin(radians)});
          }
        }
        return points;
      },
      primitive);
}

bool isCorner(const interaction::ResizeHandle handle) {
  return handle == interaction::ResizeHandle::top_left ||
         handle == interaction::ResizeHandle::top_right ||
         handle == interaction::ResizeHandle::bottom_right ||
         handle == interaction::ResizeHandle::bottom_left;
}

bool isEdge(const interaction::ResizeHandle handle) {
  return !isCorner(handle);
}

interaction::ResizeHandle oppositeHandle(const interaction::ResizeHandle handle) {
  switch (handle) {
    case interaction::ResizeHandle::top_left: return interaction::ResizeHandle::bottom_right;
    case interaction::ResizeHandle::top_center: return interaction::ResizeHandle::bottom_center;
    case interaction::ResizeHandle::top_right: return interaction::ResizeHandle::bottom_left;
    case interaction::ResizeHandle::middle_right: return interaction::ResizeHandle::middle_left;
    case interaction::ResizeHandle::bottom_right: return interaction::ResizeHandle::top_left;
    case interaction::ResizeHandle::bottom_center: return interaction::ResizeHandle::top_center;
    case interaction::ResizeHandle::bottom_left: return interaction::ResizeHandle::top_right;
    case interaction::ResizeHandle::middle_left: return interaction::ResizeHandle::middle_right;
  }
  return interaction::ResizeHandle::top_left;
}

std::size_t handleIndex(const interaction::ResizeHandle handle) {
  return static_cast<std::size_t>(handle);
}

void drawCurve(
    QPainter& painter,
    const geometry::CurveInput& curve,
    const CanvasView& canvas,
    const QPointF offset) {
  std::visit(
      [&painter, &canvas, offset](const auto& value) {
        using Curve = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Curve, geometry::CircleInput>) {
          painter.drawEllipse(
              canvas.documentToView(QPointF(value.center_x, value.center_y) + offset),
              value.radius * canvas.zoom(), value.radius * canvas.zoom());
        } else if constexpr (std::is_same_v<Curve, geometry::SegmentInput>) {
          painter.drawLine(
              canvas.documentToView(QPointF(value.source_x, value.source_y) + offset),
              canvas.documentToView(QPointF(value.target_x, value.target_y) + offset));
        } else {
          drawArc(painter, value, canvas, offset);
        }
      },
      curve);
}

double curveDistance(const QPointF point, const geometry::CurveInput& curve) {
  return std::visit(
      [point](const auto& value) {
        using Curve = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Curve, geometry::CircleInput>) {
          const double distance = std::hypot(point.x() - value.center_x, point.y() - value.center_y);
          return distance <= value.radius ? distance : std::numeric_limits<double>::infinity();
        } else if constexpr (std::is_same_v<Curve, geometry::SegmentInput>) {
          return std::sqrt(distanceSquaredToSegment(
              point, QPointF(value.source_x, value.source_y),
              QPointF(value.target_x, value.target_y)));
        } else {
          return distanceToArc(point, value);
        }
      },
      curve);
}

QPointF boundaryPoint(const geometry::BoundaryPoint& point) {
  return QPointF(point.x, point.y);
}

template <typename Mapper>
QPainterPath boundaryLoopPath(const geometry::BoundaryLoop& loop, const Mapper& map_point) {
  QPainterPath path;
  if (loop.edges.empty()) {
    return path;
  }

  for (std::size_t edge_index = 0; edge_index < loop.edges.size(); ++edge_index) {
    const auto& edge = loop.edges[edge_index];
    const QPointF edge_start = boundaryPoint(edge.start);
    if (edge_index == 0) {
      path.moveTo(map_point(edge_start));
    } else {
      path.lineTo(map_point(edge_start));
    }
    std::visit(
        [&path, &map_point](const auto& curve) {
          using Curve = std::decay_t<decltype(curve)>;
          if constexpr (std::is_same_v<Curve, geometry::LineSegmentBoundary>) {
            path.lineTo(map_point(boundaryPoint(curve.end)));
          } else {
            const QPointF center = boundaryPoint(curve.center);
            const QPointF start = boundaryPoint(curve.start);
            const QPointF end = boundaryPoint(curve.end);
            const double radius = curve.radius;
            const double start_angle = std::atan2(start.y() - center.y(), start.x() - center.x());
            const double end_angle = std::atan2(end.y() - center.y(), end.x() - center.x());
            double sweep = curve.direction == geometry::BoundaryDirection::counterclockwise
                               ? positiveAngleDelta(start_angle, end_angle)
                               : -positiveAngleDelta(end_angle, start_angle);
            if (std::abs(sweep) <= kGeometryEpsilon) {
              sweep = curve.direction == geometry::BoundaryDirection::counterclockwise
                          ? 2.0 * kPi
                          : -2.0 * kPi;
            }
            if (!std::isfinite(radius) || radius <= 0.0 || !std::isfinite(sweep)) {
              path.lineTo(map_point(end));
              return;
            }
            const int segments = std::clamp(
                static_cast<int>(std::ceil(std::abs(sweep) * 180.0 / kPi / 6.0)), 8, 720);
            for (int index = 1; index <= segments; ++index) {
              const double fraction = static_cast<double>(index) / static_cast<double>(segments);
              const double angle = start_angle + sweep * fraction;
              path.lineTo(map_point(
                  center + QPointF(radius * std::cos(angle), radius * std::sin(angle))));
            }
          }
        },
        edge.curve);
  }
  if (loop.closed) {
    path.closeSubpath();
  }
  return path;
}

template <typename Mapper>
QPainterPath booleanRegionPath(const geometry::FaceClassification& region, const Mapper& map_point) {
  QPainterPath path;
  path.setFillRule(Qt::OddEvenFill);
  path.addPath(boundaryLoopPath(region.outer_boundary, map_point));
  for (const auto& hole : region.holes) {
    path.addPath(boundaryLoopPath(hole, map_point));
  }
  return path;
}

template <typename Mapper>
QPainterPath evaluatedRegionPath(
    const geometry::EvaluatedRegion& region,
    const Mapper& map_point) {
  QPainterPath path;
  path.setFillRule(Qt::OddEvenFill);
  path.addPath(boundaryLoopPath(region.cell.outer_boundary, map_point));
  for (const auto& hole : region.cell.holes) {
    path.addPath(boundaryLoopPath(hole, map_point));
  }
  return path;
}

bool closeDocumentPoints(const QPointF left, const QPointF right) {
  const double scale = std::max({1.0, std::abs(left.x()), std::abs(left.y()),
                                 std::abs(right.x()), std::abs(right.y())});
  const double tolerance = 1.0e-7 * scale;
  return std::abs(left.x() - right.x()) <= tolerance &&
         std::abs(left.y() - right.y()) <= tolerance;
}

bool isClosedCurveSet(
    const geometry::EvaluatedCurveSet& curve_set,
    bool* contains_open_arc) {
  if (contains_open_arc != nullptr) {
    *contains_open_arc = false;
  }
  if (curve_set.curves.size() == 1 &&
      std::holds_alternative<geometry::CircleInput>(curve_set.curves.front())) {
    const auto& circle = std::get<geometry::CircleInput>(curve_set.curves.front());
    return std::isfinite(circle.center_x) && std::isfinite(circle.center_y) &&
           std::isfinite(circle.radius) && circle.radius > kGeometryEpsilon;
  }
  if (curve_set.curves.size() < 3) {
    for (const auto& curve : curve_set.curves) {
      if (std::holds_alternative<geometry::ArcInput>(curve) && contains_open_arc != nullptr) {
        *contains_open_arc = true;
      }
    }
    return false;
  }
  std::vector<QPointF> sources;
  std::vector<QPointF> targets;
  sources.reserve(curve_set.curves.size());
  targets.reserve(curve_set.curves.size());
  for (const auto& curve : curve_set.curves) {
    if (const auto* segment = std::get_if<geometry::SegmentInput>(&curve);
        segment != nullptr) {
      sources.emplace_back(segment->source_x, segment->source_y);
      targets.emplace_back(segment->target_x, segment->target_y);
      continue;
    }
    if (std::holds_alternative<geometry::ArcInput>(curve) && contains_open_arc != nullptr) {
      *contains_open_arc = true;
    }
    return false;
  }
  for (std::size_t index = 0; index < sources.size(); ++index) {
    if (!closeDocumentPoints(targets[index], sources[(index + 1) % sources.size()])) {
      return false;
    }
  }
  return closeDocumentPoints(sources.front(), targets.back());
}

QString splitStatusMessage(const geometry::SplitStatus status) {
  switch (status) {
    case geometry::SplitStatus::invalid_input:
      return QCoreApplication::translate(
          "signet::ui::CanvasView", "Cannot split this shape");
    case geometry::SplitStatus::success:
      return {};
    case geometry::SplitStatus::nonintersection:
      return QCoreApplication::translate(
          "signet::ui::CanvasView", "The split line does not cross the shape");
    case geometry::SplitStatus::tangent:
      return QCoreApplication::translate(
          "signet::ui::CanvasView", "The split line only touches the shape");
    case geometry::SplitStatus::boundary_coincident:
      return QCoreApplication::translate(
          "signet::ui::CanvasView", "The split line follows the shape boundary");
    case geometry::SplitStatus::vertex_touch:
      return QCoreApplication::translate(
          "signet::ui::CanvasView", "Move the split line away from a corner");
    case geometry::SplitStatus::odd_intersections:
      return QCoreApplication::translate(
          "signet::ui::CanvasView",
          "The split line must cross the shape twice");
    case geometry::SplitStatus::branch_ambiguity:
      return QCoreApplication::translate(
          "signet::ui::CanvasView", "Cannot determine the split regions");
  }
  return QCoreApplication::translate(
      "signet::ui::CanvasView", "Cannot split this shape");
}

}  // namespace

CanvasView::CanvasView(core::DocumentHistory& history, QWidget* parent)
    : QWidget(parent), history_(history) {
  setObjectName(QStringLiteral("constructionCanvas"));
  setFocusPolicy(Qt::StrongFocus);
  setMinimumSize(720, 520);
  setAccessibleName(tr("Geometric construction canvas"));
  refreshFromDocument();
}

void CanvasView::refreshFromDocument() {
  cancelTransientInteraction();
  evaluation_ = geometry::DocumentEvaluator::evaluate(history_.document());
  bool selection_changed = false;
  const auto valid_selection = normalizedSelection(selected_node_ids_);
  if (valid_selection != selected_node_ids_) {
    selected_node_ids_ = valid_selection;
    selection_changed = true;
  }
  if (selected_region_split_id_.has_value()) {
    const auto split = std::ranges::find_if(
        evaluation_.splits,
        [split_id = *selected_region_split_id_](const geometry::EvaluatedSplit& value) {
          return value.node_id == split_id;
        });
    if (split == evaluation_.splits.end() ||
        split->status != geometry::SplitStatus::success) {
      clearRegionSelection();
      selection_changed = true;
    } else {
      const bool all_keys_resolve = std::ranges::all_of(
          selected_region_keys_, [split](const core::RegionKey& key) {
            return std::ranges::any_of(
                split->cells,
                [&key](const geometry::EvaluatedRegion& region) { return region.key == key; });
          });
      if (!all_keys_resolve) {
        clearRegionSelection();
        selection_changed = true;
      }
    }
  } else if (!selected_region_keys_.empty()) {
    clearRegionSelection();
    selection_changed = true;
  }
  if (selection_changed) {
    emit selectionChanged();
  }
  update();
  emit viewportChanged();
}

void CanvasView::setSelectedNode(const std::optional<core::NodeId> node_id) {
  setSelectedNodes(node_id.has_value() ? std::vector<core::NodeId>{*node_id}
                                       : std::vector<core::NodeId>{});
}

void CanvasView::setSelectedNodes(std::vector<core::NodeId> node_ids) {
  node_ids = normalizedSelection(node_ids);
  if (selected_node_ids_ == node_ids) {
    return;
  }
  cancelTransientInteraction();
  selected_node_ids_ = std::move(node_ids);
  const auto new_region_context = selected_node_ids_.size() == 1
                                      ? regionContextForNode(selected_node_ids_.front())
                                      : std::nullopt;
  const bool region_selection_changed =
      selected_region_split_id_.has_value() &&
      new_region_context != selected_region_split_id_;
  if (region_selection_changed) {
    clearRegionSelection();
  }
  emit selectionChanged();
  update();
}

void CanvasView::setTool(const Tool tool) {
  if (tool_ == tool) {
    if (interaction_ != Interaction::none) {
      cancelTransientInteraction();
    }
    update();
    return;
  }
  cancelTransientInteraction();
  tool_ = tool;
  emit toolChanged(tool_);
  update();
}

void CanvasView::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event)
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.fillRect(rect(), palette().color(QPalette::Base));

  painter.save();
  painter.setPen(QPen(palette().color(QPalette::Midlight), 1.0));
  constexpr int grid_step = 20;
  const int grid_x = ((width() / 2) % grid_step + grid_step) % grid_step;
  const int grid_y = ((height() / 2) % grid_step + grid_step) % grid_step;
  for (int x = grid_x; x < width(); x += grid_step) {
    painter.drawLine(x, 0, x, height());
  }
  for (int y = grid_y; y < height(); y += grid_step) {
    painter.drawLine(0, y, width(), y);
  }
  painter.restore();

  const auto isSelected = [this](const core::NodeId node_id) {
    return std::ranges::find(selected_node_ids_, node_id) != selected_node_ids_.end();
  };
  const auto previewOffset = [this](const core::NodeId node_id) {
    const auto transform = previewTransform(node_id);
    if (!transform.has_value()) {
      return QPointF{};
    }
    const auto* node = history_.document().findNode(node_id);
    const auto* primitive = node == nullptr ? nullptr : std::get_if<core::PrimitiveNode>(&node->definition);
    if (primitive == nullptr) {
      return QPointF{};
    }
    return QPointF(
        transform->translation.x - primitive->transform.translation.x,
        transform->translation.y - primitive->transform.translation.y);
  };
  const bool preview_geometry = interaction_ == Interaction::resize_uniform ||
                                interaction_ == Interaction::resize_rectangle ||
                                interaction_ == Interaction::rotate;

  painter.setBrush(Qt::NoBrush);
  for (const auto& evaluated : evaluation_.circles) {
    if (preview_geometry && isSelected(evaluated.node_id)) {
      continue;
    }
    painter.setPen(QPen(isSelected(evaluated.node_id) ? selectionAccent(palette())
                                                      : palette().color(QPalette::Text),
                         isSelected(evaluated.node_id) ? 2.5 : 1.5));
    const QPointF offset = previewOffset(evaluated.node_id);
    const QPointF center = documentToView(
        QPointF(evaluated.circle.center_x, evaluated.circle.center_y) + offset);
    const double radius = evaluated.circle.radius * zoom_;
    painter.drawEllipse(center, radius, radius);
  }

  for (const auto& evaluated : evaluation_.curve_sets) {
    if (preview_geometry && isSelected(evaluated.node_id)) {
      continue;
    }
    painter.setPen(QPen(isSelected(evaluated.node_id) ? selectionAccent(palette())
                                                      : palette().color(QPalette::Text),
                         isSelected(evaluated.node_id) ? 2.5 : 1.5));
    const QPointF offset = previewOffset(evaluated.node_id);
    for (const auto& curve : evaluated.curves) {
      drawCurve(painter, curve, *this, offset);
    }
  }

  const auto to_view = [this](const QPointF point) { return documentToView(point); };
  for (const auto& evaluated : evaluation_.booleans) {
    for (const auto& region : evaluated.regions) {
      const QPainterPath path = booleanRegionPath(region, to_view);
      if (path.isEmpty()) {
        continue;
      }
      const bool selected = isSelected(evaluated.node_id);
      painter.save();
      painter.setPen(QPen(selected ? selectionAccent(palette()) : palette().color(QPalette::Mid),
                          selected ? 2.5 : 1.25));
      const QColor accent = selectionAccent(palette());
      painter.setBrush(selected ? QColor(accent.red(), accent.green(), accent.blue(), 45)
                              : QColor(palette().color(QPalette::Mid).red(),
                                       palette().color(QPalette::Mid).green(),
                                       palette().color(QPalette::Mid).blue(), 25));
      painter.drawPath(path);
      painter.restore();
    }
  }

  for (const auto& evaluated : evaluation_.splits) {
    if (evaluated.status != geometry::SplitStatus::success) {
      continue;
    }
    for (const auto& region : evaluated.cells) {
      const QPainterPath path = evaluatedRegionPath(region, to_view);
      if (path.isEmpty()) {
        continue;
      }
      const bool selected = selected_region_split_id_ == evaluated.node_id &&
                            std::ranges::find(
                                selected_region_keys_, region.key) != selected_region_keys_.end();
      painter.save();
      painter.setPen(QPen(selected ? splitAccent(palette()) : palette().color(QPalette::Mid),
                          selected ? 2.5 : 1.0));
      const QColor accent = splitAccent(palette());
      painter.setBrush(selected ? QColor(accent.red(), accent.green(), accent.blue(), 75)
                              : QColor(palette().color(QPalette::Mid).red(),
                                       palette().color(QPalette::Mid).green(),
                                       palette().color(QPalette::Mid).blue(), 24));
      painter.drawPath(path);
      painter.restore();
    }
  }

  if (preview_geometry) {
    painter.save();
    QPen preview_pen(selectionAccent(palette()), 2.5);
    preview_pen.setCosmetic(true);
    painter.setPen(preview_pen);
    painter.setBrush(Qt::NoBrush);
    for (const auto& preview : preview_transforms_) {
      const auto* node = history_.document().findNode(preview.node_id);
      const auto* primitive = node == nullptr ? nullptr
                                               : std::get_if<core::PrimitiveNode>(&node->definition);
      if (primitive == nullptr) {
        continue;
      }
      drawPlacementPreview(painter,
                           interaction::PrimitivePlacement{primitive->primitive, preview.current},
                           *this);
    }
    painter.restore();
  }

  if (placement_preview_.has_value()) {
    painter.save();
    QPen preview_pen(selectionAccent(palette()), 1.5, Qt::DashLine);
    preview_pen.setCosmetic(true);
    painter.setPen(preview_pen);
    painter.setBrush(Qt::NoBrush);
    drawPlacementPreview(painter, *placement_preview_, *this);
    painter.restore();
  } else if (interaction_ == Interaction::place_arc && placement_cursor_.has_value() &&
             !placement_points_.empty()) {
    painter.save();
    QPen preview_pen(selectionAccent(palette()), 1.5, Qt::DashLine);
    preview_pen.setCosmetic(true);
    painter.setPen(preview_pen);
    const QPointF start = documentToView(
        QPointF(placement_points_.front().x, placement_points_.front().y));
    const QPointF cursor = documentToView(
        QPointF(placement_cursor_->x, placement_cursor_->y));
    painter.drawLine(start, cursor);
    if (placement_points_.size() >= 2) {
      const QPointF interior = documentToView(
          QPointF(placement_points_[1].x, placement_points_[1].y));
      painter.drawLine(interior, cursor);
    }
    painter.restore();
  }

  if (const auto line = splitPreviewLine(); line.has_value()) {
    painter.save();
    QPen preview_pen(splitAccent(palette()), 1.5, Qt::DashLine);
    preview_pen.setCosmetic(true);
    painter.setPen(preview_pen);
    painter.drawLine(*line);
    painter.restore();
  }

  for (const auto& guide : circleGuideOverlays()) {
    painter.save();
    QPen guide_pen(selectionAccent(palette()), 1.25);
    guide_pen.setCosmetic(true);
    painter.setPen(guide_pen);
    painter.drawLine(guide.center_view, guide.radius_endpoint_view);
    painter.drawLine(
        guide.center_view - QPointF(kGuideCenterMarkHalfSizePixels, 0.0),
        guide.center_view + QPointF(kGuideCenterMarkHalfSizePixels, 0.0));
    painter.drawLine(
        guide.center_view - QPointF(0.0, kGuideCenterMarkHalfSizePixels),
        guide.center_view + QPointF(0.0, kGuideCenterMarkHalfSizePixels));

    QFont guide_font = painter.font();
    guide_font.setPixelSize(kGuideFontPixelSize);
    painter.setFont(guide_font);
    const QFontMetricsF metrics(guide_font);
    const QRectF text_bounds = metrics.boundingRect(guide.radius_label);
    constexpr double margin = 4.0;
    double label_x = guide.radius_endpoint_view.x() + kGuideLabelGapPixels;
    if (label_x + text_bounds.width() > width() - margin) {
      label_x = guide.radius_endpoint_view.x() - kGuideLabelGapPixels - text_bounds.width();
    }
    label_x = std::clamp(
        label_x, margin, std::max(margin, static_cast<double>(width()) - margin - text_bounds.width()));
    double label_baseline_y = guide.radius_endpoint_view.y() - kGuideLabelGapPixels;
    const double minimum_baseline = margin + metrics.ascent();
    const double maximum_baseline = height() - margin - metrics.descent();
    label_baseline_y = std::clamp(
        label_baseline_y, minimum_baseline, std::max(minimum_baseline, maximum_baseline));
    painter.drawText(QPointF(label_x, label_baseline_y), guide.radius_label);
    painter.restore();
  }

  for (const auto& guide : snapGuideOverlays()) {
    painter.save();
    QColor color = guideAccent(palette(), guide.kind);
    QPen guide_pen(color, 1.0, Qt::DashLine);
    guide_pen.setCosmetic(true);
    painter.setPen(guide_pen);
    if (guide.kind == SnapGuideKind::grid || guide.kind == SnapGuideKind::center) {
      painter.drawLine(QPointF(guide.target_view.x(), 0.0),
                       QPointF(guide.target_view.x(), height()));
      painter.drawLine(QPointF(0.0, guide.target_view.y()),
                       QPointF(width(), guide.target_view.y()));
    }
    painter.drawLine(guide.source_view, guide.target_view);
    painter.drawLine(guide.target_view - QPointF(5.0, 0.0),
                     guide.target_view + QPointF(5.0, 0.0));
    painter.drawLine(guide.target_view - QPointF(0.0, 5.0),
                     guide.target_view + QPointF(0.0, 5.0));
    if (!guide.label.isEmpty()) {
      painter.drawText(guide.target_view + QPointF(7.0, -7.0), guide.label);
    }
    painter.restore();
  }

  if (tool_ == Tool::select && !selected_node_ids_.empty()) {
    const auto layout = selectionHandleLayout();
    if (layout.has_value()) {
      const auto points = selectionHandlePointsInView();
      painter.save();
      QPen bounds_pen(selectionAccent(palette()), 1.0, Qt::DashLine);
      bounds_pen.setCosmetic(true);
      painter.setPen(bounds_pen);
      painter.setBrush(Qt::NoBrush);
      painter.drawRect(QRectF(QPointF(layout->bounds.min.x, layout->bounds.min.y),
                              QPointF(layout->bounds.max.x, layout->bounds.max.y)));
      for (const auto point : points) {
        painter.setPen(QPen(selectionAccent(palette()), 1.0));
        painter.setBrush(palette().color(QPalette::Base));
        painter.drawRect(QRectF(point.x - layout->handle_size_view / 2.0,
                                point.y - layout->handle_size_view / 2.0,
                                layout->handle_size_view, layout->handle_size_view));
      }
      painter.setPen(QPen(selectionAccent(palette()), 1.0));
      painter.setBrush(palette().color(QPalette::Base));
      painter.drawLine(QPointF(layout->rotate_handle.x, layout->bounds.min.y),
                       QPointF(layout->rotate_handle.x, layout->rotate_handle.y));
      painter.drawEllipse(QPointF(layout->rotate_handle.x, layout->rotate_handle.y),
                          layout->handle_size_view / 2.0,
                          layout->handle_size_view / 2.0);
      painter.restore();
    }
  }

}

void CanvasView::mousePressEvent(QMouseEvent* event) {
  setFocus(Qt::MouseFocusReason);
  const bool space_pan = event->button() == Qt::LeftButton && space_down_;
  const bool advancing_arc = interaction_ == Interaction::place_arc &&
                             event->button() == Qt::LeftButton;
  if (interaction_ != Interaction::none && !advancing_arc) {
    event->accept();
    return;
  }
  if (event->button() == Qt::MiddleButton || space_pan) {
    interaction_ = Interaction::pan;
    interaction_start_view_ = event->position();
    pan_origin_ = pan_offset_;
    event->accept();
    return;
  }
  if (event->button() != Qt::LeftButton) {
    QWidget::mousePressEvent(event);
    return;
  }

  const QPointF document_point = viewToDocument(event->position());
  if (tool_ == Tool::circle || tool_ == Tool::rectangle || tool_ == Tool::golden_rectangle) {
    const core::Point placement_point = snapDocumentPoint(
        {document_point.x(), document_point.y()}, event->modifiers());
    placement_points_.clear();
    placement_points_.push_back(placement_point);
    placement_cursor_ = placement_point;
    placement_preview_.reset();
    interaction_ = tool_ == Tool::circle
                       ? Interaction::place_circle
                       : tool_ == Tool::rectangle ? Interaction::place_rectangle
                                                  : Interaction::place_golden;
    updatePlacementPreview(
        QPointF(placement_point.x, placement_point.y), event->modifiers());
    event->accept();
    return;
  }
  if (tool_ == Tool::arc) {
    const core::Point placement_point = snapDocumentPoint(
        {document_point.x(), document_point.y()}, event->modifiers());
    if (placement_points_.size() >= 3) {
      placement_points_.clear();
    }
    placement_points_.push_back(placement_point);
    placement_cursor_ = placement_point;
    interaction_ = Interaction::place_arc;
    updatePlacementPreview(
        QPointF(placement_point.x, placement_point.y), event->modifiers());
    if (placement_points_.size() == 3) {
      commitPlacement();
    }
    event->accept();
    return;
  }
  if (tool_ == Tool::split) {
    beginSplit(document_point);
    event->accept();
    return;
  }

  if (!selected_node_ids_.empty() && selectionIsPrimitive()) {
    if (rotateHandleAt(event->position())) {
      beginRotate(event->position());
      event->accept();
      return;
    }
    const auto handle = resizeHandleAt(event->position());
    if (handle.has_value()) {
      beginResize(*handle, event->position());
      event->accept();
      return;
    }
  }

  if (const auto region_hit = regionAt(document_point); region_hit.has_value()) {
    if (selected_node_ids_ != std::vector<core::NodeId>{region_hit->split_node_id}) {
      setSelectedNode(region_hit->split_node_id);
    }
    toggleRegionSelection(*region_hit, event->modifiers());
    event->accept();
    return;
  }

  const auto hit = primitiveAt(document_point);
  const bool extending = event->modifiers().testFlag(Qt::ShiftModifier) ||
                         event->modifiers().testFlag(Qt::ControlModifier) ||
                         event->modifiers().testFlag(Qt::MetaModifier);
  if (!hit.has_value()) {
    if (!extending) {
      setSelectedNodes({});
    }
    event->accept();
    return;
  }
  if (extending) {
    auto next = selected_node_ids_;
    const auto found = std::ranges::find(next, *hit);
    if (found == next.end()) {
      next.push_back(*hit);
    } else {
      next.erase(found);
    }
    setSelectedNodes(std::move(next));
    event->accept();
    return;
  }

  if (std::ranges::find(selected_node_ids_, *hit) == selected_node_ids_.end()) {
    setSelectedNode(hit);
  }
  if (std::ranges::find(selected_node_ids_, *hit) != selected_node_ids_.end()) {
    interaction_start_view_ = event->position();
    beginPrimitiveDrag(document_point, event->modifiers());
  }
  event->accept();
}

void CanvasView::mouseMoveEvent(QMouseEvent* event) {
  if (interaction_ == Interaction::pan) {
    pan_offset_ = pan_origin_ + event->position() - interaction_start_view_;
    update();
    event->accept();
    return;
  }
  if (interaction_ == Interaction::drag) {
    updateDragPreview(viewToDocument(event->position()), event->modifiers());
    update();
    event->accept();
    return;
  }
  if (interaction_ == Interaction::split) {
    updateSplitPreview(viewToDocument(event->position()));
    event->accept();
    return;
  }
  if (interaction_ == Interaction::resize_uniform ||
      interaction_ == Interaction::resize_rectangle || interaction_ == Interaction::rotate) {
    updateTransformPreview(event->position(), event->modifiers());
    event->accept();
    return;
  }
  if (interaction_ == Interaction::place_circle ||
      interaction_ == Interaction::place_rectangle || interaction_ == Interaction::place_arc ||
      interaction_ == Interaction::place_golden) {
    const QPointF document_point = viewToDocument(event->position());
    updatePlacementPreview(document_point, event->modifiers());
    update();
    event->accept();
    return;
  }
  QWidget::mouseMoveEvent(event);
}

void CanvasView::mouseReleaseEvent(QMouseEvent* event) {
  if (interaction_ == Interaction::split) {
    if (event->button() != Qt::LeftButton) {
      cancelTransientInteraction();
      event->accept();
      return;
    }
    updateSplitPreview(viewToDocument(event->position()));
    commitSplit();
    event->accept();
    return;
  }
  if (interaction_ == Interaction::drag) {
    if (event->button() != Qt::LeftButton) {
      cancelTransientInteraction();
      event->accept();
      return;
    }
    updateDragPreview(viewToDocument(event->position()), event->modifiers());
    const std::vector<core::TransformUpdate> updates = [&] {
      std::vector<core::TransformUpdate> result;
      result.reserve(preview_transforms_.size());
      for (const auto& preview : preview_transforms_) {
        result.push_back({preview.node_id, preview.current});
      }
      return result;
    }();
    preview_transforms_.clear();
    interaction_ = Interaction::none;
    if (history_.applyTransforms(updates).changed) {
      refreshFromDocument();
      emit documentChanged();
    }
    update();
    event->accept();
    return;
  }
  if (interaction_ == Interaction::resize_uniform ||
      interaction_ == Interaction::resize_rectangle || interaction_ == Interaction::rotate) {
    if (event->button() != Qt::LeftButton) {
      cancelTransientInteraction();
      event->accept();
      return;
    }
    updateTransformPreview(event->position(), event->modifiers());
    commitTransformPreview();
    event->accept();
    return;
  }
  if (interaction_ == Interaction::place_circle ||
      interaction_ == Interaction::place_rectangle || interaction_ == Interaction::place_golden) {
    if (event->button() != Qt::LeftButton) {
      cancelTransientInteraction();
      event->accept();
      return;
    }
    updatePlacementPreview(viewToDocument(event->position()), event->modifiers());
    commitPlacement();
    event->accept();
    return;
  }
  if (interaction_ == Interaction::pan &&
      (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
    interaction_ = Interaction::none;
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void CanvasView::wheelEvent(QWheelEvent* event) {
  if (interaction_ != Interaction::none || event->angleDelta().y() == 0) {
    event->ignore();
    return;
  }
  const QPointF anchor = viewToDocument(event->position());
  const double factor = event->angleDelta().y() > 0 ? 1.2 : 1.0 / 1.2;
  zoom_ = std::clamp(zoom_ * factor, 0.1, 20.0);
  pan_offset_ = event->position() - QPointF(width() / 2.0, height() / 2.0) -
                QPointF(anchor.x() * zoom_, -anchor.y() * zoom_);
  emit viewportChanged();
  update();
  event->accept();
}

void CanvasView::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Space) {
    space_down_ = true;
    event->accept();
    return;
  }
  if (event->key() == Qt::Key_Escape) {
    cancelTransientInteraction();
    event->accept();
    return;
  }
  const bool command_modifier = event->modifiers().testFlag(Qt::MetaModifier) ||
                                event->modifiers().testFlag(Qt::ControlModifier);
  if (interaction_ == Interaction::none && event->key() == Qt::Key_D && command_modifier) {
    duplicateSelection();
    event->accept();
    return;
  }
  if (interaction_ == Interaction::none && command_modifier &&
      event->modifiers().testFlag(Qt::AltModifier) && event->key() == Qt::Key_H) {
    flipSelectionHorizontal();
    event->accept();
    return;
  }
  if (interaction_ == Interaction::none && command_modifier &&
      event->modifiers().testFlag(Qt::AltModifier) && event->key() == Qt::Key_V) {
    flipSelectionVertical();
    event->accept();
    return;
  }
  if (interaction_ == Interaction::none &&
      (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)) {
    deleteSelection();
    event->accept();
    return;
  }
  const bool arrow = event->key() == Qt::Key_Left || event->key() == Qt::Key_Right ||
                     event->key() == Qt::Key_Up || event->key() == Qt::Key_Down;
  if (interaction_ == Interaction::none && arrow && !selected_node_ids_.empty()) {
    if (!selectionIsPrimitive()) {
      emit statusMessage(tr("Select shapes of the same kind to move them together"));
      event->accept();
      return;
    }
    const double step = event->modifiers().testFlag(Qt::ShiftModifier) ? 10.0 : 1.0;
    QPointF delta;
    switch (event->key()) {
      case Qt::Key_Left: delta.setX(-step); break;
      case Qt::Key_Right: delta.setX(step); break;
      case Qt::Key_Up: delta.setY(step); break;
      case Qt::Key_Down: delta.setY(-step); break;
      default: break;
    }
    commitKeyboardMove(delta);
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

void CanvasView::keyReleaseEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Space) {
    space_down_ = false;
    event->accept();
    return;
  }
  QWidget::keyReleaseEvent(event);
}

void CanvasView::focusOutEvent(QFocusEvent* event) {
  cancelTransientInteraction();
  space_down_ = false;
  QWidget::focusOutEvent(event);
}

bool CanvasView::event(QEvent* event) {
  if (event->type() == QEvent::UngrabMouse || event->type() == QEvent::WindowDeactivate) {
    cancelTransientInteraction();
  }
  return QWidget::event(event);
}

bool CanvasView::hasTransientInteraction() const noexcept {
  return interaction_ != Interaction::none;
}

bool CanvasView::hasPlacementPreview() const noexcept {
  return placement_preview_.has_value();
}

QPointF CanvasView::circleCenter(const core::NodeId node_id) const {
  const auto* circle = evaluatedCircle(node_id);
  if (circle == nullptr) {
    return {};
  }
  QPointF center(circle->circle.center_x, circle->circle.center_y);
  const auto transform = previewTransform(node_id);
  if (transform.has_value()) {
    const auto* node = history_.document().findNode(node_id);
    const auto* primitive = node == nullptr ? nullptr : std::get_if<core::PrimitiveNode>(&node->definition);
    if (primitive != nullptr) {
      center += QPointF(
          transform->translation.x - primitive->transform.translation.x,
          transform->translation.y - primitive->transform.translation.y);
    }
  }
  return center;
}

std::optional<CircleGuideOverlay> CanvasView::circleGuideOverlay() const {
  const auto overlays = circleGuideOverlays();
  if (overlays.empty()) {
    return std::nullopt;
  }
  return overlays.front();
}

std::vector<CircleGuideOverlay> CanvasView::circleGuideOverlays() const {
  std::vector<CircleGuideOverlay> result;
  result.reserve(selected_node_ids_.size());
  for (const core::NodeId node_id : selected_node_ids_) {
    const auto* node = history_.document().findNode(node_id);
    const auto* primitive_node = node == nullptr ? nullptr
                                                  : std::get_if<core::PrimitiveNode>(&node->definition);
    if (primitive_node == nullptr ||
        std::get_if<core::Circle>(&primitive_node->primitive) == nullptr) {
      continue;
    }
    const auto evaluated_it = std::ranges::find(
        evaluation_.circles, node_id, &geometry::EvaluatedCircle::node_id);
    if (evaluated_it == evaluation_.circles.end()) {
      continue;
    }
    const auto transform = previewTransform(node_id);
    const auto* circle = std::get_if<core::Circle>(&primitive_node->primitive);
    double radius = evaluated_it->circle.radius;
    if (transform.has_value() && circle != nullptr) {
      radius = circle->radius * std::abs(transform->scale.x);
    }
    if (!std::isfinite(radius) || radius < 0.0) {
      continue;
    }
    QPointF center_document(evaluated_it->circle.center_x, evaluated_it->circle.center_y);
    if (transform.has_value()) {
      center_document += QPointF(
          transform->translation.x - primitive_node->transform.translation.x,
          transform->translation.y - primitive_node->transform.translation.y);
    }
    const QPointF endpoint_document = center_document + QPointF(radius, 0.0);
    result.push_back(CircleGuideOverlay{
        node_id,
        documentToView(center_document),
        documentToView(endpoint_document),
        radius,
        QStringLiteral("r = ") + QLocale::c().toString(radius, 'g', 12)});
  }
  return result;
}

std::vector<SnapGuideOverlay> CanvasView::snapGuideOverlays() const {
  if (!snap_guide_.has_value()) {
    return {};
  }
  return {*snap_guide_};
}

std::optional<core::NodeId> CanvasView::primitiveAt(const QPointF point) const {
  const double tolerance = kHitTolerancePixels / std::max(zoom_, kGeometryEpsilon);
  const auto identity = [](const QPointF value) { return value; };

  const auto hitsNode = [&](const core::NodeId node_id) {
    for (const auto& boolean : evaluation_.booleans) {
      if (boolean.node_id != node_id) {
        continue;
      }
      for (const auto& region : boolean.regions) {
        const QPainterPath path = booleanRegionPath(region, identity);
        if (path.contains(point)) {
          return true;
        }
        QPainterPathStroker stroker;
        stroker.setWidth(2.0 * tolerance);
        if (stroker.createStroke(path).contains(point)) {
          return true;
        }
      }
    }
    for (const auto& evaluated : evaluation_.circles) {
      if (evaluated.node_id == node_id &&
          std::hypot(point.x() - evaluated.circle.center_x,
                     point.y() - evaluated.circle.center_y) <= evaluated.circle.radius) {
        return true;
      }
    }
    for (const auto& evaluated : evaluation_.curve_sets) {
      if (evaluated.node_id != node_id) {
        continue;
      }
      for (const auto& curve : evaluated.curves) {
        if (curveDistance(point, curve) <= tolerance) {
          return true;
        }
      }
    }
    return false;
  };

  // Keep an already selected object draggable even when a later Boolean
  // result overlaps it.  Selection order is normalized to document order.
  for (const core::NodeId node_id : selected_node_ids_) {
    if (hitsNode(node_id)) {
      return node_id;
    }
  }

  for (auto boolean_it = evaluation_.booleans.rbegin();
       boolean_it != evaluation_.booleans.rend(); ++boolean_it) {
    for (auto region_it = boolean_it->regions.rbegin();
         region_it != boolean_it->regions.rend(); ++region_it) {
      const QPainterPath path = booleanRegionPath(*region_it, identity);
      if (path.contains(point)) {
        return boolean_it->node_id;
      }
      QPainterPathStroker stroker;
      stroker.setWidth(2.0 * tolerance);
      if (stroker.createStroke(path).contains(point)) {
        return boolean_it->node_id;
      }
    }
  }

  std::optional<core::NodeId> result;
  double nearest_distance = std::numeric_limits<double>::infinity();
  for (const auto& evaluated : evaluation_.circles) {
    const double distance = std::hypot(
        point.x() - evaluated.circle.center_x, point.y() - evaluated.circle.center_y);
    if (distance <= evaluated.circle.radius &&
        (!result.has_value() || distance < nearest_distance)) {
      result = evaluated.node_id;
      nearest_distance = distance;
    }
  }
  for (const auto& evaluated : evaluation_.curve_sets) {
    for (const auto& curve : evaluated.curves) {
      const double distance = curveDistance(point, curve);
      if (distance <= tolerance && distance < nearest_distance) {
        result = evaluated.node_id;
        nearest_distance = distance;
      }
    }
  }
  return result;
}

std::vector<interaction::SelectionItem> CanvasView::selectionItems() const {
  std::vector<interaction::SelectionItem> result;
  if (!selectionIsPrimitive()) {
    return result;
  }
  result.reserve(selected_node_ids_.size());
  for (const core::NodeId node_id : selected_node_ids_) {
    const auto* node = history_.document().findNode(node_id);
    const auto* primitive = node == nullptr ? nullptr
                                             : std::get_if<core::PrimitiveNode>(&node->definition);
    if (primitive == nullptr) {
      result.clear();
      return result;
    }
    result.push_back({node_id, primitive->primitive, primitive->transform});
  }
  return result;
}

std::optional<core::SymmetryAxis> CanvasView::selectionSymmetryAxis(
    const interaction::ReflectionAxis axis) const {
  return interaction::symmetryAxisForSelection(selectionItems(), axis);
}

core::Point CanvasView::snapDocumentPoint(
    const core::Point point,
    const Qt::KeyboardModifiers modifiers) {
  snap_guide_.reset();
  if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
      modifiers.testFlag(Qt::AltModifier)) {
    return point;
  }

  const double max_distance = kSnapToleranceLogicalPx / std::max(zoom_, kGeometryEpsilon);
  const auto grid = geometry::snapToGrid(
      geometry::SnapPoint{point.x, point.y}, kSnapGridSpacing,
      geometry::SnapPoint{}, max_distance);

  std::vector<geometry::SnapCandidate> candidates;
  candidates.push_back({1, {0.0, 0.0}});  // document/canvas center
  std::uint64_t identity = 2;
  const auto is_selected = [this](const core::NodeId node_id) {
    return std::ranges::find(selected_node_ids_, node_id) != selected_node_ids_.end();
  };
  for (const auto& circle : evaluation_.circles) {
    if (!is_selected(circle.node_id)) {
      candidates.push_back({identity++, {circle.circle.center_x, circle.circle.center_y}});
    }
  }
  for (const auto& curve_set : evaluation_.curve_sets) {
    if (is_selected(curve_set.node_id)) {
      continue;
    }
    for (const auto& curve : curve_set.curves) {
      std::visit(
          [&candidates, &identity](const auto& value) {
            using Curve = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Curve, geometry::CircleInput>) {
              candidates.push_back({identity++, {value.center_x, value.center_y}});
            } else if constexpr (std::is_same_v<Curve, geometry::SegmentInput>) {
              candidates.push_back({identity++, {value.source_x, value.source_y}});
              candidates.push_back({identity++, {value.target_x, value.target_y}});
            } else {
              candidates.push_back({identity++, {value.source_x, value.source_y}});
              candidates.push_back({identity++, {value.interior_x, value.interior_y}});
              candidates.push_back({identity++, {value.target_x, value.target_y}});
            }
          },
          curve);
    }
  }

  const auto candidate = geometry::snapToCandidates(
      geometry::SnapPoint{point.x, point.y}, candidates, max_distance);
  const double grid_distance = grid.has_value()
                                   ? std::hypot(grid->x - point.x, grid->y - point.y)
                                   : std::numeric_limits<double>::infinity();
  const double candidate_distance = candidate.has_value()
                                        ? std::hypot(candidate->point.x - point.x,
                                                     candidate->point.y - point.y)
                                        : std::numeric_limits<double>::infinity();
  if (!std::isfinite(grid_distance) && !std::isfinite(candidate_distance)) {
    return point;
  }

  core::Point target{point.x, point.y};
  SnapGuideKind kind = SnapGuideKind::grid;
  QString label;
  if (grid_distance <= candidate_distance) {
    target = {grid->x, grid->y};
    label = tr("Grid");
  } else {
    target = {candidate->point.x, candidate->point.y};
    kind = candidate->identity == 1 ? SnapGuideKind::center : SnapGuideKind::geometry;
    label = kind == SnapGuideKind::center ? tr("Center") : tr("Geometry");
  }
  const QPointF source_view = documentToView(QPointF(point.x, point.y));
  const QPointF target_view = documentToView(QPointF(target.x, target.y));
  if (std::isfinite(target_view.x()) && std::isfinite(target_view.y())) {
    snap_guide_ = SnapGuideOverlay{kind, source_view, target_view, label};
  }
  return target;
}

std::optional<interaction::SelectionBounds> CanvasView::selectionBoundsInView() const {
  const auto items = selectionItems();
  if (items.empty()) {
    return std::nullopt;
  }
  std::vector<core::Point> points;
  for (const auto& item : items) {
    const auto local_points = primitiveLocalBoundsPoints(item.primitive);
    const auto transform = previewTransform(item.node_id).value_or(item.transform);
    for (const auto local : local_points) {
      const auto document_point = transformedPoint(local, transform);
      if (!document_point.has_value()) {
        return std::nullopt;
      }
      const QPointF view_point = documentToView(QPointF(document_point->x, document_point->y));
      points.push_back({view_point.x(), view_point.y()});
    }
  }
  return interaction::boundsFromPoints(points);
}

std::array<core::Point, 8> CanvasView::selectionHandlePointsInView() const {
  const auto layout = selectionHandleLayout();
  std::array<core::Point, 8> result{};
  if (!layout.has_value()) {
    return result;
  }
  const auto items = selectionItems();
  if (items.size() != 1 || !std::holds_alternative<core::Rectangle>(items.front().primitive)) {
    return layout->resize_handles;
  }
  const auto& rectangle = std::get<core::Rectangle>(items.front().primitive);
  const auto transform = previewTransform(items.front().node_id).value_or(items.front().transform);
  const double half_width = rectangle.width / 2.0;
  const double half_height = rectangle.height / 2.0;
  const std::array<core::Point, 8> local_points{
      core::Point{-half_width, half_height}, core::Point{0.0, half_height},
      core::Point{half_width, half_height}, core::Point{half_width, 0.0},
      core::Point{half_width, -half_height}, core::Point{0.0, -half_height},
      core::Point{-half_width, -half_height}, core::Point{-half_width, 0.0}};
  for (std::size_t index = 0; index < local_points.size(); ++index) {
    const auto point = transformedPoint(local_points[index], transform);
    if (!point.has_value()) {
      return layout->resize_handles;
    }
    const QPointF view_point = documentToView(QPointF(point->x, point->y));
    result[index] = {view_point.x(), view_point.y()};
  }
  return result;
}

std::optional<interaction::HandleLayout> CanvasView::selectionHandleLayout() const {
  const auto bounds = selectionBoundsInView();
  if (!bounds.has_value()) {
    return std::nullopt;
  }
  return interaction::layoutSelectionHandlesInView(
      *bounds, kSelectionHandleSizeLogicalPx, kSelectionRotateOffsetLogicalPx);
}

std::optional<interaction::ResizeHandle> CanvasView::resizeHandleAt(const QPointF point) const {
  const auto layout = selectionHandleLayout();
  if (!layout.has_value()) {
    return std::nullopt;
  }
  const bool single_rectangle = selected_node_ids_.size() == 1 &&
                                selectionIsPrimitive() &&
                                std::holds_alternative<core::Rectangle>(selectionItems().front().primitive);
  const auto points = selectionHandlePointsInView();
  std::optional<interaction::ResizeHandle> result;
  double best_distance = kSelectionHitToleranceLogicalPx * kSelectionHitToleranceLogicalPx;
  for (std::size_t index = 0; index < points.size(); ++index) {
    const auto handle = static_cast<interaction::ResizeHandle>(index);
    if (!single_rectangle && !isCorner(handle)) {
      continue;
    }
    const double dx = point.x() - points[index].x;
    const double dy = point.y() - points[index].y;
    const double distance = dx * dx + dy * dy;
    if (distance <= best_distance) {
      best_distance = distance;
      result = handle;
    }
  }
  return result;
}

bool CanvasView::rotateHandleAt(const QPointF point) const {
  const auto layout = selectionHandleLayout();
  if (!layout.has_value()) {
    return false;
  }
  const double dx = point.x() - layout->rotate_handle.x;
  const double dy = point.y() - layout->rotate_handle.y;
  return dx * dx + dy * dy <=
         kSelectionHitToleranceLogicalPx * kSelectionHitToleranceLogicalPx;
}

void CanvasView::beginResize(const interaction::ResizeHandle handle, const QPointF view_point) {
  const auto items = selectionItems();
  const auto layout = selectionHandleLayout();
  if (items.empty() || !layout.has_value()) {
    return;
  }
  const bool single_rectangle = items.size() == 1 &&
                                std::holds_alternative<core::Rectangle>(items.front().primitive);
  if ((!single_rectangle && !isCorner(handle))) {
    return;
  }
  gesture_items_ = items;
  gesture_bounds_ = layout->bounds;
  gesture_handle_ = handle;
  gesture_start_view_ = view_point;
  const auto points = selectionHandlePointsInView();
  const auto fixed_handle = oppositeHandle(handle);
  gesture_fixed_view_ = QPointF(points[handleIndex(fixed_handle)].x,
                                points[handleIndex(fixed_handle)].y);
  gesture_is_edge_ = single_rectangle && isEdge(handle);
  if (single_rectangle) {
    const auto& rectangle = std::get<core::Rectangle>(items.front().primitive);
    const double half_width = rectangle.width / 2.0;
    const double half_height = rectangle.height / 2.0;
    const std::array<core::Point, 8> local_points{
        core::Point{-half_width, half_height}, core::Point{0.0, half_height},
        core::Point{half_width, half_height}, core::Point{half_width, 0.0},
        core::Point{half_width, -half_height}, core::Point{0.0, -half_height},
        core::Point{-half_width, -half_height}, core::Point{-half_width, 0.0}};
    gesture_moving_local_ = local_points[handleIndex(handle)];
    gesture_fixed_local_ = local_points[handleIndex(fixed_handle)];
  }
  preview_transforms_.clear();
  preview_transforms_.reserve(items.size());
  for (const auto& item : items) {
    preview_transforms_.push_back({item.node_id, item.transform, item.transform});
  }
  interaction_ = single_rectangle ? Interaction::resize_rectangle : Interaction::resize_uniform;
  update();
}

void CanvasView::beginRotate(const QPointF view_point) {
  const auto items = selectionItems();
  const auto layout = selectionHandleLayout();
  if (items.empty() || !layout.has_value()) {
    return;
  }
  gesture_items_ = items;
  gesture_bounds_ = layout->bounds;
  gesture_start_view_ = view_point;
  gesture_pivot_view_ = QPointF(
      layout->bounds.min.x / 2.0 + layout->bounds.max.x / 2.0,
      layout->bounds.min.y / 2.0 + layout->bounds.max.y / 2.0);
  preview_transforms_.clear();
  preview_transforms_.reserve(items.size());
  for (const auto& item : items) {
    preview_transforms_.push_back({item.node_id, item.transform, item.transform});
  }
  interaction_ = Interaction::rotate;
  update();
}

void CanvasView::updateTransformPreview(
    const QPointF view_point,
    const Qt::KeyboardModifiers modifiers) {
  if (gesture_items_.empty() || preview_transforms_.size() != gesture_items_.size()) {
    return;
  }
  if (interaction_ == Interaction::rotate) {
    const QPointF pivot_document = viewToDocument(gesture_pivot_view_);
    const QPointF start_document = viewToDocument(gesture_start_view_);
    const QPointF current_document = viewToDocument(view_point);
    const double start_angle = std::atan2(start_document.y() - pivot_document.y(),
                                          start_document.x() - pivot_document.x());
    const double current_angle = std::atan2(current_document.y() - pivot_document.y(),
                                            current_document.x() - pivot_document.x());
    if (!std::isfinite(start_angle) || !std::isfinite(current_angle)) {
      return;
    }
    const double delta = (current_angle - start_angle) * 180.0 / kPi;
    const auto transformed = interaction::rotateSelection(
        gesture_items_, {pivot_document.x(), pivot_document.y()}, delta);
    if (!transformed.has_value()) {
      return;
    }
    for (const auto& update : *transformed) {
      const auto found = std::ranges::find(preview_transforms_, update.node_id,
                                           &PreviewTransform::node_id);
      if (found != preview_transforms_.end()) {
        found->current = update.transform;
      }
    }
    update();
    return;
  }

  if (interaction_ == Interaction::resize_uniform) {
    const QPointF initial_handle = gesture_start_view_;
    const QPointF initial_vector = initial_handle - gesture_fixed_view_;
    const QPointF moving_document = viewToDocument(view_point);
    const core::Point snapped_moving = snapDocumentPoint(
        {moving_document.x(), moving_document.y()}, modifiers);
    const QPointF current_vector =
        documentToView(QPointF(snapped_moving.x, snapped_moving.y)) - gesture_fixed_view_;
    if (initial_vector.x() * current_vector.x() <= 0.0 ||
        initial_vector.y() * current_vector.y() <= 0.0) {
      return;
    }
    const double initial_length = std::hypot(initial_vector.x(), initial_vector.y());
    const double current_length = std::hypot(current_vector.x(), current_vector.y());
    if (initial_length <= kGeometryEpsilon || current_length <= kGeometryEpsilon) {
      return;
    }
    const double scale = current_length / initial_length;
    const QPointF fixed_document = viewToDocument(gesture_fixed_view_);
    const auto transformed = interaction::resizeSelectionUniform(
        gesture_items_, {fixed_document.x(), fixed_document.y()}, scale);
    if (!transformed.has_value()) {
      return;
    }
    for (const auto& update : *transformed) {
      const auto found = std::ranges::find(preview_transforms_, update.node_id,
                                           &PreviewTransform::node_id);
      if (found != preview_transforms_.end()) {
        found->current = update.transform;
      }
    }
    update();
    return;
  }

  const QPointF moving_document = viewToDocument(view_point);
  const core::Point snapped_moving = snapDocumentPoint(
      {moving_document.x(), moving_document.y()}, modifiers);
  const auto& item = gesture_items_.front();
  const auto& rectangle = std::get<core::Rectangle>(item.primitive);
  std::optional<core::Transform> transformed;
  if (gesture_is_edge_) {
    transformed = interaction::resizeRectangleEdgeNonUniform(
        rectangle, item.transform, gesture_fixed_local_, gesture_moving_local_,
        snapped_moving);
  } else {
    transformed = interaction::resizeRectangleNonUniform(
        rectangle, item.transform, gesture_fixed_local_, gesture_moving_local_,
        snapped_moving);
  }
  if (transformed.has_value()) {
    preview_transforms_.front().current = *transformed;
    update();
  }
}

void CanvasView::commitTransformPreview() {
  const std::vector<core::TransformUpdate> updates = [&] {
    std::vector<core::TransformUpdate> result;
    result.reserve(preview_transforms_.size());
    for (const auto& preview : preview_transforms_) {
      result.push_back({preview.node_id, preview.current});
    }
    return result;
  }();
  preview_transforms_.clear();
  gesture_items_.clear();
  interaction_ = Interaction::none;
  const auto result = history_.applyTransforms(updates);
  if (result.accepted && result.changed) {
    refreshFromDocument();
    emit documentChanged();
  } else {
    update();
  }
}

const geometry::EvaluatedCircle* CanvasView::evaluatedCircle(const core::NodeId node_id) const {
  const auto found = std::ranges::find(evaluation_.circles, node_id, &geometry::EvaluatedCircle::node_id);
  return found == evaluation_.circles.end() ? nullptr : &*found;
}

std::optional<CanvasView::RegionHit> CanvasView::regionAt(const QPointF point) const {
  std::vector<core::NodeId> candidates;
  if (selected_node_ids_.size() == 1) {
    if (const auto context = regionContextForNode(selected_node_ids_.front());
        context.has_value()) {
      candidates.push_back(*context);
    }
  }
  for (auto split = evaluation_.splits.rbegin(); split != evaluation_.splits.rend(); ++split) {
    if (std::ranges::find(candidates, split->node_id) == candidates.end()) {
      candidates.push_back(split->node_id);
    }
  }

  const QPointF view_point = documentToView(point);
  const auto to_view = [this](const QPointF value) { return documentToView(value); };
  for (const auto split_node_id : candidates) {
    const auto split = std::ranges::find_if(
        evaluation_.splits,
        [split_node_id](const geometry::EvaluatedSplit& value) {
          return value.node_id == split_node_id;
        });
    if (split == evaluation_.splits.end() ||
        split->status != geometry::SplitStatus::success) {
      continue;
    }
    // A split chord belongs to both adjacent cells.  Treat the whole
    // tolerance band as an ambiguous boundary hit and resolve it by the
    // evaluator's canonical RegionKey order (cells are key-sorted).
    for (const auto& region : split->cells) {
      const auto path = evaluatedRegionPath(region, to_view);
      if (path.isEmpty()) {
        continue;
      }
      QPainterPathStroker stroker;
      // This width is in view coordinates, so it remains a screen-pixel
      // tolerance regardless of document zoom or pan.
      stroker.setWidth(2.0 * kHitTolerancePixels);
      if (stroker.createStroke(path).contains(view_point)) {
        return RegionHit{split->node_id, region.key};
      }
    }
    for (const auto& region : split->cells) {
      const auto path = evaluatedRegionPath(region, to_view);
      if (!path.isEmpty() && path.contains(view_point)) {
        return RegionHit{split->node_id, region.key};
      }
    }
  }
  return std::nullopt;
}

std::optional<core::NodeId> CanvasView::regionContextForNode(
    const core::NodeId node_id) const {
  const auto* node = history_.document().findNode(node_id);
  if (node == nullptr) {
    return std::nullopt;
  }
  if (std::holds_alternative<core::SplitNode>(node->definition)) {
    return node_id;
  }
  if (const auto* selection = std::get_if<core::RegionSelectionNode>(&node->definition);
      selection != nullptr) {
    return selection->input;
  }
  if (const auto* filter = std::get_if<core::RegionFilterNode>(&node->definition);
      filter != nullptr) {
    return filter->input;
  }
  return std::nullopt;
}

bool CanvasView::isClosedSplitTarget(const core::NodeId node_id) const {
  if (history_.document().findNode(node_id) == nullptr ||
      std::ranges::any_of(
          evaluation_.diagnostics,
          [node_id](const geometry::EvaluationDiagnostic& diagnostic) {
            return diagnostic.node_id == node_id;
          })) {
    return false;
  }
  if (std::ranges::any_of(
          evaluation_.circles,
          [node_id](const geometry::EvaluatedCircle& circle) {
            return circle.node_id == node_id;
          })) {
    return true;
  }
  if (std::ranges::any_of(
          evaluation_.booleans,
          [node_id](const geometry::EvaluatedBoolean& boolean) {
            return boolean.node_id == node_id;
          })) {
    return true;
  }
  for (const auto& curve_set : evaluation_.curve_sets) {
    if (curve_set.node_id != node_id) {
      continue;
    }
    return isClosedCurveSet(curve_set, nullptr);
  }
  return false;
}

bool CanvasView::validateSplitCandidate(
    const core::NodeId target_node_id,
    const core::SymmetryAxis& axis,
    core::NodeId* candidate_split_id,
    QString* failure_message) const {
  if (candidate_split_id != nullptr) {
    *candidate_split_id = 0;
  }
  if (failure_message != nullptr) {
    failure_message->clear();
  }
  const auto fail = [failure_message](const QString& message) {
    if (failure_message != nullptr) {
      *failure_message = message;
    }
    return false;
  };
  if (!std::isfinite(axis.origin.x) || !std::isfinite(axis.origin.y) ||
      !std::isfinite(axis.direction.x) || !std::isfinite(axis.direction.y) ||
      std::hypot(axis.direction.x, axis.direction.y) <= kGeometryEpsilon) {
    return fail(tr("Draw a split line with a direction"));
  }
  const auto* target = history_.document().findNode(target_node_id);
  if (target == nullptr) {
    return fail(tr("Select a shape before splitting"));
  }
  bool open_arc = false;
  for (const auto& curve_set : evaluation_.curve_sets) {
    if (curve_set.node_id == target_node_id) {
      static_cast<void>(isClosedCurveSet(curve_set, &open_arc));
      break;
    }
  }
  if (!isClosedSplitTarget(target_node_id)) {
    if (const auto* primitive = std::get_if<core::PrimitiveNode>(&target->definition);
        primitive != nullptr) {
      if (const auto* arc = std::get_if<core::Arc>(&primitive->primitive);
          arc != nullptr && std::abs(arc->sweep_degrees) != 360.0) {
        open_arc = true;
      }
    }
    return fail(open_arc ? tr("Split needs a closed shape")
                         : tr("Cannot read the selected shape"));
  }

  core::Document candidate = history_.document();
  try {
    const core::NodeId split_id = candidate.addSplit(
        "Split",
        target_node_id,
        axis.origin,
        axis.direction);
    const auto candidate_evaluation = geometry::DocumentEvaluator::evaluate(candidate);
    const auto split = std::ranges::find_if(
        candidate_evaluation.splits,
        [split_id](const geometry::EvaluatedSplit& value) { return value.node_id == split_id; });
    if (split == candidate_evaluation.splits.end()) {
      return fail(tr("The split result is unavailable"));
    }
    if (split->status != geometry::SplitStatus::success) {
      return fail(splitStatusMessage(split->status));
    }
    if (split->cells.empty()) {
      return fail(tr("The split produced no regions"));
    }
    const auto diagnostic = std::ranges::find_if(
        candidate_evaluation.diagnostics,
        [split_id](const geometry::EvaluationDiagnostic& value) {
          return value.node_id == split_id;
        });
    if (diagnostic != candidate_evaluation.diagnostics.end()) {
      return fail(QString::fromStdString(diagnostic->reason));
    }
    if (candidate_split_id != nullptr) {
      *candidate_split_id = split_id;
    }
    return true;
  } catch (const std::exception& error) {
    return fail(
        tr("Cannot split this shape: %1").arg(QString::fromUtf8(error.what())));
  }
}

std::optional<QLineF> CanvasView::splitPreviewLine() const {
  if (!split_preview_axis_.has_value() || width() <= 0 || height() <= 0) {
    return std::nullopt;
  }
  const QPointF origin = documentToView(
      QPointF(split_preview_axis_->origin.x, split_preview_axis_->origin.y));
  const QPointF direction(
      split_preview_axis_->direction.x * zoom_,
      -split_preview_axis_->direction.y * zoom_);
  if (!std::isfinite(origin.x()) || !std::isfinite(origin.y()) ||
      !std::isfinite(direction.x()) || !std::isfinite(direction.y()) ||
      std::hypot(direction.x(), direction.y()) <= kGeometryEpsilon) {
    return std::nullopt;
  }

  double minimum = -std::numeric_limits<double>::infinity();
  double maximum = std::numeric_limits<double>::infinity();
  const auto clip = [&minimum, &maximum](
                        const double origin_component,
                        const double direction_component,
                        const double lower,
                        const double upper) {
    if (std::abs(direction_component) <= kGeometryEpsilon) {
      return origin_component >= lower && origin_component <= upper;
    }
    double first = (lower - origin_component) / direction_component;
    double second = (upper - origin_component) / direction_component;
    if (first > second) {
      std::swap(first, second);
    }
    minimum = std::max(minimum, first);
    maximum = std::min(maximum, second);
    return minimum <= maximum;
  };
  if (!clip(origin.x(), direction.x(), 0.0, static_cast<double>(width())) ||
      !clip(origin.y(), direction.y(), 0.0, static_cast<double>(height())) ||
      !std::isfinite(minimum) || !std::isfinite(maximum)) {
    return std::nullopt;
  }
  return QLineF(origin + minimum * direction, origin + maximum * direction);
}

std::optional<core::Transform> CanvasView::previewTransform(const core::NodeId node_id) const {
  const auto found = std::ranges::find(
      preview_transforms_, node_id, &CanvasView::PreviewTransform::node_id);
  if (found == preview_transforms_.end()) {
    return std::nullopt;
  }
  return found->current;
}

bool CanvasView::selectionIsPrimitive() const {
  if (selected_node_ids_.empty()) {
    return false;
  }
  return std::ranges::all_of(selected_node_ids_, [this](const core::NodeId node_id) {
    const auto* node = history_.document().findNode(node_id);
    return node != nullptr && std::holds_alternative<core::PrimitiveNode>(node->definition);
  });
}

std::vector<core::NodeId> CanvasView::normalizedSelection(
    const std::vector<core::NodeId>& node_ids) const {
  std::unordered_set<core::NodeId> requested;
  requested.reserve(node_ids.size());
  for (const core::NodeId node_id : node_ids) {
    if (history_.document().findNode(node_id) != nullptr) {
      requested.insert(node_id);
    }
  }
  std::vector<core::NodeId> result;
  result.reserve(requested.size());
  for (const auto& node : history_.document().nodes()) {
    if (requested.contains(node.id)) {
      result.push_back(node.id);
    }
  }
  return result;
}

void CanvasView::beginPrimitiveDrag(
    const QPointF document_point,
    const Qt::KeyboardModifiers modifiers) {
  if (!selectionIsPrimitive()) {
    emit statusMessage(tr("Select shapes of the same kind to move them together"));
    return;
  }
  std::vector<PreviewTransform> previews;
  previews.reserve(selected_node_ids_.size());
  for (const core::NodeId node_id : selected_node_ids_) {
    const auto* node = history_.document().findNode(node_id);
    const auto* primitive = node == nullptr ? nullptr : std::get_if<core::PrimitiveNode>(&node->definition);
    if (primitive == nullptr) {
      emit statusMessage(tr("Select shapes of the same kind to move them together"));
      return;
    }
    previews.push_back(PreviewTransform{node_id, primitive->transform, primitive->transform});
  }
  interaction_ = Interaction::drag;
  Q_UNUSED(modifiers)
  interaction_start_document_ = document_point;
  snap_guide_.reset();
  preview_transforms_ = std::move(previews);
  update();
}

void CanvasView::updateDragPreview(
    const QPointF document_point,
    const Qt::KeyboardModifiers modifiers) {
  if (interaction_ != Interaction::drag) {
    return;
  }
  const auto snapped = snapDocumentPoint(
      {document_point.x(), document_point.y()}, modifiers);
  const QPointF delta{
      snapped.x - interaction_start_document_.x(),
      snapped.y - interaction_start_document_.y()};
  for (auto& preview : preview_transforms_) {
    preview.current.translation.x = preview.origin.translation.x + delta.x();
    preview.current.translation.y = preview.origin.translation.y + delta.y();
  }
}

void CanvasView::updatePlacementPreview(
    const QPointF document_point,
    const Qt::KeyboardModifiers modifiers) {
  const core::Point snapped = snapDocumentPoint(
      {document_point.x(), document_point.y()}, modifiers);
  placement_cursor_ = snapped;
  if (placement_points_.empty()) {
    placement_preview_.reset();
    return;
  }
  const core::Point first = placement_points_.front();
  if (interaction_ == Interaction::place_circle) {
    placement_preview_ = interaction::placeCircle(first, *placement_cursor_);
  } else if (interaction_ == Interaction::place_rectangle) {
    placement_preview_ = interaction::placeAxisAlignedRectangle(first, *placement_cursor_);
  } else if (interaction_ == Interaction::place_golden) {
    placement_preview_ = interaction::placeGoldenByCenterAndShortSideVector(
        first, core::Point{placement_cursor_->x - first.x, placement_cursor_->y - first.y});
  } else if (interaction_ == Interaction::place_arc && placement_points_.size() >= 3) {
    placement_preview_ = interaction::placeThreePointArc(
        placement_points_[0], placement_points_[1], placement_points_[2]);
  } else {
    placement_preview_.reset();
  }
}

void CanvasView::commitPlacement() {
  if (!placement_preview_.has_value()) {
    emit statusMessage(tr("Drag farther to place the shape"));
    cancelTransientInteraction();
    return;
  }

  const interaction::PrimitivePlacement placement = *placement_preview_;
  std::string name;
  std::visit(
      [&name](const auto& primitive) {
        using Primitive = std::decay_t<decltype(primitive)>;
        if constexpr (std::is_same_v<Primitive, core::Circle>) {
          name = "Circle";
        } else if constexpr (std::is_same_v<Primitive, core::Rectangle>) {
          name = "Rectangle";
        } else if constexpr (std::is_same_v<Primitive, core::Arc>) {
          name = "Arc";
        } else if constexpr (std::is_same_v<Primitive, core::GoldenRectangle>) {
          name = "Golden Rectangle";
        } else {
          name = "Primitive";
        }
      },
      placement.primitive);
  cancelTransientInteraction();
  const core::NodeId node_id =
      history_.addPrimitive(std::move(name), placement.primitive, placement.transform);
  refreshFromDocument();
  setSelectedNode(node_id);
  emit documentChanged();
}

void CanvasView::beginSplit(const QPointF document_point) {
  if (selected_node_ids_.size() != 1) {
    emit statusMessage(tr("Select one closed shape before splitting"));
    return;
  }
  const core::NodeId target_node_id = selected_node_ids_.front();
  const auto* target = history_.document().findNode(target_node_id);
  bool open_arc = false;
  if (target != nullptr) {
    for (const auto& curve_set : evaluation_.curve_sets) {
      if (curve_set.node_id == target_node_id) {
        static_cast<void>(isClosedCurveSet(curve_set, &open_arc));
        break;
      }
    }
    if (const auto* primitive = std::get_if<core::PrimitiveNode>(&target->definition);
        primitive != nullptr) {
      if (const auto* arc = std::get_if<core::Arc>(&primitive->primitive);
          arc != nullptr && std::abs(arc->sweep_degrees) != 360.0) {
        open_arc = true;
      }
    }
  }
  if (!isClosedSplitTarget(target_node_id)) {
    emit statusMessage(open_arc ? tr("Split needs a closed shape")
                                : tr("Cannot read the selected shape"));
    return;
  }
  interaction_ = Interaction::split;
  interaction_start_document_ = document_point;
  split_target_node_id_ = target_node_id;
  split_preview_axis_ = core::SymmetryAxis{
      {document_point.x(), document_point.y()},
      {0.0, 0.0}};
  update();
}

void CanvasView::updateSplitPreview(const QPointF document_point) {
  if (interaction_ != Interaction::split || !split_preview_axis_.has_value()) {
    return;
  }
  split_preview_axis_->direction = {
      document_point.x() - split_preview_axis_->origin.x,
      document_point.y() - split_preview_axis_->origin.y};
  update();
}

void CanvasView::commitSplit() {
  if (interaction_ != Interaction::split || !split_target_node_id_.has_value() ||
      !split_preview_axis_.has_value()) {
    cancelTransientInteraction();
    return;
  }
  const auto axis = *split_preview_axis_;
  const auto target_node_id = *split_target_node_id_;
  core::NodeId candidate_split_id = 0;
  QString failure_message;
  if (!validateSplitCandidate(
          target_node_id, axis, &candidate_split_id, &failure_message)) {
    emit statusMessage(failure_message.isEmpty()
                           ? tr("Split cancelled")
                           : failure_message);
    cancelTransientInteraction();
    return;
  }

  core::NodeId split_node_id = 0;
  try {
    split_node_id = history_.addSplit(
        "Split",
        target_node_id,
        axis.origin,
        axis.direction);
  } catch (const std::exception& error) {
    emit statusMessage(
        tr("Cannot apply the split: %1").arg(QString::fromUtf8(error.what())));
    cancelTransientInteraction();
    return;
  }
  if (split_node_id == 0) {
    emit statusMessage(tr("Cannot apply the split result"));
    cancelTransientInteraction();
    return;
  }
  cancelTransientInteraction();
  refreshFromDocument();
  setSelectedNode(split_node_id);
  emit documentChanged();
}

void CanvasView::toggleRegionSelection(
    const RegionHit& hit,
    const Qt::KeyboardModifiers modifiers) {
  const bool extending = modifiers.testFlag(Qt::ShiftModifier) ||
                         modifiers.testFlag(Qt::ControlModifier) ||
                         modifiers.testFlag(Qt::MetaModifier);
  bool changed = false;
  if (selected_region_split_id_ != std::optional<core::NodeId>(hit.split_node_id)) {
    selected_region_split_id_ = hit.split_node_id;
    selected_region_keys_.clear();
    changed = true;
  }
  const auto found = std::ranges::find(selected_region_keys_, hit.key);
  if (found != selected_region_keys_.end()) {
    selected_region_keys_.erase(found);
    changed = true;
  } else {
    if (!extending && !selected_region_keys_.empty()) {
      selected_region_keys_.clear();
    }
    selected_region_keys_.push_back(hit.key);
    std::ranges::sort(selected_region_keys_);
    changed = true;
  }
  if (selected_region_keys_.empty()) {
    selected_region_split_id_.reset();
  }
  if (changed) {
    emit selectionChanged();
    update();
  }
}

void CanvasView::clearRegionSelection() {
  selected_region_split_id_.reset();
  selected_region_keys_.clear();
}

void CanvasView::deleteSelectedRegions() {
  if (interaction_ != Interaction::none || selected_region_keys_.empty()) {
    return;
  }
  if (!selected_region_split_id_.has_value() ||
      history_.document().findNode(*selected_region_split_id_) == nullptr) {
    clearRegionSelection();
    emit selectionChanged();
    emit statusMessage(tr("Those regions are no longer available"));
    return;
  }
  const core::NodeId split_node_id = *selected_region_split_id_;
  const auto keys = selected_region_keys_;
  const auto result = history_.addRegionSelectionAndFilter(
      "Selected Regions",
      "Filtered Regions",
      split_node_id,
      keys,
      core::RegionFilterMode::remove_selected);
  if (!result.accepted) {
    if (result.rejection == core::RegionFilterRejection::invalid_region_key ||
        result.rejection == core::RegionFilterRejection::invalid_split) {
      clearRegionSelection();
      emit selectionChanged();
    }
    emit statusMessage(tr("Cannot keep the selected regions"));
    return;
  }
  refreshFromDocument();
  setSelectedNode(split_node_id);
  emit documentChanged();
}

void CanvasView::cancelTransientInteraction() {
  if (interaction_ == Interaction::pan) {
    pan_offset_ = pan_origin_;
  }
  interaction_ = Interaction::none;
  preview_transforms_.clear();
  gesture_items_.clear();
  gesture_is_edge_ = false;
  placement_points_.clear();
  placement_cursor_.reset();
  placement_preview_.reset();
  split_preview_axis_.reset();
  split_target_node_id_.reset();
  snap_guide_.reset();
  update();
}

void CanvasView::commitKeyboardMove(const QPointF delta) {
  if (selected_node_ids_.empty() || !selectionIsPrimitive()) {
    return;
  }
  std::vector<core::TransformUpdate> updates;
  updates.reserve(selected_node_ids_.size());
  for (const core::NodeId node_id : selected_node_ids_) {
    const auto* node = history_.document().findNode(node_id);
    const auto* primitive = node == nullptr ? nullptr : std::get_if<core::PrimitiveNode>(&node->definition);
    if (primitive == nullptr) {
      return;
    }
    auto transform = primitive->transform;
    transform.translation.x += delta.x();
    transform.translation.y += delta.y();
    updates.push_back({node_id, transform});
  }
  const auto result = history_.applyTransforms(updates);
  if (result.accepted && result.changed) {
    refreshFromDocument();
    emit documentChanged();
  }
}

void CanvasView::duplicateSelection() {
  if (interaction_ != Interaction::none || selected_node_ids_.empty()) {
    return;
  }
  const auto result = history_.duplicateSelected(selected_node_ids_);
  if (!result.accepted) {
    emit statusMessage(tr("Cannot duplicate the selected shapes"));
    return;
  }
  if (result.mapping.empty()) {
    return;
  }
  std::vector<core::NodeId> duplicates;
  duplicates.reserve(result.mapping.size());
  for (const auto& [source, duplicate] : result.mapping) {
    Q_UNUSED(source)
    duplicates.push_back(duplicate);
  }
  refreshFromDocument();
  setSelectedNodes(std::move(duplicates));
  emit documentChanged();
}

void CanvasView::deleteSelection() {
  if (hasSelectedRegions()) {
    deleteSelectedRegions();
    return;
  }
  if (interaction_ != Interaction::none || selected_node_ids_.empty()) {
    return;
  }
  const auto result = history_.removeSelected(
      selected_node_ids_, core::RemovePolicy::reject_if_referenced);
  if (!result.accepted) {
    emit statusMessage(tr("This shape is used by another shape"));
    return;
  }
  if (result.removed_ids.empty()) {
    return;
  }
  setSelectedNodes({});
  refreshFromDocument();
  emit documentChanged();
}

void CanvasView::flipSelectionHorizontal() {
  if (interaction_ != Interaction::none || selected_node_ids_.size() != 1 ||
      !selectionIsPrimitive()) {
    emit statusMessage(tr("Select one shape before flipping"));
    return;
  }
  const auto axis = selectionSymmetryAxis(interaction::ReflectionAxis::vertical);
  if (!axis.has_value()) {
    emit statusMessage(tr("Cannot find the shape's vertical axis"));
    return;
  }
  const core::NodeId input = selected_node_ids_.front();
  const core::NodeId symmetry = history_.addSymmetry("Flip Horizontal", input, *axis);
  refreshFromDocument();
  setSelectedNode(symmetry);
  emit documentChanged();
}

void CanvasView::flipSelectionVertical() {
  if (interaction_ != Interaction::none || selected_node_ids_.size() != 1 ||
      !selectionIsPrimitive()) {
    emit statusMessage(tr("Select one shape before flipping"));
    return;
  }
  const auto axis = selectionSymmetryAxis(interaction::ReflectionAxis::horizontal);
  if (!axis.has_value()) {
    emit statusMessage(tr("Cannot find the shape's horizontal axis"));
    return;
  }
  const core::NodeId input = selected_node_ids_.front();
  const core::NodeId symmetry = history_.addSymmetry("Flip Vertical", input, *axis);
  refreshFromDocument();
  setSelectedNode(symmetry);
  emit documentChanged();
}

QPointF CanvasView::documentToView(const QPointF point) const {
  return QPointF(width() / 2.0 + pan_offset_.x() + point.x() * zoom_,
                 height() / 2.0 + pan_offset_.y() - point.y() * zoom_);
}

QPointF CanvasView::viewToDocument(const QPointF point) const {
  return QPointF((point.x() - width() / 2.0 - pan_offset_.x()) / zoom_,
                 (height() / 2.0 + pan_offset_.y() - point.y()) / zoom_);
}

}  // namespace signet::ui
