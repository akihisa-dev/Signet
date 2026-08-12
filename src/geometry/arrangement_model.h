// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <CGAL/Arr_circle_segment_traits_2.h>
#include <CGAL/Arrangement_with_history_2.h>
#include <CGAL/Exact_predicates_exact_constructions_kernel.h>

#include "geometry/circle_input.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace signet::geometry {

struct SegmentInput final {
  double source_x{};
  double source_y{};
  double target_x{};
  double target_y{};

  friend bool operator==(const SegmentInput&, const SegmentInput&) = default;
};

struct ArcInput final {
  double source_x{};
  double source_y{};
  double interior_x{};
  double interior_y{};
  double target_x{};
  double target_y{};

  friend bool operator==(const ArcInput&, const ArcInput&) = default;
};

using CurveInput = std::variant<CircleInput, SegmentInput, ArcInput>;
using OperandGroup = std::vector<CurveInput>;
using FaceSelector = std::function<bool(std::span<const bool>)>;

struct Point2 final {
  double x{};
  double y{};

  friend bool operator==(const Point2&, const Point2&) = default;
};

struct SplitAxis final {
  Point2 origin{};
  Point2 direction{1.0, 0.0};

  friend bool operator==(const SplitAxis&, const SplitAxis&) = default;
};

enum class SplitChordSide : std::uint8_t {
  negative,
  positive,
  // Used by a cell that has no strict relation to the split axis (for
  // example, a degenerate/tangent boundary).  The alias keeps the public
  // vocabulary compatible with the document-side cutter contract.
  on_axis,
  on_boundary = on_axis,
};

enum class SplitStatus : std::uint8_t {
  invalid_input,
  success,
  nonintersection,
  tangent,
  boundary_coincident,
  vertex_touch,
  odd_intersections,
  branch_ambiguity,
};

struct SplitChord final {
  // The two source curves containing the exact chord endpoints, in canonical
  // axis order.  The endpoint coordinates remain private exact arrangement
  // state and are intentionally not part of this DTO.
  std::vector<std::size_t> source_curve_indices;
  SplitChordSide side{SplitChordSide::positive};
  SplitStatus status{SplitStatus::success};

  friend bool operator==(const SplitChord&, const SplitChord&) = default;
};

// These DTOs deliberately contain only scalar values.  They are an
// arrangement-evaluation snapshot, not a persistence format: FaceId values
// are valid only for the ArrangementModel snapshot that produced them.
using FaceId = std::uint64_t;

struct BoundaryPoint final {
  double x{};
  double y{};

  friend bool operator==(const BoundaryPoint&, const BoundaryPoint&) = default;
};

enum class BoundaryDirection : std::uint8_t {
  clockwise,
  counterclockwise,
};

struct CircularArcBoundary final {
  BoundaryPoint center{};
  double radius{};
  BoundaryPoint start{};
  BoundaryPoint end{};
  BoundaryDirection direction{BoundaryDirection::counterclockwise};

  friend bool operator==(const CircularArcBoundary&, const CircularArcBoundary&) = default;
};

struct LineSegmentBoundary final {
  BoundaryPoint start{};
  BoundaryPoint end{};

  friend bool operator==(const LineSegmentBoundary&, const LineSegmentBoundary&) = default;
};

using BoundaryCurve = std::variant<CircularArcBoundary, LineSegmentBoundary>;
using CircularArcDto = CircularArcBoundary;
using LineSegmentDto = LineSegmentBoundary;
using BoundaryCurveDto = BoundaryCurve;

struct BoundaryEdge final {
  BoundaryCurve curve;
  BoundaryPoint start{};
  BoundaryPoint end{};
  // Indices refer only to the input CurveInput vector, and are therefore also
  // snapshot-local.  They expose source Arrangement_with_history_2
  // provenance without exposing CGAL types.
  std::vector<std::size_t> source_curve_indices;
  // Split edges are kept separate from source indices so a displayed
  // boundary never misrepresents a generated chord as an input curve.
  std::vector<std::size_t> split_chord_indices;

