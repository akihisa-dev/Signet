// SPDX-License-Identifier: AGPL-3.0-or-later
#include "geometry/arrangement_model.h"

#include <CGAL/Arr_naive_point_location.h>
#include <CGAL/Arr_point_location_result.h>
#include <CGAL/intersections.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>

namespace signet::geometry {

namespace {

using ValidationKernel = CGAL::Exact_predicates_exact_constructions_kernel;
using ValidationTraits = CGAL::Arr_circle_segment_traits_2<ValidationKernel>;
using ValidationArrangement = CGAL::Arrangement_with_history_2<ValidationTraits>;

bool finite(const double value) { return std::isfinite(value); }

void validateCurveInput(const CurveInput& curve) {
  std::visit(
      [](const auto& input) {
        using Input = std::decay_t<decltype(input)>;
        if constexpr (std::is_same_v<Input, CircleInput>) {
          if (!finite(input.center_x) || !finite(input.center_y) || !finite(input.radius) ||
              input.radius <= 0.0) {
            throw std::invalid_argument(
                "Circles require finite coordinates and a positive radius");
          }
        } else if constexpr (std::is_same_v<Input, SegmentInput>) {
          if (!finite(input.source_x) || !finite(input.source_y) || !finite(input.target_x) ||
              !finite(input.target_y) ||
              (input.source_x == input.target_x && input.source_y == input.target_y)) {
            throw std::invalid_argument("Segments require finite, distinct endpoints");
          }
        } else {
          if (!finite(input.source_x) || !finite(input.source_y) || !finite(input.interior_x) ||
              !finite(input.interior_y) || !finite(input.target_x) || !finite(input.target_y) ||
              (input.source_x == input.target_x && input.source_y == input.target_y) ||
              (input.source_x == input.interior_x && input.source_y == input.interior_y) ||
              (input.interior_x == input.target_x && input.interior_y == input.target_y)) {
            throw std::invalid_argument("Arcs require three finite, distinct points");
          }
          const ValidationKernel::Point_2 source(input.source_x, input.source_y);
          const ValidationKernel::Point_2 interior(input.interior_x, input.interior_y);
          const ValidationKernel::Point_2 target(input.target_x, input.target_y);
          if (CGAL::collinear(source, interior, target)) {
            throw std::invalid_argument("Arc points must not be collinear");
          }
        }
      },
      curve);
}

void insertValidationCurve(ValidationArrangement& arrangement, const CurveInput& curve) {
  using Point = ValidationKernel::Point_2;
  std::visit(
      [&arrangement](const auto& input) {
        using Input = std::decay_t<decltype(input)>;
        if constexpr (std::is_same_v<Input, CircleInput>) {
          const Point center(input.center_x, input.center_y);
          const ValidationKernel::FT radius(input.radius);
          const ValidationKernel::Circle_2 circle(center, radius * radius);
          CGAL::insert(arrangement, typename ValidationTraits::Curve_2(circle));
        } else if constexpr (std::is_same_v<Input, SegmentInput>) {
          const Point source(input.source_x, input.source_y);
          const Point target(input.target_x, input.target_y);
          CGAL::insert(arrangement, typename ValidationTraits::Curve_2(source, target));
        } else {
          const Point source(input.source_x, input.source_y);
          const Point interior(input.interior_x, input.interior_y);
          const Point target(input.target_x, input.target_y);
          CGAL::insert(
              arrangement,
              typename ValidationTraits::Curve_2(source, interior, target));
        }
      },
      curve);
}

void validateBooleanOperand(const std::span<const CurveInput> curves) {
  if (curves.empty()) {
    throw std::invalid_argument("Boolean operands require non-empty closed curve sets");
  }

  for (const auto& curve : curves) {
    validateCurveInput(curve);
  }

  const auto full_circle_count = static_cast<std::size_t>(std::ranges::count_if(
      curves,
      [](const CurveInput& curve) { return std::holds_alternative<CircleInput>(curve); }));
  if (full_circle_count != 0) {
    if (full_circle_count == 1 && curves.size() == 1) {
      return;
    }
    throw std::invalid_argument(
        "Boolean operand must contain one closed contour; full circles cannot be mixed");
  }

  // A second curve from the same operand on an arrangement edge is not a
  // second crossing. Reject it here so face propagation never depends on
  // source-history multiplicity. Curves from the other operand are checked
  // independently and are therefore intentionally allowed to coincide.
  ValidationArrangement validation_arrangement;
  for (const auto& curve : curves) {
    insertValidationCurve(validation_arrangement, curve);
  }
  for (auto edge = validation_arrangement.edges_begin();
       edge != validation_arrangement.edges_end();
       ++edge) {
    if (validation_arrangement.number_of_originating_curves(edge) > 1) {
      throw std::invalid_argument(
          "Boolean operand contains coincident or overlapping curves");
    }
  }

  using Point = ValidationKernel::Point_2;
  struct Endpoint final {
    Point source;
    Point target;
  };
  std::vector<Endpoint> endpoints;
  endpoints.reserve(curves.size());
  for (const auto& curve : curves) {
    std::visit(
        [&endpoints](const auto& input) {
          using Input = std::decay_t<decltype(input)>;
          if constexpr (!std::is_same_v<Input, CircleInput>) {
            endpoints.push_back(Endpoint{
                Point{input.source_x, input.source_y},
                Point{input.target_x, input.target_y}});
          }
        },
        curve);
  }

  struct Vertex final {
    Point point;
    std::size_t degree{};
    std::vector<std::size_t> neighbors;
  };
  std::vector<Vertex> vertices;
  vertices.reserve(endpoints.size());
  const auto vertex_index = [&vertices](const Point& point) {
    const auto found = std::ranges::find(vertices, point, &Vertex::point);
    if (found != vertices.end()) {
      return static_cast<std::size_t>(found - vertices.begin());
    }
    vertices.push_back(Vertex{point, 0, {}});
    return vertices.size() - 1;
  };

  for (const auto& endpoint : endpoints) {
    const std::size_t source = vertex_index(endpoint.source);
    const std::size_t target = vertex_index(endpoint.target);
    ++vertices[source].degree;
    ++vertices[target].degree;
    vertices[source].neighbors.push_back(target);
    vertices[target].neighbors.push_back(source);
  }

  if (std::ranges::any_of(vertices, [](const Vertex& vertex) {
        return vertex.degree != 2;
      })) {
    if (curves.size() == 1 && std::holds_alternative<ArcInput>(curves.front())) {
      throw std::invalid_argument(
          "Boolean operand is an open Arc; only closed curve sets are supported");
    }
    throw std::invalid_argument(
        "Boolean operand curve set is open or branched; endpoints must form a closed contour");
  }

  std::vector<bool> visited(vertices.size(), false);
  std::vector<std::size_t> pending{0};
  visited[0] = true;
  while (!pending.empty()) {
    const std::size_t current = pending.back();
    pending.pop_back();
    for (const std::size_t neighbor : vertices[current].neighbors) {
      if (!visited[neighbor]) {
        visited[neighbor] = true;
        pending.push_back(neighbor);
      }
    }
  }
  if (std::ranges::any_of(visited, [](const bool value) { return !value; })) {
    throw std::invalid_argument(
        "Boolean operand curve set must be one connected closed contour");
  }
}

bool selected(const BooleanOperation operation, const bool inside_left, const bool inside_right) {
  switch (operation) {
    case BooleanOperation::unite:
      return inside_left || inside_right;
    case BooleanOperation::intersect:
      return inside_left && inside_right;
    case BooleanOperation::subtract:
      return inside_left && !inside_right;
    case BooleanOperation::exclusive_or:
      return inside_left != inside_right;
  }
  return false;
}

BoundaryPoint pointDto(const auto& point) {
  return BoundaryPoint{CGAL::to_double(point.x()), CGAL::to_double(point.y())};
}

BoundaryDirection opposite(const BoundaryDirection direction) {
  return direction == BoundaryDirection::counterclockwise
             ? BoundaryDirection::clockwise
             : BoundaryDirection::counterclockwise;
}

struct CircleLineIntersection final {
  ValidationTraits::Point_2 point;
  bool tangent{};
};

std::vector<CircleLineIntersection> intersectLineCircle(
    const ValidationKernel::Line_2& line,
    const ValidationKernel::Circle_2& circle) {
  using NT = ValidationKernel::FT;
  using Point = ValidationTraits::Point_2;
  using Coordinate = Point::CoordNT;

  const NT line_factor = CGAL::square(line.a()) + CGAL::square(line.b());
  const NT line_value = line.a() * circle.center().x() +
                        line.b() * circle.center().y() + line.c();
  const NT discriminant = line_factor * circle.squared_radius() -
                          CGAL::square(line_value);
  const CGAL::Sign discriminant_sign = CGAL::sign(discriminant);
  if (discriminant_sign == CGAL::NEGATIVE) {
    return {};
  }

  const NT aux = line.b() * circle.center().x() - line.a() * circle.center().y();
  const NT x_base = (aux * line.b() - line.a() * line.c()) / line_factor;
  const NT y_base = (-aux * line.a() - line.b() * line.c()) / line_factor;
  if (discriminant_sign == CGAL::ZERO) {
    return {CircleLineIntersection{Point{x_base, y_base}, true}};
  }

  const NT x_root_coeff = line.b() / line_factor;
  const NT y_root_coeff = line.a() / line_factor;
  const bool minus_root_first = CGAL::sign(line.b()) == CGAL::POSITIVE;
  const Point first{
      Coordinate{x_base, minus_root_first ? -x_root_coeff : x_root_coeff, discriminant},
      Coordinate{y_base, minus_root_first ? y_root_coeff : -y_root_coeff, discriminant}};
  const Point second{
      Coordinate{x_base, minus_root_first ? x_root_coeff : -x_root_coeff, discriminant},
      Coordinate{y_base, minus_root_first ? -y_root_coeff : y_root_coeff, discriminant}};
  return {CircleLineIntersection{first, false}, CircleLineIntersection{second, false}};
}

ValidationTraits::Curve_2 makeCircularCurve(const CurveInput& input) {
  using Point = ValidationKernel::Point_2;
  return std::visit(
      [](const auto& value) -> ValidationTraits::Curve_2 {
        using Input = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Input, CircleInput>) {
          const Point center{value.center_x, value.center_y};
          const ValidationKernel::FT radius{value.radius};
          return ValidationTraits::Curve_2{
              ValidationKernel::Circle_2{center, radius * radius}};
        } else if constexpr (std::is_same_v<Input, ArcInput>) {
          return ValidationTraits::Curve_2{
              Point{value.source_x, value.source_y},
              Point{value.interior_x, value.interior_y},
              Point{value.target_x, value.target_y}};
        } else {
          throw std::logic_error("Expected a circular curve");
        }
      },
      input);
}

bool circularCurveContains(
    const ValidationTraits::Curve_2& curve,
    const ValidationTraits::Point_2& point) {
  if (curve.is_full()) {
    return true;
  }

  using Piece = std::variant<ValidationTraits::Point_2, ValidationTraits::X_monotone_curve_2>;
  std::vector<Piece> pieces;
  ValidationTraits traits;
  traits.make_x_monotone_2_object()(curve, std::back_inserter(pieces));
  return std::ranges::any_of(pieces, [&point](const Piece& piece) {
    const auto* monotone = std::get_if<ValidationTraits::X_monotone_curve_2>(&piece);
    return monotone != nullptr && monotone->is_in_x_range(point) &&
           monotone->point_position(point) == CGAL::EQUAL;
  });
}

RegionBoundaryProvenanceLoop canonicalProvenanceLoop(
    const RegionBoundaryProvenanceLoop& sequence) {
  if (sequence.empty()) {
    return {};
  }

  const auto minimumRotation = [](const RegionBoundaryProvenanceLoop& source) {
    RegionBoundaryProvenanceLoop best = source;
    for (std::size_t offset = 1; offset < source.size(); ++offset) {
      RegionBoundaryProvenanceLoop candidate;
      candidate.reserve(source.size());
      for (std::size_t index = 0; index < source.size(); ++index) {
        candidate.push_back(source[(offset + index) % source.size()]);
      }
      if (candidate < best) {
        best = std::move(candidate);
      }
    }
    return best;
  };

  const auto forward = minimumRotation(sequence);
  RegionBoundaryProvenanceLoop reversed(sequence.rbegin(), sequence.rend());
  const auto backward = minimumRotation(reversed);
  return backward < forward ? backward : forward;
}

std::vector<RegionBoundaryProvenanceLoop> canonicalProvenance(
    const FaceClassification& face,
    const std::size_t source_curve_count,
    const std::vector<std::size_t>& group_offsets,
    const std::span<const SplitChord> chords) {
  const auto operandFor = [&group_offsets, source_curve_count](const std::size_t source) {
    if (source >= source_curve_count) {
      throw std::logic_error("Source provenance index exceeds the source curve count");
    }
    const auto upper = std::upper_bound(group_offsets.begin(), group_offsets.end(), source);
    if (upper == group_offsets.begin() || upper == group_offsets.end()) {
      throw std::logic_error("Source provenance index is outside operand offsets");
    }
    return static_cast<std::size_t>(upper - group_offsets.begin() - 1);
  };

  const auto makeLoop = [&](const BoundaryLoop& loop) {
    RegionBoundaryProvenanceLoop provenance;
    for (const auto& edge : loop.edges) {
      for (const std::size_t source : edge.source_curve_indices) {
        if (source < source_curve_count) {
          provenance.push_back(RegionBoundaryProvenance{
              RegionBoundaryProvenanceKind::source_curve,
              operandFor(source),
              source,
              std::numeric_limits<std::size_t>::max(),
              {},
              {},
              SplitChordSide::on_axis});
        } else {
          throw std::logic_error("Boundary provenance contains an unknown curve");
        }
      }
      for (const std::size_t chord_index : edge.split_chord_indices) {
        if (chord_index >= chords.size()) {
          throw std::logic_error("Boundary provenance contains an unknown split chord");
        }
        const auto& chord = chords[chord_index];
        std::vector<std::size_t> chord_operand_indices;
        chord_operand_indices.reserve(chord.source_curve_indices.size());
        for (const std::size_t source : chord.source_curve_indices) {
          chord_operand_indices.push_back(operandFor(source));
        }
        provenance.push_back(RegionBoundaryProvenance{
            RegionBoundaryProvenanceKind::split_chord,
            std::numeric_limits<std::size_t>::max(),
            std::numeric_limits<std::size_t>::max(),
            chord_index,
            chord.source_curve_indices,
            std::move(chord_operand_indices),
            chord.side});
      }
    }
    return canonicalProvenanceLoop(provenance);
  };

  std::vector<RegionBoundaryProvenanceLoop> result;
  if (!face.outer_boundary.edges.empty()) {
    result.push_back(makeLoop(face.outer_boundary));
  }
  for (const auto& hole : face.holes) {
    if (!hole.edges.empty()) {
      result.push_back(makeLoop(hole));
    }
  }
  std::ranges::sort(result);
  return result;
}

}  // namespace

