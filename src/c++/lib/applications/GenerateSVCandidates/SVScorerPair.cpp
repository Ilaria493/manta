// -*- mode: c++; indent-tabs-mode: nil; -*-
//
// Manta
// Copyright (c) 2013 Illumina, Inc.
//
// This software is provided under the terms and conditions of the
// Illumina Open Source Software License 1.
//
// You should have received a copy of the Illumina Open Source
// Software License 1 along with this program. If not, see
// <https://github.com/sequencing/licenses/>
//

///
/// \author Chris Saunders and Xiaoyu Chen
///

#include "SVScorer.hh"
#include "SVScorerShared.hh"
#include "SVScorePairAltProcessor.hh"

#include "blt_util/align_path_bam_util.hh"
#include "blt_util/bam_streamer.hh"
#include "blt_util/bam_record_util.hh"
#include "common/Exceptions.hh"
#include "manta/SVCandidateUtil.hh"
#include "svgraph/GenomeIntervalUtil.hh"

#include "boost/foreach.hpp"
#include "boost/make_shared.hpp"

#include <iostream>
#include <sstream>
#include <string>

/// standard debug output for this file:
//#define DEBUG_PAIR

/// ridiculous debug output for this file:
//#define DEBUG_MEGAPAIR

#ifdef DEBUG_PAIR
#include "blt_util/log.hh"
#endif





static
void
setAlleleFrag(
    const SizeDistribution& fragDistro,
    const int size,
    SVFragmentEvidenceAlleleBreakend& bp)
{
    float fragProb(fragDistro.cdf(size));
    fragProb = std::min(fragProb, (1-fragProb));
#ifdef DEBUG_MEGAPAIR
    log_os << __FUNCTION__ << ": fraglen,prob " << size << " " << fragProb << "\n";
#endif

    bp.isFragmentSupport = true;
    bp.fragLengthProb = fragProb;
}



static
void
processBamProcList(
    const std::vector<SVScorer::streamPtr>& bamList,
    std::vector<SVScorer::bamProcPtr>& bamProcList)
{
    const unsigned bamCount(bamList.size());
    const unsigned bamProcCount(bamProcList.size());

    for (unsigned bamIndex(0); bamIndex < bamCount; ++bamIndex)
    {
        // get the minimum set of scan intervals (this should almost always be 1!)
        std::vector<GenomeInterval> scanIntervals;
        std::vector<unsigned> intervalMap;
        {
            BOOST_FOREACH(SVScorer::bamProcPtr& bpp, bamProcList)
            {
                const GenomeInterval& interval(bpp->nextBamIndex(bamIndex));
                if (interval.range.size() < 1) continue;

                scanIntervals.push_back(interval);
            }

            intervalMap = intervalCompressor(scanIntervals);
        }

        bam_streamer& bamStream(*bamList[bamIndex]);

        const unsigned intervalCount(scanIntervals.size());
        for (unsigned intervalIndex(0); intervalIndex<intervalCount; ++intervalIndex)
        {
            const GenomeInterval& scanInterval(scanIntervals[intervalIndex]);
            if (scanInterval.range.begin_pos() >= scanInterval.range.end_pos()) continue;

            // set bam stream to new search interval:
            bamStream.set_new_region(scanInterval.tid, scanInterval.range.begin_pos(), scanInterval.range.end_pos());

            /// define the procs where' going to handle in this interval:
            std::vector<unsigned> targetProcs;
            for (unsigned procIndex(0); procIndex<bamProcCount; ++procIndex)
            {
                if (intervalMap[procIndex] == intervalIndex)
                {
                    targetProcs.push_back(procIndex);
                }
            }

            while (bamStream.next())
            {
                const bam_record& bamRead(*(bamStream.get_record_ptr()));

                BOOST_FOREACH(const unsigned procIndex, targetProcs)
                {
                    SVScorer::bamProcPtr& bpp(bamProcList[procIndex]);
                    bpp->processRecord(bamRead);
                }
            }
        }
    }
}



