#ifndef FUZZY_RANK_BRIDGE_H
#define FUZZY_RANK_BRIDGE_H

#include <cstddef>

struct FuzzyRankCandidate {
    const char* text;
    double score;
};

extern "C" std::size_t fuzzy_rank_messages(
    const char* query,
    const FuzzyRankCandidate* candidates,
    std::size_t candidateCount,
    std::size_t* outputIndices,
    std::size_t outputCapacity
);

// Exact transcript-window adapter: each candidate contains one ordered,
// context-expanded transcript window. The bridge treats the window as one
// fuzzy-rank message field so a phrase can continue across Whisper boundaries.
extern "C" std::size_t fuzzy_rank_transcript_windows(
    const char* query,
    const FuzzyRankCandidate* candidates,
    std::size_t candidateCount,
    std::size_t* outputIndices,
    std::size_t outputCapacity
);

#endif
