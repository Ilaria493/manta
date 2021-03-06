//
// Manta - Structural Variant and Indel Caller
// Copyright (c) 2013-2019 Illumina, Inc.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//

/// \file
/// \author Chris Saunders and Xiaoyu Chen
///

#pragma once

#include <iosfwd>
#include <string>

#include "manta/SVCandidate.hpp"
#include "manta/SVCandidateAssemblyData.hpp"

/// Computes mapping from candidate SV breakends to the assembled SV contig and reference sequence while
/// accounting for microhomology, and stores the mapping information.
///
struct SVAlignmentInfo {
  SVAlignmentInfo(const SVCandidate& sv, const SVCandidateAssemblyData& assemblyData);

  const std::string& bp1ContigSeq() const { return (_bp1ContigReversed ? revContigSeq : contigSeq); }

  const std::string& bp2ContigSeq() const { return (_bp2ContigReversed ? revContigSeq : contigSeq); }

  const std::string& bp1ReferenceSeq() const { return bp1RefSeq; }

  const std::string& bp2ReferenceSeq() const { return (_isSpanning ? bp2RefSeq : bp1RefSeq); }

  bool isSpanning() const { return _isSpanning; }

  /// If true, this indicates that there is enough room on either side of the breakend for both the ref and
  /// contig to make a fair split read evaluation.
  ///
  /// \param minEdge The minimum distance of each breakpoint from either edge of the sv contig or reference
  /// sequence.
  ///
  bool isMinBpEdge(const unsigned minEdge) const;

  friend std::ostream& operator<<(std::ostream& os, const SVAlignmentInfo& ai);

private:
  std::string contigSeq;
  std::string revContigSeq;
  std::string bp1RefSeq;
  std::string bp2RefSeq;
  const bool  _isSpanning;
  const bool  _bp1ContigReversed;
  const bool  _bp2ContigReversed;

public:
  // All offset range 'begin' values correspond to the zero-indexed base immediately before the
  // breakend on the fwd-strand, and 'end' values correspond to the zero-indexed base immediately
  // before the breakend on the forward strand+microhomology range.
  //
  // In the absence of microhomology, begin and end should be equal.

  known_pos_range2 bp1ContigOffset;
  known_pos_range2 bp2ContigOffset;
  known_pos_range2 bp1RefOffset;
  known_pos_range2 bp2RefOffset;
};

std::ostream& operator<<(std::ostream& os, const SVAlignmentInfo& ai);

/// Sample-specific and allele-specific evidence info
struct SVSampleAlleleInfo {
  void clear()
  {
    spanningPairCount                    = 0;
    confidentSpanningPairCount           = 0;
    confidentSemiMappedSpanningPairCount = 0;
    splitReadCount                       = 0;
    splitReadEvidence                    = 0;
    splitReadMapQ                        = 0;
    confidentSplitReadCount              = 0;
    confidentSplitReadAndPairCountRefBp1 = 0;
    confidentSplitReadAndPairCountRefBp2 = 0;
  }

  // allele read pair support

  /// all mapped pairs compatible with the allele
  unsigned spanningPairCount = 0;

  /// Read pairs where both reads are mapped and we've successfully looked up a fragment prob of 0.01 or more
  unsigned confidentSpanningPairCount = 0;

  /// Read pairs where at least one read is mapped and we've successfully looked up a fragment prob of 0.01
  /// or more
  unsigned confidentSemiMappedSpanningPairCount = 0;

  // allele split read support

  unsigned splitReadCount    = 0;
  float    splitReadEvidence = 0;
  float    splitReadMapQ     = 0;

  /// count by comparing alignment quality vs the other allele
  unsigned confidentSplitReadCount = 0;

  /// For 'ref' alleles, the support by split reads and spanning pairs at bp1:
  unsigned confidentSplitReadAndPairCountRefBp1 = 0;

  /// For 'ref' alleles, the support by split reads and spanning pairs at bp2:
  unsigned confidentSplitReadAndPairCountRefBp2 = 0;
};

std::ostream& operator<<(std::ostream& os, const SVSampleAlleleInfo& si);

/// Sample-specific evidence info
struct SVSampleInfo {
  void clear()
  {
    alt.clear();
    ref.clear();
  }

  SVSampleAlleleInfo alt;
  SVSampleAlleleInfo ref;
};

std::ostream& operator<<(std::ostream& os, const SVSampleInfo& si);

/// Consolidate model-agnostic scoring results applied to an SV candidate
struct SVScoreInfo {
  void setSampleCount(const unsigned sampleCount)
  {
    samples.resize(sampleCount);
    clear();
  }

  void clear()
  {
    for (auto& sample : samples) {
      sample.clear();
    }

    bp1MaxDepth = 0;
    bp2MaxDepth = 0;

    bp1MQ0Frac = 0.;
    bp2MQ0Frac = 0.;
  }

  std::vector<SVSampleInfo> samples;

  unsigned bp1MaxDepth = 0;
  unsigned bp2MaxDepth = 0;

  float bp1MQ0Frac = 0;
  float bp2MQ0Frac = 0;
};

std::ostream& operator<<(std::ostream& os, const SVScoreInfo& ssi);