/// get reference allele pair support at a single breakend:
///
void
SVScorer::
getSVRefPairSupport(
    const PairOptions& pairOpt,
    const SVBreakend& bp,
    const bool isBp1,
    SVEvidence& evidence)
{
    /// search for all read pairs supporting the reference allele
    ///
    /// APPROXIMATION: for imprecise and precise variants treat the breakend locations as the center of the
    ///  breakend interval.
    ///
    /// TODO: improve on the approx above
    ///
    const pos_t centerPos(bp.interval.range.center_pos());


    const unsigned minMapQ(_readScanner.getMinMapQ());

    const unsigned bamCount(_bamStreams.size());
    for (unsigned bamIndex(0); bamIndex < bamCount; ++bamIndex)
    {
        const bool isTumor(_isAlignmentTumor[bamIndex]);

        bam_streamer& bamStream(*_bamStreams[bamIndex]);

        /// set the search range around centerPos so that we can get any fragments at the Xth percentile length or smaller which could have
        /// min Fragsupport
        const SVLocusScanner::Range& pRange(_readScanner.getEvidencePairRange(bamIndex));
        const pos_t minFrag(static_cast<pos_t>(pRange.min));
        const pos_t maxFrag(static_cast<pos_t>(pRange.max));

        const SizeDistribution& fragDistro(_readScanner.getFragSizeDistro(bamIndex));

        const pos_t maxSupportedFrag(maxFrag-pairOpt.minFragSupport);

        const pos_t beginPos(centerPos-maxSupportedFrag);
        const pos_t endPos(centerPos+maxSupportedFrag+1);
#ifdef DEBUG_MEGAPAIR
        log_os << __FUNCTION__ << ": pair scan begin/end: " << beginPos << " " << endPos << "\n";
#endif

        /// This could occur if the fragment distribution is incredibly small --
        /// we effectively can't make use of pairs in this case:
        if (beginPos >= endPos) continue;

        // set bam stream to new search interval:
        bamStream.set_new_region(bp.interval.tid, beginPos, endPos);

        while (bamStream.next())
        {
            const bam_record& bamRead(*(bamStream.get_record_ptr()));

            if (bamRead.is_filter()) continue;
            if (bamRead.is_dup()) continue;
            if (bamRead.is_secondary()) continue;
            if (bamRead.is_supplement()) continue;

            if (bamRead.is_unmapped() || bamRead.is_mate_unmapped()) continue;

            /// check for standard innie orientation:
            if (! is_innie_pair(bamRead)) continue;

#ifdef DEBUG_MEGAPAIR
            log_os << __FUNCTION__ << ": read: " << bamRead << "\n";
#endif

            /// check if fragment is too big or too small:
            const int templateSize(std::abs(bamRead.template_size()));
            if (templateSize < minFrag) continue;
            if (templateSize > maxFrag) continue;

            // count only from the down stream read unless the mate-pos goes past center-pos
            const bool isLeftMost(bamRead.pos() < bamRead.mate_pos());

            // get fragment range:
            pos_t fragBeginRefPos(0);
            if (isLeftMost)
            {
                fragBeginRefPos=bamRead.pos()-1;
            }
            else
            {
                fragBeginRefPos=bamRead.mate_pos()-1;
            }

            const pos_t fragEndRefPos(fragBeginRefPos+templateSize);

            if (fragBeginRefPos > fragEndRefPos)
            {
                using namespace illumina::common;

                std::ostringstream oss;
                oss << "ERROR: Failed to parse fragment range from bam record. Frag begin,end: " << fragBeginRefPos << " " << fragEndRefPos << " bamRecord: " << bamRead << "\n";
                BOOST_THROW_EXCEPTION(LogicException(oss.str()));
            }

            {
                const pos_t fragOverlap(std::min((1+centerPos-fragBeginRefPos), (fragEndRefPos-centerPos)));
#ifdef DEBUG_MEGAPAIR
                log_os << __FUNCTION__ << ": frag begin/end/overlap: " << fragBeginRefPos << " " << fragEndRefPos << " " << fragOverlap << "\n";
#endif
                if (fragOverlap < pairOpt.minFragSupport) continue;
            }

            SVFragmentEvidence& fragment(evidence.getSample(isTumor)[bamRead.qname()]);

            SVFragmentEvidenceRead& evRead(fragment.getRead(bamRead.is_first()));
            setReadEvidence(minMapQ, bamRead, evRead);

            setAlleleFrag(fragDistro, templateSize, fragment.ref.getBp(isBp1));
        }
    }
}



void
SVScorer::
getSVAltPairSupport(
    const PairOptions& pairOpt,
    const SVCandidate& sv,
    SVEvidence& evidence)
{
    bamProcPtr bp1Ptr(new SVScorePairAltProcessor(_isAlignmentTumor, _readScanner, pairOpt, sv, true, evidence));
    bamProcPtr bp2Ptr(new SVScorePairAltProcessor(_isAlignmentTumor, _readScanner, pairOpt, sv, false, evidence));

    std::vector<bamProcPtr> bamProcList;
    bamProcList.push_back(bp1Ptr);
    bamProcList.push_back(bp2Ptr);

    processBamProcList(_bamStreams, bamProcList);
}



