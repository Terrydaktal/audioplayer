#ifndef TRANSCRIPT_TYPES_H
#define TRANSCRIPT_TYPES_H

#include <string>

struct Segment {
    double start = 0.0;
    double end = 0.0;
    std::string text;
};

struct GlobalSearchResult {
    std::string audioFilename;
    std::string transcriptFilename;
    std::string recordingTime;
    bool recordingTimeUncertain = false;
    double start = 0.0;
    double end = 0.0;
    std::string text;
};

#endif
