#ifndef TRANSCRIPT_FINGERPRINT_H
#define TRANSCRIPT_FINGERPRINT_H

#include <string>
#include <vector>

#include "transcripts/transcript_types.h"

std::string Sha256Hex(const std::string& value);
std::string TranscriptContentFingerprint(const std::string& audioFilename,
                                         const std::vector<Segment>& segments);

#endif