  friend bool operator==(const BoundaryEdge&, const BoundaryEdge&) = default;
};

struct BoundaryLoop final {
  std::vector<BoundaryEdge> edges;
  bool closed{true};

  friend bool operator==(const BoundaryLoop&, const BoundaryLoop&) = default;
};
using BoundaryLoopDto = BoundaryLoop;

enum class RegionBoundaryProvenanceKind : std::uint8_t {
  source_curve,
  split_chord,
};

// A boundary token is deliberately independent of endpoints, traversal
// direction, and arrangement handles.  For a source token, operand_index and
// source_curve_index identify the evaluated input relation.  For a split token
// split_chord_index identifies the chord in the same SplitSnapshot.
struct RegionBoundaryProvenance final {
  RegionBoundaryProvenanceKind kind{RegionBoundaryProvenanceKind::source_curve};
  std::size_t operand_index{std::numeric_limits<std::size_t>::max()};
  std::size_t source_curve_index{std::numeric_limits<std::size_t>::max()};
  std::size_t split_chord_index{std::numeric_limits<std::size_t>::max()};
  // A split token carries the relation that created the chord as well as its
  // snapshot-local occurrence index.  This lets a caller map provenance to a
  // persistent key without coordinates, FaceId values, or curve handles.
  std::vector<std::size_t> split_chord_source_curve_indices;
  std::vector<std::size_t> split_chord_operand_indices;
  SplitChordSide split_chord_side{SplitChordSide::on_axis};

  friend bool operator==(
      const RegionBoundaryProvenance&, const RegionBoundaryProvenance&) = default;
  friend auto operator<=>(
      const RegionBoundaryProvenance&, const RegionBoundaryProvenance&) = default;
};

using RegionBoundaryProvenanceLoop = std::vector<RegionBoundaryProvenance>;

struct RegionCell final {
  // FaceId is meaningful only inside the SplitSnapshot that contains this
  // cell.  It is not part of boundary provenance or a persistent identity.
  FaceId face_id{};
  BoundaryLoop outer_boundary;
  std::vector<BoundaryLoop> holes;
  SplitChordSide side{SplitChordSide::on_axis};
  // The loops are canonicalized independently (orientation- and cyclic-start
  // independent) and then sorted lexicographically.  A source curve token
  // carries its operand relation; a split token carries only the chord index.
  std::vector<RegionBoundaryProvenanceLoop> boundary_provenance;

  friend bool operator==(const RegionCell&, const RegionCell&) = default;
};

struct SplitEvaluationSnapshot final {
  SplitStatus status{SplitStatus::success};
  std::vector<SplitChord> chords;
  std::vector<RegionCell> cells;

  friend bool operator==(const SplitEvaluationSnapshot&, const SplitEvaluationSnapshot&) =
      default;
};

using SplitSnapshot = SplitEvaluationSnapshot;

struct RegionHit final {
  enum class Kind { face, edge, vertex };

  Kind kind{Kind::face};
  bool bounded{};
  std::optional<FaceId> face_id;
};

enum class BooleanOperation : std::uint8_t {
  unite,
  intersect,
  subtract,
  exclusive_or,
};

struct FaceClassification final {
  FaceId face_id{};
  bool bounded{};
  std::vector<bool> operand_membership;
  bool inside_left{};
  bool inside_right{};
  bool selected{};
  BoundaryLoop outer_boundary;
  std::vector<BoundaryLoop> holes;

  friend bool operator==(const FaceClassification&, const FaceClassification&) = default;
};

struct BooleanEvaluationSnapshot final {
  BooleanOperation operation{BooleanOperation::unite};
  // Only selected bounded regions are included.  The unbounded face is never
  // a result region, even when a Boolean predicate selects it.
  std::vector<FaceClassification> regions;

  friend bool operator==(
      const BooleanEvaluationSnapshot&, const BooleanEvaluationSnapshot&) = default;
};
using FaceRegion = FaceClassification;
using BooleanSnapshot = BooleanEvaluationSnapshot;

