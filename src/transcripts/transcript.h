#ifndef TRANSCRIPTS_TRANSCRIPT_H
#define TRANSCRIPTS_TRANSCRIPT_H

#include <string>
#include <vector>

#include "transcripts/transcript_types.h"

class TranscriptHandler {
public:
    TranscriptHandler();
    ~TranscriptHandler();

    bool Load(const std::string& audioPath, const std::string& transcriptDir);
    bool LoadTranscriptFile(const std::string& transcriptPath);
    bool LoadTranscriptDatabase(const std::string& databasePath, const std::string& transcriptKey);
    const std::vector<Segment>& GetSegments() const { return segments; }
    void Clear() { segments.clear(); }

private:
    std::vector<Segment> segments;
};

std::string TranscriptToAudioFilename(const std::string& jsonFilename);
std::vector<GlobalSearchResult> SearchAllTranscripts(const std::string& transcriptDir,
                                                     const std::string& query);
void SyncTranscriptDatabase(const std::string& transcriptDir);

#endif
