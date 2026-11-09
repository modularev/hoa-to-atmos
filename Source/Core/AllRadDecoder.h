#pragma once

#include "AtmosLayout.h"
#include "SphericalHarmonics.h"

#include <array>
#include <string>
#include <vector>

namespace hoa
{
struct DecoderOptions
{
    int order = 7;
    Normalization normalization = Normalization::SN3D;
    bool maxRE = true;
    bool foldBelowHorizon = true; // fold the imaginary nadir speaker into the ear-level ring
    double yawDeg = 0.0;          // rotate the sound scene counter-clockwise by this amount
};

/** Row-major decoding matrix: out[s] = sum_k gains[s * numInputs + k] * in[k]. */
struct DecoderMatrix
{
    int numOutputs = 0;
    int numInputs = 0;
    std::vector<float> gains;

    float at (int out, int in) const { return gains[(size_t) (out * numInputs + in)]; }
};

struct DecoderReport
{
    double meanAngularErrorDeg = 0; // energy-vector direction error, upper hemisphere
    double maxAngularErrorDeg = 0;
    double meanRe = 0;              // energy-vector length (1 = perfectly focused)
    double energySpreadDb = 0;      // max - min loudness over upper hemisphere directions
    int numTriangles = 0;
    int numImaginary = 0;
};

/** Design an AllRAD decoder (Zotter & Frank 2012) for the given speakers. LFE rows are zero. */
DecoderMatrix designAllRad (const std::vector<VirtualSpeaker>& speakers,
                            const DecoderOptions& options,
                            DecoderReport* report = nullptr);

/** Quasi-uniform spherical point set (Fibonacci lattice), unit vectors. */
std::vector<std::array<double, 3>> fibonacciSphere (int n);
} // namespace hoa
