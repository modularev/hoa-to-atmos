#include "../Core/Converter.h"

#include <juce_events/juce_events.h>

#include <iostream>

namespace
{
void usage()
{
    std::cout <<
R"(hoa2adm - decode an AmbiX (ACN) ambisonic mix (1st..7th order) to a Dolby Atmos ADM BWF
          that Logic Pro can import (File > Import > ADM BWF).

usage: hoa2adm <input.wav> [-o output.wav] [options]

  -o, --output <file>     output ADM BWF (default: <input>_atmos.wav)
  --layout <name>         7.1.4 | extended (default) | dense
                            7.1.4    : 7.1 bed + 4 height objects                 (12 tracks)
                            extended : + wides, rear centre, 4 top, zenith         (20 tracks)
                            dense    : + ear-level fill, mid ring, upper ring      (37 tracks)
  --no-bed                write everything (incl. ear level) as objects, no LFE
  --norm <sn3d|n3d>       input normalisation (default sn3d = AmbiX)
  --order <n>             decode only up to order n (default: from channel count)
  --basic                 no max-rE weighting (sharper but more phasey)
  --discard-below         drop content from below the horizon instead of folding it to ear level
  --yaw <deg>             rotate the scene counter-clockwise (default 0)
  --lfe <dB>              feed low-passed W to the LFE bed channel at this gain (default: off)
  --lfe-cutoff <Hz>       LFE low-pass cutoff (default 120)
  --gain <dB>             output gain before clip check (default 0)
  --no-trim               do not auto-trim to -0.1 dBFS when the decode would clip
  --stems <dir>           also write one mono WAV per track + positions.txt
  --info                  print input info and the layout, then exit
  -h, --help
)";
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit; // needed for CoreAudio-backed readers

    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (juce::String::fromUTF8 (argv[i]));

    if (args.isEmpty() || args.contains ("-h") || args.contains ("--help"))
    {
        usage();
        return args.isEmpty() ? 1 : 0;
    }

    hoa::ConvertOptions opt;
    bool infoOnly = false;
    auto cwd = juce::File::getCurrentWorkingDirectory();

    for (int i = 0; i < args.size(); ++i)
    {
        const auto& a = args[i];
        auto next = [&]() -> juce::String {
            if (i + 1 >= args.size())
            {
                std::cerr << "Missing value for " << a << "\n";
                std::exit (2);
            }
            return args[++i];
        };

        if (a == "-o" || a == "--output")        opt.output = cwd.getChildFile (next());
        else if (a == "--layout")
        {
            const auto v = next().toLowerCase();
            if (v == "7.1.4" || v == "714")      opt.layout = hoa::LayoutPreset::Atmos714;
            else if (v == "extended")            opt.layout = hoa::LayoutPreset::Extended;
            else if (v == "dense")               opt.layout = hoa::LayoutPreset::Dense;
            else { std::cerr << "Unknown layout " << v << "\n"; return 2; }
        }
        else if (a == "--no-bed")                opt.useBed = false;
        else if (a == "--norm")
        {
            const auto v = next().toLowerCase();
            if (v == "sn3d" || v == "ambix")     opt.normalization = hoa::Normalization::SN3D;
            else if (v == "n3d")                 opt.normalization = hoa::Normalization::N3D;
            else { std::cerr << "Unknown normalisation " << v << "\n"; return 2; }
        }
        else if (a == "--order")                 opt.order = next().getIntValue();
        else if (a == "--basic")                 opt.maxRE = false;
        else if (a == "--discard-below")         opt.foldBelowHorizon = false;
        else if (a == "--yaw")                   opt.yawDeg = next().getDoubleValue();
        else if (a == "--lfe")                   { opt.lfeEnabled = true; opt.lfeGainDb = next().getDoubleValue(); }
        else if (a == "--lfe-cutoff")            opt.lfeCutoffHz = next().getDoubleValue();
        else if (a == "--gain")                  opt.outputGainDb = next().getDoubleValue();
        else if (a == "--no-trim")               opt.autoTrim = false;
        else if (a == "--stems")                 opt.stemsDirectory = cwd.getChildFile (next());
        else if (a == "--info")                  infoOnly = true;
        else if (a.startsWith ("-"))             { std::cerr << "Unknown option " << a << "\n"; return 2; }
        else if (opt.input == juce::File())      opt.input = cwd.getChildFile (a);
        else                                     { std::cerr << "Unexpected argument " << a << "\n"; return 2; }
    }

    if (opt.input == juce::File())
    {
        usage();
        return 2;
    }

    const auto info = hoa::probeInput (opt.input);
    if (! info.ok)
    {
        std::cerr << info.error << "\n";
        return 1;
    }

    if (infoOnly)
    {
        std::cout << opt.input.getFileName() << ": " << info.formatName << ", " << info.numChannels << " ch, "
                  << info.bitsPerSample << " bit, " << info.sampleRate << " Hz, "
                  << (double) info.lengthFrames / info.sampleRate << " s -> ambisonic order " << info.order << "\n\n";
        std::cout << "Layout '" << hoa::toString (opt.layout) << "'" << (opt.useBed ? " (7.1 bed + objects)" : " (objects only)") << ":\n";
        int idx = 1;
        for (const auto& s : hoa::makeLayout (opt.layout, opt.useBed))
        {
            std::printf ("  %2d  %-6s %-7s az %7.1f  el %5.1f   X %6.3f  Y %6.3f  Z %6.3f\n", idx++, s.name.c_str(),
                         s.isBed ? "bed" : "object", s.azimuthDeg, s.elevationDeg, s.cube.x, s.cube.y, s.cube.z);
        }
        return 0;
    }

    if (opt.output == juce::File())
        opt.output = opt.input.getSiblingFile (opt.input.getFileNameWithoutExtension() + "_atmos.wav");
    if (opt.output == opt.input)
    {
        std::cerr << "Output would overwrite the input\n";
        return 2;
    }

    int lastPct = -1;
    const auto res = hoa::convert (
        opt,
        [&] (double p, const juce::String& stage) {
            const int pct = (int) (p * 100.0);
            if (pct != lastPct)
            {
                lastPct = pct;
                std::cout << "\r" << stage << " " << pct << "%   " << std::flush;
            }
            return true;
        },
        [] (const juce::String& line) { std::cout << "\r" << line << "\n"; });

    if (! res.ok)
    {
        std::cerr << "\nError: " << res.message << "\n";
        return 1;
    }
    return 0;
}