std::optional<std::vector<bool>> ArrangementModel::membershipAt(
    const Traits::Point_2& point,
    const std::vector<std::size_t>& group_offsets) const {
  using PointLocation = CGAL::Arr_naive_point_location<Arrangement>;
  using Result = CGAL::Arr_point_location_result<Arrangement>::Type;
  PointLocation point_location(arrangement_);
  const Result result = point_location.locate(point);
  const auto* face = std::get_if<typename Arrangement::Face_const_handle>(&result);
  if (face == nullptr) {
    return std::nullopt;
  }

  FaceId face_id = 0;
  FaceId next_id = 1;
  for (auto candidate = arrangement_.faces_begin(); candidate != arrangement_.faces_end();
       ++candidate, ++next_id) {
    if (&*candidate == &**face) {
      face_id = next_id;
      break;
    }
  }
  if (face_id == 0) {
    throw std::logic_error("Point location returned an unknown arrangement face");
  }
  const auto classifications = classifyFaces(
      group_offsets, [](const std::span<const bool>) { return false; });
  const auto found = std::ranges::find_if(
      classifications, [face_id](const FaceClassification& item) {
        return item.face_id == face_id;
      });
  if (found == classifications.end()) {
    throw std::logic_error("Point location face was not classified");
  }
  return found->operand_membership;
}

