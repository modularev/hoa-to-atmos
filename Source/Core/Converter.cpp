#include "Converter.h"

#include "AdmBwfWriter.h"

#include <cmath>
#include <memory>

namespace hoa
{
namespace
{
    std::unique_ptr<juce::AudioFormatReader> openReader (const juce::File& file, juce::String& error)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (file));
        if (r == nullptr)
            error = "Could not read audio file (expected WAV / RF64 / AIFF / CAF): " + file.getFullPathName();
        return r;
    }

    /** 2nd-order Butterworth low-pass; two in series form an LR4 crossover branch. */
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

        void setLowPass (double fs, double fc)
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * fc / fs;
            const double cw = std::cos (w), alpha = std::sin (w) / (2.0 * std::sqrt (0.5));
            const double a0 = 1.0 + alpha;
            b0 = (1.0 - cw) * 0.5 / a0;
            b1 = (1.0 - cw) / a0;
            b2 = b0;
            a1 = -2.0 * cw / a0;
            a2 = (1.0 - alpha) / a0;
            z1 = z2 = 0;
        }

        float process (float x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return (float) y;
        }
    };

    double toDb (double g) { return g > 0 ? 20.0 * std::log10 (g) : -200.0; }
} // namespace

InputInfo probeInput (const juce::File& file)
{
    InputInfo info;
    auto reader = openReader (file, info.error);
    if (reader == nullptr)
        return info;
    info.formatName = reader->getFormatName();
    info.numChannels = (int) reader->numChannels;
    info.sampleRate = reader->sampleRate;
    info.lengthFrames = reader->lengthInSamples;
    info.bitsPerSample = (int) reader->bitsPerSample;
    info.order = std::min (orderForChannelCount (info.numChannels), kMaxOrder);
    info.ok = info.order >= 1;
    if (! info.ok)
        info.error = "Need at least 4 channels (1st order ambisonics), got " + juce::String (info.numChannels);
    return info;
}

