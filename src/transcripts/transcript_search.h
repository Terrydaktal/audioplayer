#ifndef TRANSCRIPT_SEARCH_H
#define TRANSCRIPT_SEARCH_H

#include "transcripts/transcript.h"

void FindTranscriptMatches(const std::vector<Segment>& segments,
                           const std::string& query,
                           std::vector<int>& matches,
                           int& currentMatch);

#endif
