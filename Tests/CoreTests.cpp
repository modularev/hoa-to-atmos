// Self-checking tests for the HOA -> Atmos core. Exit code 0 == all passed.
#include "../Source/Core/AdmBwfWriter.h"
#include "../Source/Core/Converter.h"

#include <juce_events/juce_events.h>

#include <cmath>
#include <cstdio>
#include <random>

namespace
{
int failures = 0;

void check (bool cond, const char* what)
{
    std::printf ("  [%s] %s\n", cond ? " ok " : "FAIL", what);
    if (! cond)
        ++failures;
}

constexpr double kDeg = 3.14159265358979323846 / 180.0;

void testSphericalHarmonics()
{
    std::printf ("Spherical harmonics\n");
    const int N = 7, K = hoa::numChannelsForOrder (N);
    const auto grid = hoa::fibonacciSphere (20000);
    std::vector<double> gram ((size_t) K * K, 0.0), y ((size_t) K);
    for (const auto& p : grid)
    {
        const double az = std::atan2 (p[1], p[0]), el = std::asin (p[2]);
        hoa::evaluate (N, az, el, hoa::Normalization::N3D, y.data());
        for (int i = 0; i < K; ++i)
            for (int j = 0; j < K; ++j)
                gram[(size_t) (i * K + j)] += y[(size_t) i] * y[(size_t) j] / (double) grid.size();
    }
    double maxErr = 0;
    for (int i = 0; i < K; ++i)
        for (int j = 0; j < K; ++j)
            maxErr = std::max (maxErr, std::abs (gram[(size_t) (i * K + j)] - (i == j ? 1.0 : 0.0)));
    std::printf ("    N3D orthonormality max error %.4f\n", maxErr);
    check (maxErr < 0.01, "N3D harmonics orthonormal up to order 7");

    // AmbiX first order: W=1, Y=sin(az)cos(el), Z=sin(el), X=cos(az)cos(el)
    auto v = hoa::evaluate (1, 90 * kDeg, 0, hoa::Normalization::SN3D);
    check (std::abs (v[0] - 1) < 1e-12 && std::abs (v[1] - 1) < 1e-12 && std::abs (v[2]) < 1e-12 && std::abs (v[3]) < 1e-12,
           "SN3D first order matches AmbiX for a source at +90 (left)");
    v = hoa::evaluate (1, 0, 90 * kDeg, hoa::Normalization::SN3D);
    check (std::abs (v[2] - 1) < 1e-12, "SN3D Z == 1 at the zenith");
}

void testCubeMapping()
{
    std::printf ("Atmos cube mapping\n");
    auto near = [] (hoa::CubePosition p, double x, double y, double z) {
        return std::abs (p.x - x) < 1e-9 && std::abs (p.y - y) < 1e-9 && std::abs (p.z - z) < 1e-9;
    };
    check (near (hoa::polarToCube (30, 0), -1, 1, 0), "L  (+30)  -> front-left corner");
    check (near (hoa::polarToCube (-30, 0), 1, 1, 0), "R  (-30)  -> front-right corner");
    check (near (hoa::polarToCube (0, 0), 0, 1, 0), "C         -> front centre");
    check (near (hoa::polarToCube (90, 0), -1, 0, 0), "Lss (+90) -> left wall middle");
    check (near (hoa::polarToCube (-135, 0), 1, -1, 0), "Rrs (-135)-> rear-right corner");
    check (near (hoa::polarToCube (180, 0), 0, -1, 0), "rear centre");
    check (near (hoa::polarToCube (45, 45), -1, 1, 1), "Ltf (+45, el 45) -> ceiling front-left");
    check (near (hoa::polarToCube (-135, 45), 1, -1, 1), "Rtr (-135, el 45) -> ceiling rear-right");
    check (near (hoa::polarToCube (0, 90), 0, 0, 1), "zenith -> ceiling centre");
}

void testDecoder (hoa::LayoutPreset preset, int order, double maxSpreadDb, double maxMeanErrDeg)
{
    std::printf ("AllRAD decoder, layout %s, order %d\n", hoa::toString (preset), order);
    const auto spk = hoa::makeLayout (preset, true);
    hoa::DecoderOptions opt;
    opt.order = order;
    hoa::DecoderReport rep;
    const auto D = hoa::designAllRad (spk, opt, &rep);
    std::printf ("    %d outputs, %d triangles, rE %.3f, err mean %.1f / max %.1f deg, spread %.2f dB\n", D.numOutputs,
                 rep.numTriangles, rep.meanRe, rep.meanAngularErrorDeg, rep.maxAngularErrorDeg, rep.energySpreadDb);

    int correct = 0, total = 0;
    for (size_t s = 0; s < spk.size(); ++s)
    {
        if (spk[s].isLFE)
            continue;
        const auto y = hoa::evaluate (order, spk[s].azimuthDeg * kDeg, spk[s].elevationDeg * kDeg, hoa::Normalization::SN3D);
        int best = -1;
        double bestE = -1, sumE = 0;
        for (int o = 0; o < D.numOutputs; ++o)
        {
            double v = 0;
            for (int k = 0; k < D.numInputs; ++k)
                v += D.at (o, k) * y[(size_t) k];
            sumE += v * v;
            if (v * v > bestE)
            {
                bestE = v * v;
                best = o;
            }
        }
        ++total;
        if (best == (int) s)
            ++correct;
        else
            std::printf ("    source at %s loudest on %s\n", spk[s].name.c_str(), spk[(size_t) best].name.c_str());
    }
    if (order >= 3) // first order is too blurry to single out one of 20 speakers
        check (correct == total, "a source at each speaker position is loudest on that speaker");
    check (rep.energySpreadDb < maxSpreadDb, "loudness roughly direction-independent (upper hemisphere)");
    check (rep.meanAngularErrorDeg < maxMeanErrDeg, "energy-vector direction error small");
    for (int o = 0; o < D.numOutputs; ++o)
        if (spk[(size_t) o].isLFE)
        {
            bool zero = true;
            for (int k = 0; k < D.numInputs; ++k)
                zero = zero && D.at (o, k) == 0.0f;
            check (zero, "LFE row of the matrix is silent");
        }
}

void testYaw()
{
    std::printf ("Yaw\n");
    const auto spk = hoa::makeLayout (hoa::LayoutPreset::Atmos714, true);
    hoa::DecoderOptions opt;
    opt.order = 5;
    opt.yawDeg = 90; // front source should land on the left side speaker
    const auto D = hoa::designAllRad (spk, opt);
    const auto y = hoa::evaluate (5, 0, 0, hoa::Normalization::SN3D);
    int best = -1;
    double bestE = -1;
    for (int o = 0; o < D.numOutputs; ++o)
    {
        double v = 0;
        for (int k = 0; k < D.numInputs; ++k)
            v += D.at (o, k) * y[(size_t) k];
        if (v * v > bestE) { bestE = v * v; best = o; }
    }
    check (spk[(size_t) best].name == "Lss", "front source + 90 deg yaw -> Lss");
}

void testPipeline()
{
    std::printf ("End-to-end conversion (7th order, 64 ch)\n");
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("hoa2adm_test");
    dir.deleteRecursively();
    dir.createDirectory();
    const auto in = dir.getChildFile ("hoa7.wav"), out = dir.getChildFile ("hoa7_atmos.wav");

    // 3 s of noise: 0-1 s at M+060, 1-2 s at Lss, 2-3 s from below (el -60, az 0)
    const int sr = 48000, N = 7, K = hoa::numChannelsForOrder (N), len = sr * 3;
    juce::AudioBuffer<float> buf (K, len);
    buf.clear();
    std::mt19937 rng (1);
    std::normal_distribution<float> nd (0.0f, 0.1f);
    const double dirs[3][2] = { { 60, 0 }, { 90, 0 }, { 0, -60 } };
    for (int seg = 0; seg < 3; ++seg)
    {
        const auto y = hoa::evaluate (N, dirs[seg][0] * kDeg, dirs[seg][1] * kDeg, hoa::Normalization::SN3D);
        for (int i = seg * sr; i < (seg + 1) * sr; ++i)
        {
            const float s = nd (rng);
            for (int k = 0; k < K; ++k)
                buf.setSample (k, i, s * (float) y[(size_t) k]);
        }
    }
    {
        juce::WavAudioFormat wav;
        auto stream = std::make_unique<juce::FileOutputStream> (in);
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (stream.get(), sr, (unsigned) K, 24, {}, 0));
        stream.release();
        w->writeFromAudioSampleBuffer (buf, 0, len);
    }