SplitEvaluationSnapshot ArrangementModel::splitChords(
    std::vector<OperandGroup> source_operand_groups,
    const FaceSelector& selector,
    const SplitAxis axis) {
  SplitEvaluationSnapshot snapshot;
  if (!selector || !finite(axis.origin.x) || !finite(axis.origin.y) ||
      !finite(axis.direction.x) || !finite(axis.direction.y) ||
      (axis.direction.x == 0.0 && axis.direction.y == 0.0) ||
      source_operand_groups.empty()) {
    snapshot.status = SplitStatus::invalid_input;
    return snapshot;
  }

  std::vector<CurveInput> flattened;
  std::vector<std::size_t> group_offsets{0};
  for (const auto& group : source_operand_groups) {
    for (const auto& curve : group) {
      if (std::holds_alternative<ArcInput>(curve)) {
        // ArcInput is supported here only as part of a validated closed
        // contour.  Full circles use CircleInput, as everywhere else in the
        // arrangement API.
      } else if (!std::holds_alternative<SegmentInput>(curve) &&
                 !std::holds_alternative<CircleInput>(curve)) {
        snapshot.status = SplitStatus::invalid_input;
        return snapshot;
      }
      flattened.push_back(curve);
    }
    group_offsets.push_back(flattened.size());
  }
  if (flattened.empty()) {
    snapshot.status = SplitStatus::invalid_input;
    return snapshot;
  }

  ArrangementModel candidate;
  try {
    candidate.setBooleanOperandGroups(source_operand_groups);
  } catch (const std::invalid_argument&) {
    snapshot.status = SplitStatus::invalid_input;
    return snapshot;
  }

  using Point = Kernel::Point_2;
  using Segment = Kernel::Segment_2;
  using Line = Kernel::Line_2;
  const bool reverse_axis = axis.direction.x < 0.0 ||
                            (axis.direction.x == 0.0 && axis.direction.y < 0.0);
  const Point origin{Kernel::FT{axis.origin.x}, Kernel::FT{axis.origin.y}};
  const Kernel::FT direction_sign = reverse_axis ? Kernel::FT{-1} : Kernel::FT{1};
  const Point direction_point{
      origin.x() + direction_sign * Kernel::FT{axis.direction.x},
      origin.y() + direction_sign * Kernel::FT{axis.direction.y}};
  const auto direction = direction_point - origin;
  const Line split_line{origin, direction_point};
  using ChordPoint = Traits::Point_2;
  struct Hit final {
    ChordPoint point;
    std::vector<std::size_t> sources;
    bool endpoint{};
    bool tangent{};
  };
  struct ChordGeometry final {
    ChordPoint source;
    ChordPoint target;
  };

  const auto makeCellSide = [&split_line](const auto face) {
    bool has_negative = false;
    bool has_positive = false;
    const auto inspect = [&split_line, &has_negative, &has_positive](const auto& ccb) {
      const auto start = ccb;
      auto current = start;
      do {
        const auto& point = current->source()->point();
        const auto value = split_line.a() * point.x() + split_line.b() * point.y() +
                           split_line.c();
        switch (CGAL::sign(value)) {
          case CGAL::NEGATIVE:
            has_negative = true;
            break;
          case CGAL::POSITIVE:
            has_positive = true;
            break;
          case CGAL::ZERO:
            break;
        }
        ++current;
      } while (current != start);
    };

    for (auto ccb = face->outer_ccbs_begin(); ccb != face->outer_ccbs_end(); ++ccb) {
      inspect(*ccb);
    }
    for (auto hole = face->holes_begin(); hole != face->holes_end(); ++hole) {
      inspect(*hole);
    }
    if (has_positive && !has_negative) {
      return SplitChordSide::positive;
    }
    if (has_negative && !has_positive) {
      return SplitChordSide::negative;
    }
    return SplitChordSide::on_axis;
  };

  const auto makeCells = [
                             &candidate,
                             &group_offsets,
                             &selector,
                             &makeCellSide,
                             &snapshot,
                             source_curve_count = flattened.size()](const std::size_t chord_count) {
    const auto classifications = candidate.classifyFaces(group_offsets, selector);
    std::vector<typename Arrangement::Face_const_handle> faces;
    faces.reserve(candidate.arrangement_.number_of_faces());
    for (auto face = candidate.arrangement_.faces_begin();
         face != candidate.arrangement_.faces_end();
         ++face) {
      faces.push_back(face);
    }

    std::vector<RegionCell> cells;
    cells.reserve(classifications.size());
    for (const auto& face : classifications) {
      if (!face.bounded || !face.selected) {
        continue;
      }
      if (face.face_id == 0 || face.face_id > faces.size()) {
        throw std::logic_error("Split classification returned an unknown face");
      }
      const auto arrangement_face = faces[static_cast<std::size_t>(face.face_id - 1)];
      cells.push_back(RegionCell{
          face.face_id,
          face.outer_boundary,
          face.holes,
          makeCellSide(arrangement_face),
          canonicalProvenance(
              face,
              source_curve_count,
              group_offsets,
              std::span<const SplitChord>{snapshot.chords}.first(chord_count))});
    }

    // Canonical provenance is the primary order.  Stable sorting leaves
    // indistinguishable provenance entries in the arrangement's deterministic
    // order without using FaceId or coordinates as identity.
    std::stable_sort(
        cells.begin(),
        cells.end(),
        [](const RegionCell& left, const RegionCell& right) {
          if (left.boundary_provenance != right.boundary_provenance) {
            return left.boundary_provenance < right.boundary_provenance;
          }
          return left.side < right.side;
        });
    snapshot.cells = std::move(cells);
  };

  std::vector<Hit> hits;
  const auto add_hit = [&hits](
                          const ChordPoint& point,
                          const std::size_t source,
                          const bool endpoint,
                          const bool tangent) {
    const auto found = std::ranges::find_if(
        hits, [&point](const Hit& hit) { return hit.point.equals(point); });
    if (found == hits.end()) {
      hits.push_back(Hit{point, {source}, endpoint, tangent});
    } else {
      found->sources.push_back(source);
      found->endpoint = found->endpoint || endpoint;
      found->tangent = found->tangent || tangent;
    }
  };
  for (std::size_t source = 0; source < flattened.size(); ++source) {
    const auto& input = flattened[source];
    if (const auto* segment_input = std::get_if<SegmentInput>(&input)) {
      const Point first{Kernel::FT{segment_input->source_x}, Kernel::FT{segment_input->source_y}};
      const Point second{Kernel::FT{segment_input->target_x}, Kernel::FT{segment_input->target_y}};
      const Segment segment{first, second};
      const auto intersection = CGAL::intersection(split_line, segment);
      if (!intersection) {
        continue;
      }
      if (const auto* point = std::get_if<Point>(&*intersection)) {
        add_hit(
            ChordPoint{point->x(), point->y()},
            source,
            *point == first || *point == second,
            false);
      } else {
        snapshot.status = SplitStatus::boundary_coincident;
        return snapshot;
      }
      continue;
    }

    const auto circular_curve = makeCircularCurve(input);
    const auto intersections = intersectLineCircle(split_line, circular_curve.supporting_circle());
    for (const auto& intersection : intersections) {
      if (!circularCurveContains(circular_curve, intersection.point)) {
        continue;
      }
      const bool endpoint = !circular_curve.is_full() &&
                            (intersection.point.equals(circular_curve.source()) ||
                             intersection.point.equals(circular_curve.target()));
      add_hit(intersection.point, source, endpoint, intersection.tangent);
    }
  }
  if (hits.empty()) {
    snapshot.status = SplitStatus::nonintersection;
    try {
      makeCells(0);
    } catch (const std::exception&) {
      snapshot.status = SplitStatus::branch_ambiguity;
      snapshot.cells.clear();
    }
    return snapshot;
  }
  if (std::ranges::any_of(hits, [](const Hit& hit) {
        return hit.endpoint || hit.sources.size() > 1;
      })) {
    snapshot.status = SplitStatus::vertex_touch;
    return snapshot;
  }
  const bool has_tangent = std::ranges::any_of(hits, [](const Hit& hit) { return hit.tangent; });
  std::erase_if(hits, [](const Hit& hit) { return hit.tangent; });
  if (hits.empty()) {
    snapshot.status = has_tangent ? SplitStatus::tangent : SplitStatus::nonintersection;
    try {
      makeCells(0);
    } catch (const std::exception&) {
      snapshot.status = SplitStatus::branch_ambiguity;
      snapshot.cells.clear();
    }
    return snapshot;
  }
  std::ranges::sort(hits, [&direction, &origin](const Hit& left, const Hit& right) {
    const auto parameter = [&direction, &origin](const ChordPoint& point) {
      const ChordPoint origin_point{origin.x(), origin.y()};
      const auto vector = point.x() - origin_point.x();
      const auto vector_y = point.y() - origin_point.y();
      return vector * direction.x() + vector_y * direction.y();
    };
    return CGAL::compare(parameter(left.point), parameter(right.point)) == CGAL::SMALLER;
  });
  if (hits.size() % 2 != 0) {
    snapshot.status = SplitStatus::odd_intersections;
    return snapshot;
  }

  std::vector<ChordGeometry> chord_geometries;
  for (std::size_t index = 0; index < hits.size(); index += 2) {
    const ChordPoint midpoint{
        (hits[index].point.x() + hits[index + 1].point.x()) / 2,
        (hits[index].point.y() + hits[index + 1].point.y()) / 2};
    const auto membership = candidate.membershipAt(midpoint, group_offsets);
    if (!membership) {
      snapshot.status = SplitStatus::branch_ambiguity;
      return snapshot;
    }
    std::unique_ptr<bool[]> membership_values(new bool[membership->size()]);
    for (std::size_t group = 0; group < membership->size(); ++group) {
      membership_values[group] = (*membership)[group];
    }
    if (selector(std::span<const bool>{membership_values.get(), membership->size()})) {
      std::vector<std::size_t> sources{
          hits[index].sources.front(), hits[index + 1].sources.front()};
      std::ranges::sort(sources);
      snapshot.chords.push_back(SplitChord{std::move(sources), SplitChordSide::positive,
                                           SplitStatus::success});
      chord_geometries.push_back(ChordGeometry{hits[index].point, hits[index + 1].point});
    }
  }

  // The chord endpoints stay exact from hit detection through insertion.  A
  // chord is appended only to the candidate history, after all source
  // validation has succeeded; the caller's ArrangementModel is committed
    // only after the complete cell snapshot has been constructed.
  try {
    for (const auto& chord : chord_geometries) {
      const auto inserted = CGAL::insert(
          candidate.arrangement_, Traits::Curve_2{split_line, chord.source, chord.target});
      if (candidate.arrangement_.number_of_induced_edges(inserted) == 0) {
        throw std::logic_error("Split chord did not induce an arrangement edge");
      }
      candidate.source_curve_addresses_.push_back(&*inserted);
    }
    makeCells(chord_geometries.size());
  } catch (const std::exception&) {
    snapshot.status = SplitStatus::branch_ambiguity;
    snapshot.chords.clear();
    snapshot.cells.clear();
    return snapshot;
  }

  snapshot.status = SplitStatus::success;
  setBooleanOperandGroups(std::move(source_operand_groups));
  return snapshot;
}