class ArrangementModel final {
 public:
  void setCurves(std::vector<CurveInput> curves);
  void setCircles(std::vector<CircleInput> circles);
  // Concatenates the two evaluated primitive contour sets while retaining
  // their operand boundary.  Each operand is validated as one closed
  // contour when a Boolean classification is requested.
  void setBooleanOperands(
      std::vector<CurveInput> left_curves,
      std::vector<CurveInput> right_curves);
  // Concatenates validated closed contours in caller-provided group order.
  // Groups are identified by their position, not by source-curve history.
  void setBooleanOperandGroups(std::vector<OperandGroup> groups);

  // Builds source boundaries and exact material chords for a split axis.  The
  // returned snapshot contains provenance only; exact endpoints are retained
  // by the arrangement and are never round-tripped through public doubles.
  [[nodiscard]] SplitEvaluationSnapshot splitChords(
      std::vector<OperandGroup> source_operand_groups,
      const FaceSelector& selector,
      SplitAxis axis);

  [[nodiscard]] std::size_t sourceCurveCount() const noexcept;
  [[nodiscard]] std::size_t vertexCount() const noexcept;
  [[nodiscard]] std::size_t edgeCount() const noexcept;
  [[nodiscard]] std::size_t faceCount() const noexcept;
  [[nodiscard]] bool hasCompleteSourceHistory() const noexcept;
  [[nodiscard]] RegionHit locate(double x, double y) const;
  [[nodiscard]] std::vector<FaceClassification> classifyFaces(
      BooleanOperation operation) const;
  [[nodiscard]] std::vector<FaceClassification> classifyFaces(
      BooleanOperation operation, std::size_t leftSourceCurveCount) const;
  [[nodiscard]] std::vector<FaceClassification> classifyFaces(
      const FaceSelector& selector) const;
  [[nodiscard]] BooleanEvaluationSnapshot evaluateBoolean(
      BooleanOperation operation) const;
  [[nodiscard]] BooleanEvaluationSnapshot evaluateBoolean(
      BooleanOperation operation, std::size_t leftSourceCurveCount) const;
  [[nodiscard]] BooleanEvaluationSnapshot evaluateBoolean(
      const FaceSelector& selector) const;

  // Alias used by consumers that name the result after the snapshot rather
  // than after the operation.
  [[nodiscard]] BooleanEvaluationSnapshot booleanSnapshot(
      BooleanOperation operation) const {
    return evaluateBoolean(operation);
  }

  [[nodiscard]] BooleanEvaluationSnapshot booleanSnapshot(
      BooleanOperation operation, std::size_t leftSourceCurveCount) const {
    return evaluateBoolean(operation, leftSourceCurveCount);
  }

  [[nodiscard]] BooleanEvaluationSnapshot booleanSnapshot(
      const FaceSelector& selector) const {
    return evaluateBoolean(selector);
  }

 private:
  using Kernel = CGAL::Exact_predicates_exact_constructions_kernel;
  using Traits = CGAL::Arr_circle_segment_traits_2<Kernel>;
  using Arrangement = CGAL::Arrangement_with_history_2<Traits>;

  [[nodiscard]] std::vector<FaceClassification> classifyFaces(
      const std::vector<std::size_t>& group_offsets,
      const FaceSelector& selector) const;
  [[nodiscard]] BooleanEvaluationSnapshot evaluateBoolean(
      const std::vector<std::size_t>& group_offsets,
      const FaceSelector& selector) const;

  [[nodiscard]] std::optional<std::vector<bool>> membershipAt(
      const Traits::Point_2& point,
      const std::vector<std::size_t>& group_offsets) const;

  void rebuild();

  std::vector<CurveInput> curves_;
  Arrangement arrangement_;
  std::vector<const Arrangement::Curve_2*> source_curve_addresses_;
  std::vector<std::size_t> boolean_operand_group_offsets_;
};

}  // namespace signet::geometry
