#pragma once

#include "AllRadDecoder.h"
#include "AtmosLayout.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <functional>

namespace hoa
{
struct InputInfo
{
    bool ok = false;
    juce::String error;
    juce::String formatName;
    int numChannels = 0;
    int order = -1;        // highest full order that fits the channel count
    double sampleRate = 0;
    int64_t lengthFrames = 0;
    int bitsPerSample = 0;
};

struct ConvertOptions
{
    juce::File input, output;
    LayoutPreset layout = LayoutPreset::Extended;
    bool useBed = true;
    Normalization normalization = Normalization::SN3D;
    int order = -1; // -1: use the highest order the input supports
    bool maxRE = true;
    bool foldBelowHorizon = true;
    double yawDeg = 0;
    bool lfeEnabled = false;
    double lfeGainDb = -10.0;
    double lfeCutoffHz = 120.0;
    bool autoTrim = true; // scale down to -0.1 dBFS if the decode would clip
    double outputGainDb = 0;
    juce::File stemsDirectory; // if set, also write one mono WAV per output channel
};

struct ConvertResult
{
    bool ok = false;
    juce::String message;
    double peakDb = -200;
    double appliedTrimDb = 0;
};

using ProgressFn = std::function<bool (double progress01, const juce::String& stage)>; // return false to cancel
using LogFn = std::function<void (const juce::String&)>;

InputInfo probeInput (const juce::File& file);

ConvertResult convert (const ConvertOptions& options, ProgressFn progress = {}, LogFn log = {});
} // namespace hoa