void ArrangementModel::setCurves(std::vector<CurveInput> curves) {
  for (const auto& curve : curves) {
    validateCurveInput(curve);
  }

  curves_ = std::move(curves);
  boolean_operand_group_offsets_.clear();
  rebuild();
}

void ArrangementModel::setCircles(std::vector<CircleInput> circles) {
  std::vector<CurveInput> curves;
  curves.reserve(circles.size());
  for (const auto& circle : circles) {
    curves.emplace_back(circle);
  }
  setCurves(std::move(curves));
}

void ArrangementModel::setBooleanOperands(
    std::vector<CurveInput> left_curves,
    std::vector<CurveInput> right_curves) {
  std::vector<OperandGroup> groups;
  groups.reserve(2);
  groups.push_back(std::move(left_curves));
  groups.push_back(std::move(right_curves));
  setBooleanOperandGroups(std::move(groups));
}

void ArrangementModel::setBooleanOperandGroups(std::vector<OperandGroup> groups) {
  if (groups.empty()) {
    throw std::invalid_argument("Boolean operations require at least one operand group");
  }

  // Validate every independent group before changing curves_ or the
  // arrangement. This gives rejected input a strong no-partial-state
  // guarantee and keeps coincident curves legal across group boundaries.
  std::vector<std::size_t> group_offsets;
  group_offsets.reserve(groups.size() + 1);
  std::vector<CurveInput> curves;
  group_offsets.push_back(0);
  for (auto& group : groups) {
    validateBooleanOperand(group);
    curves.reserve(curves.size() + group.size());
    std::ranges::move(group, std::back_inserter(curves));
    group_offsets.push_back(curves.size());
  }

  setCurves(std::move(curves));
  boolean_operand_group_offsets_ = std::move(group_offsets);
}

