# Interactive Audio Player with Transcript & Cropping

A high-performance, interactive C++ audio player designed for navigating long recordings with JSON transcripts. Built with SDL2, Dear ImGui, and FFmpeg.

## 🚀 Key Features

- **Asynchronous Audio Engine**: Mandatory FFmpeg-to-WAV transcoding ensures 100% compatibility with all formats (MP3, AAC, M4A, etc.) and provides sample-accurate seeking without freezing the UI.
- **Deep Media Probing**: Uses `ffprobe` to detect real containers and codecs (e.g., identifies if an `.mp3` is actually an `.m4a` file).
- **Advanced Transcript Interface**:
  - **Timestamp Navigation**: Click any transcript segment to jump the audio to that exact moment.
  - **Ctrl+F Search**: Live search with match cycling, cyan highlighting, and automatic scrolling to results.
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
│   ├── main.cpp            # UI logic and application loop
│   ├── audio_player.cpp/h  # Async FFmpeg-based audio engine
│   ├── transcript.cpp/h    # JSON transcript parser
│   ├── ffmpeg_utils.h      # Cropping and command helpers
│   └── test_transcript.cpp # Unit tests for parser
├── external/               # Header-only dependencies (miniaudio, json, imgui)
└── build/                  # Build artifacts (ignored)
```

## 🛠️ Prerequisites

Ensure you have the following installed on your Linux system:
- **CMake** (v3.10+)
- **GCC/G++** (v11+ for C++17 support)
- **SDL2 Development Libraries**: `sudo apt install libsdl2-dev`
- **FFmpeg**: `sudo apt install ffmpeg`

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
