#pragma once

#include <vector>

namespace hoa
{
constexpr int kMaxOrder = 7;

enum class Normalization
{
    SN3D, // AmbiX (Reaper / IEM / SPARTA default)
    N3D
};

inline constexpr int numChannelsForOrder (int order) { return (order + 1) * (order + 1); }

/** Highest full order N with (N+1)^2 <= numChannels, or -1 if numChannels == 0. */
int orderForChannelCount (int numChannels);

/** Ambisonic order of ACN channel index. */
int orderOfAcn (int acn);

/** Real spherical harmonics in ACN order (no Condon-Shortley phase), for a direction
    given as azimuth (counter-clockwise from front, radians) and elevation (radians).
    Writes numChannelsForOrder(order) values into out. */
void evaluate (int order, double azimuthRad, double elevationRad, Normalization norm, double* out);

std::vector<double> evaluate (int order, double azimuthRad, double elevationRad, Normalization norm);

/** Legendre polynomial P_n(x). */
double legendre (int n, double x);

/** Per-order max-rE weights for a 3D decoder of the given order (Zotter & Frank). */
std::vector<double> maxReWeights (int order);
} // namespace hoa