std::size_t ArrangementModel::sourceCurveCount() const noexcept {
  return arrangement_.number_of_curves();
}

std::size_t ArrangementModel::vertexCount() const noexcept {
  return arrangement_.number_of_vertices();
}

std::size_t ArrangementModel::edgeCount() const noexcept {
  return arrangement_.number_of_edges();
}

std::size_t ArrangementModel::faceCount() const noexcept {
  return arrangement_.number_of_faces();
}

bool ArrangementModel::hasCompleteSourceHistory() const noexcept {
  for (auto edge = arrangement_.edges_begin(); edge != arrangement_.edges_end(); ++edge) {
    if (arrangement_.number_of_originating_curves(edge) == 0) {
      return false;
    }
  }

  for (auto curve = arrangement_.curves_begin(); curve != arrangement_.curves_end(); ++curve) {
    if (arrangement_.induced_edges_begin(curve) == arrangement_.induced_edges_end(curve)) {
      return false;
    }
  }
  return true;
}

RegionHit ArrangementModel::locate(const double x, const double y) const {
  if (!std::isfinite(x) || !std::isfinite(y)) {
    throw std::invalid_argument("Region queries require finite coordinates");
  }

  using PointLocation = CGAL::Arr_naive_point_location<Arrangement>;
  using Result = CGAL::Arr_point_location_result<Arrangement>::Type;

  PointLocation point_location(arrangement_);
  using Coordinate = typename Traits::Point_2::CoordNT;
  const Traits::Point_2 query_point{Coordinate{Kernel::FT{x}}, Coordinate{Kernel::FT{y}}};
  const Result result = point_location.locate(query_point);

  if (const auto* face = std::get_if<typename Arrangement::Face_const_handle>(&result)) {
    FaceId face_id = 0;
    FaceId next_id = 1;
    for (auto candidate = arrangement_.faces_begin(); candidate != arrangement_.faces_end();
         ++candidate, ++next_id) {
      if (&*candidate == &**face) {
        face_id = next_id;
        break;
      }
    }
    if (face_id == 0) {
      throw std::logic_error("Point location returned an unknown arrangement face");
    }
    return RegionHit{RegionHit::Kind::face, !(*face)->is_unbounded(), face_id};
  }
  if (std::get_if<typename Arrangement::Halfedge_const_handle>(&result) != nullptr) {
    return RegionHit{RegionHit::Kind::edge, false, std::nullopt};
  }
  return RegionHit{RegionHit::Kind::vertex, false, std::nullopt};
}

