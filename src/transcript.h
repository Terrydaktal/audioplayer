#ifndef TRANSCRIPT_H
#define TRANSCRIPT_H

#include <string>
#include <vector>

struct Segment {
    double start;
    double end;
    std::string text;
};

class TranscriptHandler {
public:
    TranscriptHandler();
    ~TranscriptHandler();

    bool Load(const std::string& audioPath, const std::string& transcriptDir);
    const std::vector<Segment>& GetSegments() const { return segments; }
    void Clear() { segments.clear(); }

private:
    std::vector<Segment> segments;
};

#endif