ConvertResult convert (const ConvertOptions& opt, ProgressFn progress, LogFn log)
{
    ConvertResult res;
    auto say = [&] (const juce::String& s) { if (log) log (s); };

    juce::String err;
    auto reader = openReader (opt.input, err);
    if (reader == nullptr)
    {
        res.message = err;
        return res;
    }

    const int inChannels = (int) reader->numChannels;
    const int availableOrder = std::min (orderForChannelCount (inChannels), kMaxOrder);
    const int order = opt.order > 0 ? opt.order : availableOrder;
    if (availableOrder < 1)
    {
        res.message = "Input has " + juce::String (inChannels) + " channels; need at least 4 (1st order)";
        return res;
    }
    if (order > availableOrder)
    {
        res.message = "Requested order " + juce::String (order) + " but the input only carries order " + juce::String (availableOrder);
        return res;
    }
    const int K = numChannelsForOrder (order);
    if (inChannels > K)
        say ("Note: input has " + juce::String (inChannels) + " channels; using the first " + juce::String (K)
             + " (order " + juce::String (order) + ").");

    const int sr = (int) std::lround (reader->sampleRate);
    if (sr != 48000 && sr != 96000)
    {
        res.message = "Dolby Atmos ADM BWF needs 48 kHz or 96 kHz; the input is " + juce::String (reader->sampleRate)
                      + " Hz. Render the ambisonic mix from Reaper at 48 kHz.";
        return res;
    }
    const int64_t numFrames = reader->lengthInSamples;
    if (numFrames <= 0)
    {
        res.message = "Input file is empty";
        return res;
    }

    // --- decoder
    const auto speakers = makeLayout (opt.layout, opt.useBed);
    DecoderOptions dopt;
    dopt.order = order;
    dopt.normalization = opt.normalization;
    dopt.maxRE = opt.maxRE;
    dopt.foldBelowHorizon = opt.foldBelowHorizon;
    dopt.yawDeg = opt.yawDeg;
    DecoderReport rep;
    DecoderMatrix D;
    try
    {
        D = designAllRad (speakers, dopt, &rep);
    }
    catch (const std::exception& e)
    {
        res.message = juce::String ("Decoder design failed: ") + e.what();
        return res;
    }

    int numBed = 0, numObj = 0, lfeIndex = -1;
    for (size_t s = 0; s < speakers.size(); ++s)
    {
        if (speakers[s].isBed) ++numBed; else ++numObj;
        if (speakers[s].isLFE) lfeIndex = (int) s;
    }
    say ("Input: " + juce::String (inChannels) + " ch, order " + juce::String (order) + " "
         + (opt.normalization == Normalization::SN3D ? "SN3D" : "N3D") + ", " + juce::String (sr) + " Hz, "
         + juce::String ((double) numFrames / sr, 2) + " s");
    say ("Layout: " + juce::String (toString (opt.layout)) + " -> " + juce::String (numBed) + " bed channels + "
         + juce::String (numObj) + " objects (" + juce::String ((int) speakers.size()) + " tracks)");
    say ("AllRAD " + juce::String (opt.maxRE ? "max-rE" : "basic") + ": " + juce::String (rep.numTriangles) + " triangles, rE mean "
         + juce::String (rep.meanRe, 3) + ", direction error mean " + juce::String (rep.meanAngularErrorDeg, 1) + " deg / max "
         + juce::String (rep.maxAngularErrorDeg, 1) + " deg, loudness spread " + juce::String (rep.energySpreadDb, 2) + " dB (upper hemisphere)");

    const bool doLfe = opt.lfeEnabled && lfeIndex >= 0;
    if (opt.lfeEnabled && lfeIndex < 0)
        say ("Note: LFE feed ignored (no bed in this configuration).");
    const float lfeGain = (float) juce::Decibels::decibelsToGain (opt.lfeGainDb);
    const float outGain = (float) juce::Decibels::decibelsToGain (opt.outputGainDb);

    // --- streaming render
    const int S = D.numOutputs;
    const int blockSize = 8192;
    juce::AudioBuffer<float> in (inChannels, blockSize), out (S, blockSize);
    Biquad lp1, lp2;

    // returns false if cancelled / failed
    auto runPass = [&] (const juce::String& stage, double progStart, double progSpan,
                        const std::function<bool (const juce::AudioBuffer<float>&, int)>& sink) {
        lp1.setLowPass (sr, opt.lfeCutoffHz);
        lp2.setLowPass (sr, opt.lfeCutoffHz);
        for (int64_t pos = 0; pos < numFrames; pos += blockSize)
        {
            const int n = (int) std::min<int64_t> (blockSize, numFrames - pos);
            if (! reader->read (in.getArrayOfWritePointers(), inChannels, pos, n))
            {
                res.message = "Read error at frame " + juce::String (pos);
                return false;
            }
            out.clear();
            for (int s = 0; s < S; ++s)
            {
                float* dst = out.getWritePointer (s);
                for (int k = 0; k < K; ++k)
                {
                    const float g = D.at (s, k) * outGain;
                    if (g != 0.0f)
                        juce::FloatVectorOperations::addWithMultiply (dst, in.getReadPointer (k), g, n);
                }
            }
            if (doLfe)
            {
                float* lfe = out.getWritePointer (lfeIndex);
                const float* w = in.getReadPointer (0);
                for (int i = 0; i < n; ++i)
                    lfe[i] = lp2.process (lp1.process (w[i])) * lfeGain * outGain;
            }
            if (! sink (out, n))
                return false;
            if (progress && ! progress (progStart + progSpan * (double) (pos + n) / (double) numFrames, stage))
            {
                res.message = "Cancelled";
                return false;
            }
        }
        return true;
    };

    // Pass 1: peak
    float peak = 0;
    if (! runPass ("Analysing", 0.0, 0.3, [&] (const juce::AudioBuffer<float>& b, int n) {
            for (int s = 0; s < S; ++s)
            {
                const auto range = juce::FloatVectorOperations::findMinAndMax (b.getReadPointer (s), n);
                peak = std::max ({ peak, range.getEnd(), -range.getStart() });
            }
            return true;
        }))
        return res;

    float trim = 1.0f;
    const float ceiling = (float) juce::Decibels::decibelsToGain (-0.1);
    if (peak > ceiling)
    {
        if (opt.autoTrim)
        {
            trim = ceiling / peak;
            say ("Decoded peak " + juce::String (toDb (peak), 2) + " dBFS -> trimming all channels by "
                 + juce::String (toDb (trim), 2) + " dB to avoid clipping.");
        }
        else
            say ("Warning: decoded peak " + juce::String (toDb (peak), 2) + " dBFS will clip (auto-trim disabled).");
    }
    else
        say ("Decoded peak " + juce::String (toDb (peak), 2) + " dBFS.");
    res.peakDb = toDb (peak * trim);
    res.appliedTrimDb = toDb (trim);

    // Pass 2: write
    AdmBwfWriter writer;
    std::string werr;
    if (! writer.open (opt.output.getFullPathName().toStdString(), speakers, sr, numFrames,
                       opt.input.getFileNameWithoutExtension().toStdString(), werr))
    {
        res.message = werr;
        return res;
    }

    juce::OwnedArray<juce::AudioFormatWriter> stems;
    if (opt.stemsDirectory != juce::File())
    {
        opt.stemsDirectory.createDirectory();
        juce::WavAudioFormat wav;
        juce::String positions = "# HOA -> Atmos virtual speakers (Atmos room cube: X -1 L..+1 R, Y -1 back..+1 front, Z 0..1)\n";
        for (int s = 0; s < S; ++s)
        {
            const auto& spk = speakers[(size_t) s];
            const auto f = opt.stemsDirectory.getChildFile (juce::String (s + 1).paddedLeft ('0', 2) + "_" + spk.name + ".wav");
            f.deleteFile();
            auto stream = std::make_unique<juce::FileOutputStream> (f);
            auto* w = stream->openedOk() ? wav.createWriterFor (stream.get(), sr, 1, 24, {}, 0) : nullptr;
            if (w == nullptr)
            {
                res.message = "Could not create stem file " + f.getFullPathName();
                return res;
            }
            stream.release();
            stems.add (w);
            positions << f.getFileName() << "\t" << (spk.isBed ? "bed " + spk.speakerLabel : juce::String ("object"))
                      << "\taz " << spk.azimuthDeg << "\tel " << spk.elevationDeg
                      << "\tX " << spk.cube.x << "\tY " << spk.cube.y << "\tZ " << spk.cube.z << "\n";
        }
        opt.stemsDirectory.getChildFile ("positions.txt").replaceWithText (positions);
    }

    const bool ok = runPass ("Writing", 0.3, 0.7, [&] (const juce::AudioBuffer<float>& b, int n) {
        auto& mb = const_cast<juce::AudioBuffer<float>&> (b);
        if (trim != 1.0f)
            mb.applyGain (0, n, trim);
        if (! writer.write (b.getArrayOfReadPointers(), n))
        {
            res.message = "Write error (disk full?)";
            return false;
        }
        for (int s = 0; s < stems.size(); ++s)
        {
            const float* p = b.getReadPointer (s);
            if (! stems[s]->writeFromFloatArrays (&p, 1, n))
            {
                res.message = "Write error on stems";
                return false;
            }
        }
        return true;
    });
    stems.clear();

    std::string cerr;
    const bool closed = writer.close (cerr);
    if (! ok)
    {
        opt.output.deleteFile();
        return res;
    }
    if (! closed)
    {
        res.message = cerr.empty() ? juce::String ("Failed to finalise output") : juce::String (cerr);
        return res;
    }
    res.ok = true;
    res.message = "Wrote " + opt.output.getFullPathName();
    say (res.message);
    if (progress)
        progress (1.0, "Done");
    return res;
}
} // namespace hoa