std::vector<FaceClassification> ArrangementModel::classifyFaces(
    const BooleanOperation operation) const {
  if (boolean_operand_group_offsets_.size() != 3) {
    throw std::invalid_argument(
        "Two-operand classification requires exactly two configured operand groups");
  }
  const auto selector = [operation](const std::span<const bool> membership) {
    return selected(operation, membership[0], membership[1]);
  };
  return classifyFaces(boolean_operand_group_offsets_, selector);
}

std::vector<FaceClassification> ArrangementModel::classifyFaces(
    const BooleanOperation operation, const std::size_t leftSourceCurveCount) const {
  if (leftSourceCurveCount > curves_.size()) {
    throw std::invalid_argument("Left Boolean input exceeds the source curve count");
  }
  std::vector<std::size_t> group_offsets{0, leftSourceCurveCount, curves_.size()};
  if (leftSourceCurveCount != 0) {
    validateBooleanOperand(
        std::span<const CurveInput>{curves_}.first(leftSourceCurveCount));
  }
  if (leftSourceCurveCount != curves_.size()) {
    validateBooleanOperand(
        std::span<const CurveInput>{curves_}.subspan(leftSourceCurveCount));
  }
  const auto selector = [operation](const std::span<const bool> membership) {
    return selected(operation, membership[0], membership[1]);
  };
  return classifyFaces(group_offsets, selector);
}

std::vector<FaceClassification> ArrangementModel::classifyFaces(
    const FaceSelector& selector) const {
  if (boolean_operand_group_offsets_.size() < 2) {
    throw std::invalid_argument(
        "Boolean operands were not configured; call setBooleanOperandGroups first");
  }
  if (!selector) {
    throw std::invalid_argument("Boolean face selector must be callable");
  }
  return classifyFaces(boolean_operand_group_offsets_, selector);
}