void
SVScorer::
getSVRefPairSupport(
    const PairOptions& pairOpt,
    const SVCandidate& sv,
    SVEvidence& evidence)
{
    getSVRefPairSupport(pairOpt, sv.bp1, true, evidence);
    getSVRefPairSupport(pairOpt, sv.bp2, false, evidence);
}



struct SpanReadInfo
{
    SpanReadInfo() :
        isFwdStrand(true),
        readSize(0)
    {}

    GenomeInterval interval;
    bool isFwdStrand;
    unsigned readSize;
};



static
void
getFragInfo(
    const bam_record& localRead,
    SpanReadInfo& local,
    SpanReadInfo& remote)
{
    using namespace ALIGNPATH;

    // local read:
    local.isFwdStrand = localRead.is_fwd_strand();
    local.readSize = localRead.read_size();
    local.interval.tid = localRead.target_id();
    const pos_t localBeginPos(localRead.pos()-1);

    // get cigar:
    path_t localPath;
    bam_cigar_to_apath(localRead.raw_cigar(), localRead.n_cigar(), localPath);

    const pos_t localEndPos(localBeginPos+apath_ref_length(localPath));

    local.interval.range.set_range(localBeginPos,localEndPos);

    // remote read:
    remote.isFwdStrand = localRead.is_mate_fwd_strand();
    remote.readSize = local.readSize;
    remote.interval.tid = localRead.mate_target_id();
    const pos_t remoteBeginPos(localRead.mate_pos()-1);

    // approximate end-point of remote read:
    const pos_t remoteEndPos(remoteBeginPos+localRead.read_size());

    remote.interval.range.set_range(remoteBeginPos,remoteEndPos);
}



/// fill in SpanReadInfo as accurately as possible depending on
/// whether one or both of the read pair's bam records have been found:
static
void
getFragInfo(
    const SVCandidateSetReadPair& pair,
    SpanReadInfo& read1,
    SpanReadInfo& read2)
{
    using namespace ALIGNPATH;

    if (pair.read1.isSet())
    {
        getFragInfo(pair.read1.bamrec, read1, read2);

        if (pair.read2.isSet())
        {
            const bam_record& bamRead2(pair.read2.bamrec);

            read2.readSize = bamRead2.read_size();

            // get cigar:
            path_t apath2;
            bam_cigar_to_apath(bamRead2.raw_cigar(), bamRead2.n_cigar(), apath2);

            read2.interval.range.set_end_pos(read2.interval.range.begin_pos() + apath_ref_length(apath2));
        }
    }
    else if (pair.read2.isSet())
    {
        getFragInfo(pair.read2.bamrec, read2, read1);
    }
    else
    {
        assert(false && "Neither fragment read found");
    }
}



/// read pairs are abstracted to two terminals for the purpose of
/// fragment size estimation in the context of the alternate allele:
/// tid+pos represent one of the two extreme ends of the fragment in
/// genomic chromosome+position coordinates
///
struct SpanTerminal
{
    SpanTerminal() :
        tid(0),
        pos(0),
        isFwdStrand(true),
        readSize(0)
    {}

    int32_t tid;
    pos_t pos;
    bool isFwdStrand;
    unsigned readSize;
};


#ifdef DEBUG_PAIR
static
std::ostream&
operator<<(std::ostream& os, const SpanTerminal& st)
{
    os << "tid: " << st.tid
       << " pos: "<< st.pos
       << " isFwdStrand: " << st.isFwdStrand
       << " readSize: " << st.readSize;
    return os;
}
#endif



/// convert SpanReadInfo to SpanTerminal
static
void
getTerminal(
    const SpanReadInfo& rinfo,
    SpanTerminal& fterm)
{
    fterm.tid = rinfo.interval.tid;
    fterm.isFwdStrand = rinfo.isFwdStrand;
    fterm.pos = ( fterm.isFwdStrand ? rinfo.interval.range.begin_pos() : rinfo.interval.range.end_pos() );
    fterm.readSize = rinfo.readSize;
}



