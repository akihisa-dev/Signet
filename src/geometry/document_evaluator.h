// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "core/document.h"
#include "geometry/arrangement_model.h"
#include "geometry/circle_input.h"

#include <string>
#include <vector>

namespace signet::geometry {

struct EvaluatedCircle final {
  core::NodeId node_id{};
  CircleInput circle{};

  friend bool operator==(const EvaluatedCircle&, const EvaluatedCircle&) = default;
};

// Library-independent curves generated from one primitive.  The node id is
// attached to the set because a rectangle is represented by four source
// segments while an arc is represented by one source curve.
struct EvaluatedCurveSet final {
  core::NodeId node_id{};
  std::vector<CurveInput> curves;

  friend bool operator==(const EvaluatedCurveSet&, const EvaluatedCurveSet&) = default;
};

struct EvaluationDiagnostic final {
  core::NodeId node_id{};
  std::string reason;
};

struct EvaluatedBoolean final {
  core::NodeId node_id{};
  core::BooleanOperation operation{core::BooleanOperation::unite};
  // The regions and their boundary DTOs belong to this evaluation snapshot.
  // FaceId values are not persistent document identities.
  std::vector<FaceClassification> regions;
};

// A split cell is paired with the document-stable key produced from the
// construction DAG and arrangement provenance.  The RegionCell itself remains
// an evaluation/display DTO: its FaceId and coordinates are not persistent.
struct EvaluatedRegion final {
  core::RegionKey key{};
  RegionCell cell{};

  friend bool operator==(const EvaluatedRegion&, const EvaluatedRegion&) = default;
};

struct EvaluatedSplit final {
  core::NodeId node_id{};
  SplitStatus status{SplitStatus::invalid_input};
  std::vector<SplitChord> chords;
  // Split is non-destructive: every material cell is retained here.
  std::vector<EvaluatedRegion> cells;

  friend bool operator==(const EvaluatedSplit&, const EvaluatedSplit&) = default;
};

struct EvaluatedRegionSelection final {
  core::NodeId node_id{};
  core::NodeId input{};
  std::vector<core::RegionKey> region_keys;
  std::vector<EvaluatedRegion> cells;

  friend bool operator==(
      const EvaluatedRegionSelection&, const EvaluatedRegionSelection&) = default;
};

struct EvaluatedRegionFilter final {
  core::NodeId node_id{};
  core::NodeId input{};
  core::NodeId selection{};
  core::RegionFilterMode mode{core::RegionFilterMode::keep_selected};
  // The filtered result is the selected display arrangement, not a mutation
  // of the input split.  In remove mode this contains the complement cells.
  std::vector<EvaluatedRegion> cells;

  friend bool operator==(const EvaluatedRegionFilter&, const EvaluatedRegionFilter&) = default;
};

struct DocumentEvaluationSnapshot final {
  std::vector<EvaluatedCircle> circles;
  std::vector<EvaluatedCurveSet> curve_sets;
  std::vector<EvaluatedBoolean> booleans;
  std::vector<EvaluatedSplit> splits;
  std::vector<EvaluatedRegionSelection> region_selections;
  std::vector<EvaluatedRegionFilter> region_filters;
  std::vector<EvaluationDiagnostic> diagnostics;
};

class DocumentEvaluator final {
 public:
  [[nodiscard]] static DocumentEvaluationSnapshot evaluate(const core::Document& document);
};

}  // namespace signet::geometry