std::vector<FaceClassification> ArrangementModel::classifyFaces(
    const std::vector<std::size_t>& group_offsets,
    const FaceSelector& selector) const {
  if (group_offsets.size() < 2 || group_offsets.back() > curves_.size()) {
    throw std::logic_error("Boolean operand group offsets do not match the arrangement");
  }
  if (!std::ranges::is_sorted(group_offsets)) {
    throw std::logic_error("Boolean operand group offsets are not ordered");
  }

  using Face = Arrangement::Face;
  std::vector<typename Arrangement::Face_const_handle> faces;
  faces.reserve(arrangement_.number_of_faces());
  std::unordered_map<const Face*, std::size_t> face_indices;
  for (auto face = arrangement_.faces_begin(); face != arrangement_.faces_end(); ++face) {
    const auto index = faces.size();
    faces.push_back(face);
    face_indices.emplace(&*face, index);
  }

  const std::size_t group_count = group_offsets.size() - 1;
  std::vector<std::vector<bool>> inside(
      faces.size(), std::vector<bool>(group_count, false));
  // A split candidate may have additional arrangement-history curves that
  // are not represented in curves_ or in the operand offsets.  The sentinel
  // keeps those curves from toggling operand membership while retaining
  // their history for boundary provenance.
  std::vector<std::size_t> source_groups(curves_.size(), group_count);
  for (std::size_t group = 0; group < group_count; ++group) {
    for (std::size_t source = group_offsets[group]; source < group_offsets[group + 1]; ++source) {
      source_groups[source] = group;
    }
  }
  std::vector<bool> visited(faces.size(), false);
  const auto unbounded = std::ranges::find_if(
      faces, [](const auto face) { return face->is_unbounded(); });
  if (unbounded == faces.end()) {
    throw std::logic_error("Arrangement must have an unbounded face");
  }
  const auto unbounded_index = static_cast<std::size_t>(unbounded - faces.begin());
  visited[unbounded_index] = true;

  auto source_index = [this](const Arrangement::Curve_const_iterator curve) {
    const auto* address = &*curve;
    const auto found = std::ranges::find(source_curve_addresses_, address);
    if (found == source_curve_addresses_.end()) {
      throw std::logic_error("Arrangement history contains an unknown source curve");
    }
    return static_cast<std::size_t>(found - source_curve_addresses_.begin());
  };

  auto propagate = [&](const auto& current, const auto& neighbor, const auto& halfedge) {
    const auto current_index = face_indices.at(&*current);
    const auto neighbor_index = face_indices.at(&*neighbor);
    auto candidate = inside[current_index];
    std::vector<bool> crossings(group_count, false);
    for (auto origin = arrangement_.originating_curves_begin(halfedge);
         origin != arrangement_.originating_curves_end(halfedge); ++origin) {
      const std::size_t source =
          source_index(static_cast<Arrangement::Curve_const_iterator>(origin));
      if (source < source_groups.size() && source_groups[source] < group_count) {
        crossings[source_groups[source]] = true;
      }
    }
    for (std::size_t group = 0; group < crossings.size(); ++group) {
      if (crossings[group]) {
        candidate[group] = !candidate[group];
      }
    }
    if (!visited[neighbor_index]) {
      inside[neighbor_index] = std::move(candidate);
      visited[neighbor_index] = true;
      return;
    }
    if (inside[neighbor_index] != candidate) {
      throw std::logic_error("Inconsistent closed-curve face classification");
    }
  };

  bool changed = true;
  while (changed) {
    changed = false;
    for (auto edge = arrangement_.edges_begin(); edge != arrangement_.edges_end(); ++edge) {
      const auto halfedge = typename Arrangement::Halfedge_const_handle(edge);
      const auto twin = halfedge->twin();
      const auto current = halfedge->face();
      const auto neighbor = twin->face();
      const auto current_index = face_indices.at(&*current);
      const auto neighbor_index = face_indices.at(&*neighbor);
      const bool current_was_visited = visited[current_index];
      const bool neighbor_was_visited = visited[neighbor_index];
      if (current_was_visited) {
        propagate(current, neighbor, halfedge);
      } else if (neighbor_was_visited) {
        propagate(neighbor, current, twin);
      }
      changed = changed || (!current_was_visited && visited[current_index]) ||
                (!neighbor_was_visited && visited[neighbor_index]);
    }
  }

  if (std::ranges::any_of(visited, [](const bool value) { return !value; })) {
    throw std::logic_error("Arrangement faces are not connected through edges");
  }

  std::vector<FaceClassification> result;
  result.reserve(faces.size());

  const auto source_indices = [this](const auto& halfedge) {
    std::vector<std::size_t> indices;
    for (auto origin = arrangement_.originating_curves_begin(halfedge);
         origin != arrangement_.originating_curves_end(halfedge); ++origin) {
      const auto* address = &*origin;
      const auto found = std::ranges::find(source_curve_addresses_, address);
      if (found == source_curve_addresses_.end()) {
        throw std::logic_error("Arrangement history contains an unknown source curve");
      }
      indices.push_back(static_cast<std::size_t>(found - source_curve_addresses_.begin()));
    }
    std::ranges::sort(indices);
    return indices;
  };

  const auto make_loop = [&](const auto& ccb) {
    BoundaryLoop loop;
    const auto start = ccb;
    auto current = start;
    auto last_target = start->target()->point();
    do {
      const auto& curve = current->curve();
      const BoundaryPoint edge_start = pointDto(current->source()->point());
      const BoundaryPoint edge_end = pointDto(current->target()->point());
      last_target = current->target()->point();
      const auto history_indices = source_indices(current);
      std::vector<std::size_t> source_curve_indices;
      std::vector<std::size_t> split_chord_indices;
      for (const std::size_t history_index : history_indices) {
        if (history_index < curves_.size()) {
          source_curve_indices.push_back(history_index);
        } else {
          split_chord_indices.push_back(history_index - curves_.size());
        }
      }
      BoundaryEdge edge{
          BoundaryCurve{LineSegmentBoundary{edge_start, edge_end}},
          edge_start,
          edge_end,
          std::move(source_curve_indices),
          std::move(split_chord_indices)};

      if (curve.is_circular()) {
        const auto circle = curve.supporting_circle();
        BoundaryPoint center = pointDto(circle.center());
        double radius = std::sqrt(CGAL::to_double(circle.squared_radius()));
        if (!edge.source_curve_indices.empty() &&
            edge.source_curve_indices.front() < curves_.size()) {
          const auto& source = curves_[edge.source_curve_indices.front()];
          if (const auto* input = std::get_if<CircleInput>(&source)) {
            center = BoundaryPoint{input->center_x, input->center_y};
            radius = input->radius;
          }
        }
        auto direction = curve.orientation() == CGAL::COUNTERCLOCKWISE
                             ? BoundaryDirection::counterclockwise
                             : BoundaryDirection::clockwise;
        if (!curve.source().equals(current->source()->point())) {
          direction = opposite(direction);
        }
        edge.curve = CircularArcBoundary{
            center,
            radius,
            edge_start,
            edge_end,
            direction};
      }
      loop.edges.push_back(std::move(edge));
      ++current;
    } while (current != start);
    loop.closed = !loop.edges.empty() && last_target == start->source()->point();
    return loop;
  };

  const auto boundaries = [&](const auto face) {
    BoundaryLoop outer;
    if (face->outer_ccbs_begin() != face->outer_ccbs_end()) {
      outer = make_loop(*face->outer_ccbs_begin());
    }
    std::vector<BoundaryLoop> holes;
    for (auto hole = face->holes_begin(); hole != face->holes_end(); ++hole) {
      holes.push_back(make_loop(*hole));
    }
    return std::pair<BoundaryLoop, std::vector<BoundaryLoop>>{
        std::move(outer), std::move(holes)};
  };

  for (std::size_t index = 0; index < faces.size(); ++index) {
    const bool inside_left = inside[index].size() > 0 && inside[index][0];
    const bool inside_right = inside[index].size() > 1 && inside[index][1];
    const auto membership_storage = inside[index];
    std::unique_ptr<bool[]> membership(new bool[group_count]);
    for (std::size_t group = 0; group < group_count; ++group) {
      membership[group] = membership_storage[group];
    }
    const bool is_selected = selector(std::span<const bool>{membership.get(), group_count});
    auto [outer_boundary, holes] = boundaries(faces[index]);
    result.push_back(FaceClassification{
        static_cast<FaceId>(index + 1),
        !faces[index]->is_unbounded(),
        membership_storage,
        inside_left,
        inside_right,
        is_selected,
        std::move(outer_boundary),
        std::move(holes)});
  }
  return result;
}

