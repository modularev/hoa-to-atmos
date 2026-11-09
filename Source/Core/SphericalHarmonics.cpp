#include "SphericalHarmonics.h"

#include <cmath>

namespace hoa
{
int orderForChannelCount (int numChannels)
{
    if (numChannels <= 0)
        return -1;
    int n = 0;
    while (numChannelsForOrder (n + 1) <= numChannels)
        ++n;
    return n;
}

int orderOfAcn (int acn)
{
    return (int) std::floor (std::sqrt ((double) acn));
}

static double factorial (int n)
{
    double r = 1.0;
    for (int i = 2; i <= n; ++i)
        r *= i;
    return r;
}

void evaluate (int order, double az, double el, Normalization norm, double* out)
{
    const double x = std::sin (el);              // cos(colatitude)
    const double s = std::sqrt (std::max (0.0, 1.0 - x * x));

    // Associated Legendre P[n][m] without Condon-Shortley phase.
    double P[kMaxOrder + 1][kMaxOrder + 1] = {};
    double pmm = 1.0;
    for (int m = 0; m <= order; ++m)
    {
        if (m > 0)
            pmm *= (2.0 * m - 1.0) * s;
        P[m][m] = pmm;
        if (m + 1 <= order)
            P[m + 1][m] = x * (2.0 * m + 1.0) * pmm;
        for (int n = m + 2; n <= order; ++n)
            P[n][m] = ((2.0 * n - 1.0) * x * P[n - 1][m] - (n + m - 1.0) * P[n - 2][m]) / (n - m);
    }

    for (int n = 0; n <= order; ++n)
    {
        const double orderGain = (norm == Normalization::N3D) ? std::sqrt (2.0 * n + 1.0) : 1.0;
        for (int m = -n; m <= n; ++m)
        {
            const int am = std::abs (m);
            const double sn3d = std::sqrt ((m == 0 ? 1.0 : 2.0) * factorial (n - am) / factorial (n + am));
            const double trig = m < 0 ? std::sin (am * az) : std::cos (am * az);
            out[n * n + n + m] = orderGain * sn3d * P[n][am] * trig;
        }
    }
}

std::vector<double> evaluate (int order, double az, double el, Normalization norm)
{
    std::vector<double> v ((size_t) numChannelsForOrder (order));
    evaluate (order, az, el, norm, v.data());
    return v;
}

double legendre (int n, double x)
{
    if (n == 0)
        return 1.0;
    double p0 = 1.0, p1 = x;
    for (int k = 2; k <= n; ++k)
    {
        const double p2 = ((2.0 * k - 1.0) * x * p1 - (k - 1.0) * p0) / k;
        p0 = p1;
        p1 = p2;
    }
    return p1;
}

std::vector<double> maxReWeights (int order)
{
    constexpr double pi = 3.14159265358979323846;
    const double rE = std::cos (137.9 * pi / 180.0 / (order + 1.51));
    std::vector<double> w ((size_t) order + 1);
    for (int n = 0; n <= order; ++n)
        w[(size_t) n] = legendre (n, rE);
    return w;
}
} // namespace hoa
