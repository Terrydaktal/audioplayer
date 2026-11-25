#include "transcripts/transcript_fingerprint.h"

#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

#include <openssl/evp.h>

namespace {

void AppendField(std::ostringstream& stream, const std::string& value) {
    stream << value.size() << ':' << value;
}

void AppendDouble(std::ostringstream& stream, double value) {
    stream << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
}

} // namespace

std::string Sha256Hex(const std::string& value) {
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (!context) throw std::runtime_error("Unable to allocate SHA-256 context");

    unsigned char digest[EVP_MAX_MD_SIZE] = {};
    unsigned int digestLength = 0;
    const bool initialized = EVP_DigestInit_ex(context, EVP_sha256(), nullptr) == 1;
    const bool updated = initialized && EVP_DigestUpdate(context, value.data(), value.size()) == 1;
    const bool finalized = updated && EVP_DigestFinal_ex(context, digest, &digestLength) == 1;
    EVP_MD_CTX_free(context);
    if (!finalized) throw std::runtime_error("Unable to calculate SHA-256 digest");

    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (unsigned int index = 0; index < digestLength; ++index) {
        result << std::setw(2) << static_cast<unsigned int>(digest[index]);
    }
    return result.str();
}

std::string TranscriptContentFingerprint(const std::string& audioFilename,
                                         const std::vector<Segment>& segments) {
    std::ostringstream canonical;
    AppendField(canonical, audioFilename);
    canonical << '|';
    canonical << segments.size() << '|';
    for (const auto& segment : segments) {
        AppendDouble(canonical, segment.start);
        canonical << '|';
        AppendDouble(canonical, segment.end);
        canonical << '|';
        AppendField(canonical, segment.text);
        canonical << ';';
    }
    return Sha256Hex(canonical.str());
}