/// double check that a read-pair supports an sv, and if so what is the fragment length prob?
static
void
getFragProb(
    const PairOptions& pairOpt,
    const SVCandidate& sv,
    const SVCandidateSetReadPair& pair,
    const SizeDistribution& fragDistro,
    const bool isStrictMatch,
    bool& isFragSupportSV,
    float& fragProb)
{
#ifdef DEBUG_PAIR
    static const std::string logtag("getFragProb: ");
#endif

    isFragSupportSV=false;
    fragProb=0.;

    SpanReadInfo read1;
    SpanReadInfo read2;
    getFragInfo(pair, read1, read2);

    // define the end-points of fragment:
    SpanTerminal frag1;
    getTerminal(read1,frag1);

    SpanTerminal frag2;
    getTerminal(read2,frag2);

    const pos_t bp1pos(sv.bp1.interval.range.center_pos());
    const pos_t bp2pos(sv.bp2.interval.range.center_pos());

    // match bp to frag
    bool isBpFragReversed(false);

    if (frag1.tid != sv.bp1.interval.tid)
    {
        isBpFragReversed=true;
    }
    else if (frag1.isFwdStrand != (sv.bp1.state == SVBreakendState::RIGHT_OPEN) )
    {
        isBpFragReversed=true;
    }
    else if (frag1.isFwdStrand == frag2.isFwdStrand)
    {
        if ((frag1.pos < frag2.pos) != (bp1pos < bp2pos))
        {
            if (frag1.pos != frag2.pos)
            {
                isBpFragReversed=true;
            }
        }
    }

    if (isBpFragReversed)
    {
        std::swap(frag1,frag2);
#ifdef DEBUG_PAIR
        log_os << logtag << "swapping fragments\n";
#endif
    }

#ifdef DEBUG_PAIR
    log_os << logtag << "pair: " << pair << "\n";
    log_os << logtag << "sv: " << sv << "\n";
    log_os << logtag << "frag1: " << frag1 << "\n";
    log_os << logtag << "frag2: " << frag2 << "\n";
#endif

    // QC the frag/bp matchup:
    {
        std::string errorMsg;
        if (frag1.tid != frag2.tid)
        {
            if (frag1.tid != sv.bp1.interval.tid)
            {
                errorMsg = "Can't match evidence read chrom to sv-candidate bp1.";
            }
            if (frag2.tid != sv.bp2.interval.tid)
            {
                errorMsg = "Can't match evidence read chrom to sv-candidate bp2.";
            }
        }
        else if (frag1.isFwdStrand != frag2.isFwdStrand)
        {
            if ( frag1.isFwdStrand != (sv.bp1.state == SVBreakendState::RIGHT_OPEN) )
            {
                errorMsg = "Can't match evidence read strand to sv-candidate bp1";
            }
            if ( frag2.isFwdStrand != (sv.bp2.state == SVBreakendState::RIGHT_OPEN) )
            {
                errorMsg = "Can't match evidence read strand to sv-candidate bp2";
            }
        }
        else
        {
            if ( (frag1.pos < frag2.pos) != (bp1pos < bp2pos) )
            {
                if (frag1.pos != frag2.pos)
                {
                    errorMsg = "Can't match read pair positions to sv-candidate.";
                }
            }
        }

        if (! errorMsg.empty())
        {
            if (! isStrictMatch) return;

            using namespace illumina::common;

            std::ostringstream oss;
            oss << "ERROR: " << errorMsg  << '\n'
                << "\tcandidate-sv: " << sv
                << "\tread-pair: " << pair
                << '\n';
            BOOST_THROW_EXCEPTION(LogicException(oss.str()));
        }
    }

    pos_t frag1Size(bp1pos-frag1.pos);
    if (! frag1.isFwdStrand) frag1Size *= -1;

    pos_t frag2Size(bp2pos-frag2.pos);
    if (! frag2.isFwdStrand) frag2Size *= -1;

#ifdef DEBUG_PAIR
    log_os << logtag << "frag1size,frag2size: " << frag1Size << " " << frag2Size << "\n";
#endif

    if (frag1Size < pairOpt.minFragSupport) return;
    if (frag2Size < pairOpt.minFragSupport) return;

    fragProb=fragDistro.cdf(frag1Size+frag2Size);
#ifdef DEBUG_PAIR
    log_os << logtag << "cdf: " << fragProb << " final: " << std::min(fragProb, (1-fragProb)) << "\n";
#endif
    fragProb = std::min(fragProb, (1-fragProb));

    /// TODO: any cases where fragProb is 0 or extremely small should be some
    /// sort of mulit-SV error artifact (like a large CIGAR indel in one of the
    /// reads of the pair) try to improve this case -- ideally we can account
    /// for such events.
    ///
    static const float minFragProb(0.0001);
    if (fragProb >= minFragProb)
    {
        isFragSupportSV = true;
    }
}



