#pragma once

#include "AtmosLayout.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace hoa
{
/** Writes a 24-bit PCM ADM BWF (ITU-R BS.2088 / BS.2076) in the shape of the Dolby Atmos
    master ADM profile: an optional DirectSpeakers bed plus static cartesian objects.
    Switches to RF64 automatically when the data exceeds 4 GB. */
class AdmBwfWriter
{
public:
    ~AdmBwfWriter();

    bool open (const std::string& path,
               const std::vector<VirtualSpeaker>& channels,
               int sampleRate,
               int64_t numFrames,
               const std::string& programmeName,
               std::string& error);

    /** Write numFrames frames of non-interleaved float data (one pointer per channel). */
    bool write (const float* const* channelData, int numFrames);

    /** Finalise the file. Returns false if fewer frames than announced were written. */
    bool close (std::string& error);

    /** The axml chunk text (exposed for tests / debugging). */
    static std::string buildAxml (const std::vector<VirtualSpeaker>& channels,
                                  int sampleRate,
                                  int64_t numFrames,
                                  const std::string& programmeName);

private:
    std::ofstream file;
    int numChannels = 0;
    int64_t framesExpected = 0, framesWritten = 0;
    std::vector<uint8_t> scratch;
};
} // namespace hoa