BooleanEvaluationSnapshot ArrangementModel::evaluateBoolean(
    const BooleanOperation operation) const {
  BooleanEvaluationSnapshot snapshot;
  snapshot.operation = operation;
  for (auto& face : classifyFaces(operation)) {
    if (face.bounded && face.selected) {
      snapshot.regions.push_back(std::move(face));
    }
  }
  return snapshot;
}

BooleanEvaluationSnapshot ArrangementModel::evaluateBoolean(
    const BooleanOperation operation, const std::size_t leftSourceCurveCount) const {
  BooleanEvaluationSnapshot snapshot;
  snapshot.operation = operation;
  for (auto& face : classifyFaces(operation, leftSourceCurveCount)) {
    if (face.bounded && face.selected) {
      snapshot.regions.push_back(std::move(face));
    }
  }
  return snapshot;
}

BooleanEvaluationSnapshot ArrangementModel::evaluateBoolean(
    const FaceSelector& selector) const {
  BooleanEvaluationSnapshot snapshot;
  for (auto& face : classifyFaces(selector)) {
    if (face.bounded && face.selected) {
      snapshot.regions.push_back(std::move(face));
    }
  }
  return snapshot;
}

void ArrangementModel::rebuild() {
  arrangement_.clear();
  source_curve_addresses_.clear();
  source_curve_addresses_.reserve(curves_.size());

  using Point = Kernel::Point_2;
  using Circle = Kernel::Circle_2;
  for (const auto& curve : curves_) {
    std::visit(
        [this](const auto& input) {
          using Input = std::decay_t<decltype(input)>;
          if constexpr (std::is_same_v<Input, CircleInput>) {
            const Point center(input.center_x, input.center_y);
            const Kernel::FT radius(input.radius);
            const Circle circle(center, radius * radius);
            const auto inserted = CGAL::insert(arrangement_, typename Traits::Curve_2(circle));
            source_curve_addresses_.push_back(&*inserted);
          } else if constexpr (std::is_same_v<Input, SegmentInput>) {
            const Point source(input.source_x, input.source_y);
            const Point target(input.target_x, input.target_y);
            const auto inserted =
                CGAL::insert(arrangement_, typename Traits::Curve_2(source, target));
            source_curve_addresses_.push_back(&*inserted);
          } else {
            const Point source(input.source_x, input.source_y);
            const Point interior(input.interior_x, input.interior_y);
            const Point target(input.target_x, input.target_y);
            const auto inserted =
                CGAL::insert(arrangement_, typename Traits::Curve_2(source, interior, target));
            source_curve_addresses_.push_back(&*inserted);
          }
        },
        curve);
  }
}

}  // namespace signet::geometry
