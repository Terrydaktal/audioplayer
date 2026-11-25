# Interactive Audio Player with Transcript & Cropping

A high-performance, interactive C++ audio player designed for navigating long recordings with SQLite-backed transcripts. Built with SDL2, Dear ImGui, and FFmpeg.

## 🚀 Key Features

- **Asynchronous Audio Engine**: Mandatory FFmpeg-to-WAV transcoding ensures 100% compatibility with all formats (MP3, AAC, M4A, etc.) and provides sample-accurate seeking without freezing the UI.
- **Deep Media Probing**: Uses `ffprobe` to detect real containers and codecs (e.g., identifies if an `.mp3` is actually an `.m4a` file).
- **Advanced Transcript Interface**:
  - **Timestamp Navigation**: Click any transcript segment to jump the audio to that exact moment.
  - **Ctrl+F Search**: Live search with match cycling, cyan highlighting, and automatic scrolling to results.
  - **Indexed Global Search**: SQLite FTS5 retrieves timestamped transcript segments, expands nearby hits into context windows, and the exact `transcript-window` adapter uses fuzzy-rank's `message` adapter to order them.
  - **Drag-to-Copy**: Highlight ranges of the transcript by dragging your mouse and use `Ctrl+C` to copy high-precision timestamped text.
- **Smart Cropping**: FFmpeg-powered cropping logic that automatically detects if it can perform a **Lossless Copy** (if formats match) or a **High-Quality Re-encode** (if converting formats).
- **Responsive UI/UX**:
  - **Sidebar Browser**: Live-filtering file list for managing large recording folders.
  - **Dynamic Fonts**: Search and apply system fonts (`.ttf`/`.otf`) at any size with real-time scaling.
  - **High Precision**: Full `HH:MM:SS.mmm` precision on all timers and transcript markers.

## 📂 Project Structure

```text
/
├── CMakeLists.txt          # Build configuration
├── src/
│   ├── main.cpp                   # CLI options and process entry point
│   ├── app/                       # Application lifecycle, state, and worker orchestration
│   ├── audio/                     # Playback, FFmpeg process, media probe, and crop services
│   ├── core/                      # Shared time formatting and parsing
│   ├── platform/                  # Font discovery and filesystem/audio-folder helpers
│   ├── transcripts/                # Public transcript API, JSON/SQLite loading, FTS search, ranking
│   ├── ui/                        # Menu bar, sidebar, player, global-search, and event views
│   ├── fuzzy_rank_bridge.h         # C ABI for the Rust relevance ranker
│   ├── ffmpeg_utils.h              # Backward-compatible crop wrapper
│   └── test_transcript.cpp         # Transcript/database behavior tests
├── rust/fuzzy_rank_bridge/ # Static Rust bridge to ../fuzzy-rank
├── external/               # Header-only dependencies (miniaudio, json, imgui)
└── build/                  # Build artifacts (ignored)
```

## 🛠️ Prerequisites

Ensure you have the following installed on your Linux system:
- **CMake** (v3.10+)
- **GCC/G++** (v11+ for C++17 support)
- **SDL2 Development Libraries**: `sudo apt install libsdl2-dev`
- **FFmpeg**: `sudo apt install ffmpeg`
- **Rust/Cargo** and the sibling `~/Dev/fuzzy-rank` crate
- **SQLite 3** built with FTS5 support
- **OpenSSL/libcrypto** (used for transcript fingerprints)

The SQLite database is the source of truth for search and transcript viewing.
`diarise --database PATH` writes transcript metadata and normalized
`segments(start_time, end_time, text)` rows directly to it, while SQLite FTS5
maintains a trigram index for fast global substring search. Legacy JSON files are
still imported when present, but are not required: selecting a database-only
result loads its segments from SQLite even when the recording and JSON file are
missing. Imports cache nanosecond source stats, a normalized segment-content
fingerprint, and the complete JSON metadata apart from the repeated `segments`
array. A metadata-only JSON change updates the metadata without rebuilding the
segments or FTS index; duplicate content is recognized even when the JSON
metadata or filename differs, and direct database rows from `diarise` remain
authoritative. Sync is additive/update-only: removing a JSON source does not
remove its database transcript, so database-only viewing and search remain
available. Search anchors are grouped into overlapping windows of nearby
segments before the exact `fuzzy-rank` message adapter ranks them. Recording
timestamps come from each transcript's `anchor_time` metadata, with the
audio-file mtime used only as a fallback when that metadata is absent.

## 🔨 Build Instructions

```bash
# Clone the repository
git clone https://github.com/Terrydaktal/audioplayer.git
cd audioplayer

# Create build directory and compile
mkdir -p build && cd build
cmake ..
make -j$(nproc)

# Run the player
./audioplayer
```

## 📖 Usage Guide

1. **Import Folder**: Set your "Audio Folder" and "Transcript Folder" in the sidebar.
2. **Filter & Load**: Use the "Filter Files" box to find a recording. Click a file to load it (transcoding happens in the background).
3. **Search**: Press `Ctrl+F` to search the transcript. Use `Enter` or the `Next/Prev` buttons to cycle through matches.
4. **Crop**: 
   - Use "Set to Current" to mark start/end points while listening.
   - Adjust the suggested "Output Path" extension if needed.
   - Click "Crop & Save" for an instant or high-quality export.
5. **Copy**: Drag over transcript lines and press `Ctrl+C` to copy text with timestamps.

## ⚖️ License

MIT License - Created by Terrydaktal.