// count the read pairs supporting the alternate allele in each sample, using data we already produced during candidate generation:
//
void
SVScorer::
processExistingAltPairInfo(
    const PairOptions& pairOpt,
    const SVCandidateSetData& svData,
    const SVCandidate& sv,
    SVEvidence& evidence)
{
    const unsigned minMapQ(_readScanner.getMinMapQ());

    const unsigned bamCount(_bamStreams.size());
    for (unsigned bamIndex(0); bamIndex < bamCount; ++bamIndex)
    {
        const bool isTumor(_isAlignmentTumor[bamIndex]);

        const SizeDistribution& fragDistro(_readScanner.getFragSizeDistro(bamIndex));

        const SVCandidateSetReadPairSampleGroup& svDataGroup(svData.getDataGroup(bamIndex));
        BOOST_FOREACH(const SVCandidateSetReadPair& pair, svDataGroup)
        {
            /// at least one read of the pair must have been found:
            assert(pair.read1.isSet() || pair.read2.isSet());

            // is this read pair associated with this candidateIndex? (each read pair can be associated with multiple candidates)
            unsigned linkIndex(0);
            {
                bool isIndexFound(false);
                BOOST_FOREACH(const SVPairAssociation& sva, pair.svLink)
                {
                    if (sv.candidateIndex == sva.index)
                    {
                        isIndexFound = true;
                        break;
                    }
                    linkIndex++;
                }

                if (! isIndexFound) continue;
            }
            assert(pair.svLink.size() > linkIndex);

            const bool isPairType(SVEvidenceType::isPairType(pair.svLink[linkIndex].evtype));

            /// if the evidence comes from a read pair observation, a very strict matching criteria
            /// is enforced between this pair and the SV candidate. If the read pair association comes from
            /// a CIGAR string for instance, the pair will not necessarily support the candidate
            ///
            const bool isStrictMatch(isPairType);

            const std::string qname(pair.qname());

#ifdef DEBUG_PAIR
            static const std::string logtag("processExistingAltPairInfo: ");
            log_os << logtag << "Finding alt pair evidence for svIndex: " << sv.candidateIndex << "  qname: " << qname << "\n";
#endif

            SVFragmentEvidence& fragment(evidence.getSample(isTumor)[qname]);
            SVFragmentEvidenceAllele& alt(fragment.alt);

            if (pair.read1.isSet())
            {
                setReadEvidence(minMapQ, pair.read1.bamrec, fragment.read1);
            }

            if (pair.read2.isSet())
            {
                setReadEvidence(minMapQ, pair.read2.bamrec, fragment.read2);
            }

            /// get fragment prob, and possibly withdraw fragment support based on refined sv breakend coordinates:
            bool isFragSupportSV(false);
            float fragProb(0);
            getFragProb(pairOpt, sv, pair, fragDistro, isStrictMatch, isFragSupportSV, fragProb);

            if (! isFragSupportSV) continue;

            /// TODO: if fragProb is zero this should be a bug -- follow-up to see if we can make this an assert(fragProb > 0.) instead
            if (fragProb <= 0.) continue;

            // for all large spanning events -- we don't test for pair support of the two breakends separately -- this could be
            // beneficial if there was an unusually large insertion associated with the event. For now we approximate that
            // these events will mostly not have very large insertions.
            //
            alt.bp1.isFragmentSupport = true;
            alt.bp1.fragLengthProb = fragProb;

            alt.bp2.isFragmentSupport = true;
            alt.bp2.fragLengthProb = fragProb;
        }
    }
}



void
SVScorer::
getSVPairSupport(
    const SVCandidateSetData& svData,
    const SVCandidateAssemblyData& assemblyData,
    const SVCandidate& sv,
    SVEvidence& evidence)
{
    static const PairOptions pairOpt;

#ifdef DEBUG_PAIR
    static const std::string logtag("getSVPairSupport: ");
    log_os << logtag << "starting alt pair search for sv: " << sv << "\n";
#endif

    if (assemblyData.isCandidateSpanning)
    {
        // count the read pairs supporting the alternate allele in each sample
        // using data we already produced during candidate generation:
        //
        processExistingAltPairInfo(pairOpt, svData, sv, evidence);
    }
    else
    {
        // for SVs which were assembled without a pair-driven prior hypothesis,
        // we need to go back to the bam and and find any supporting alt read-pairs
        getSVAltPairSupport(pairOpt, sv, evidence);
    }

    // count the read pairs supporting the reference allele on each breakend in each sample:
    //
    getSVRefPairSupport(pairOpt, sv, evidence);
}

