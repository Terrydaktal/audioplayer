#ifndef TIME_UTILS_H
#define TIME_UTILS_H

#include <string>

std::string FormatTime(double seconds);
double ParseTime(const std::string& value);

#endif