    const auto info = hoa::probeInput (in);
    check (info.ok && info.order == 7 && info.numChannels == 64, "probe detects 64 ch / order 7");

    hoa::ConvertOptions opt;
    opt.input = in;
    opt.output = out;
    opt.layout = hoa::LayoutPreset::Extended;
    opt.lfeEnabled = true;
    opt.stemsDirectory = dir.getChildFile ("stems");
    const auto res = hoa::convert (opt, {}, [] (const juce::String& s) { std::printf ("    %s\n", s.toRawUTF8()); });
    check (res.ok, "conversion succeeded");
    if (! res.ok)
    {
        std::printf ("    %s\n", res.message.toRawUTF8());
        return;
    }

    // Read back with JUCE (proves the extra chunks don't break a standard WAV parser)
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (out));
    const auto spk = hoa::makeLayout (opt.layout, true);
    check (r != nullptr && (int) r->numChannels == (int) spk.size() && r->lengthInSamples == len && r->bitsPerSample == 24,
           "output is a readable 24-bit WAV with the expected channels and length");
    if (r == nullptr)
        return;
    juce::AudioBuffer<float> o ((int) r->numChannels, len);
    r->read (&o, 0, len, 0, true, true);

    auto loudest = [&] (int seg) {
        int best = -1;
        double bestE = -1;
        for (int c = 0; c < o.getNumChannels(); ++c)
        {
            if (spk[(size_t) c].isLFE)
                continue;
            const double e = o.getRMSLevel (c, seg * sr, sr);
            if (e > bestE) { bestE = e; best = c; }
        }
        return spk[(size_t) best].name;
    };
    check (loudest (0) == "M+060", "segment 1 (az 60) loudest on object M+060");
    check (loudest (1) == "Lss", "segment 2 (az 90) loudest on bed Lss");
    {
        // below-horizon source folded to ear level, nothing much on the tops
        double ear = 0, top = 0;
        for (int c = 0; c < o.getNumChannels(); ++c)
        {
            if (spk[(size_t) c].isLFE) continue;
            const double e = std::pow (o.getRMSLevel (c, 2 * sr, sr), 2.0);
            (spk[(size_t) c].elevationDeg > 20 ? top : ear) += e;
        }
        std::printf ("    below-horizon source: ear-level energy share %.2f\n", ear / (ear + top));
        check (ear / (ear + top) > 0.9, "below-horizon source ends up at ear level");
    }
    int lfe = -1;
    for (size_t c = 0; c < spk.size(); ++c) if (spk[c].isLFE) lfe = (int) c;
    check (lfe >= 0 && o.getRMSLevel (lfe, 0, len) > 1e-4, "LFE carries low-passed W");

    // Chunks present
    juce::MemoryBlock mb;
    out.loadFileAsData (mb);
    auto contains = [&] (const char* tag) {
        const auto* d = static_cast<const char*> (mb.getData());
        const size_t n = std::min<size_t> (mb.getSize(), 400000), t = std::strlen (tag);
        for (size_t i = 0; i + t <= n; ++i)
            if (std::memcmp (d + i, tag, t) == 0)
                return true;
        return false;
    };
    check (contains ("chna") && contains ("axml") && contains ("audioFormatExtended"), "chna + axml chunks present");
    check (dir.getChildFile ("stems").getChildFile ("positions.txt").existsAsFile()
               && dir.getChildFile ("stems").getNumberOfChildFiles (juce::File::findFiles, "*.wav") == (int) spk.size(),
           "mono stems + positions.txt written");
    std::printf ("    test files in %s\n", dir.getFullPathName().toRawUTF8());
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    testSphericalHarmonics();
    testCubeMapping();
    testDecoder (hoa::LayoutPreset::Extended, 1, 6.0, 8.0);
    testDecoder (hoa::LayoutPreset::Extended, 3, 3.5, 4.0);
    testDecoder (hoa::LayoutPreset::Extended, 7, 3.0, 5.0);
    testDecoder (hoa::LayoutPreset::Atmos714, 7, 4.0, 8.0); // sparse layout: bigger gaps
    testDecoder (hoa::LayoutPreset::Dense, 7, 3.0, 3.0);
    testYaw();
    testPipeline();
    std::printf (failures == 0 ? "\nAll tests passed\n" : "\n%d test(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
