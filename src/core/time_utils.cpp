#include "core/time_utils.h"

#include <cstdio>
#include <sstream>
#include <vector>

std::string FormatTime(double seconds) {
    int total = static_cast<int>(seconds);
    int milliseconds = static_cast<int>((seconds - total) * 1000);
    int hours = total / 3600;
    int minutes = (total % 3600) / 60;
    int remainingSeconds = total % 60;
    char buffer[64];
    if (hours > 0) {
        std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03d",
                      hours, minutes, remainingSeconds, milliseconds);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%02d:%02d.%03d",
                      minutes, remainingSeconds, milliseconds);
    }
    return buffer;
}

double ParseTime(const std::string& value) {
    std::vector<std::string> parts;
    std::stringstream stream(value);
    std::string part;
    while (std::getline(stream, part, ':')) {
        parts.push_back(part);
    }

    try {
        if (parts.size() == 3) {
            return std::stod(parts[0]) * 3600.0 +
                   std::stod(parts[1]) * 60.0 + std::stod(parts[2]);
        }
        if (parts.size() == 2) {
            return std::stod(parts[0]) * 60.0 + std::stod(parts[1]);
        }
        if (parts.size() == 1) {
            return std::stod(parts[0]);
        }
    } catch (...) {
        return 0.0;
    }
    return 0.0;
}
