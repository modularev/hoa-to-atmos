#include "AllRadDecoder.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace hoa
{
namespace
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kDeg = kPi / 180.0;

    using Vec3 = std::array<double, 3>;

    Vec3 sub (const Vec3& a, const Vec3& b) { return { a[0] - b[0], a[1] - b[1], a[2] - b[2] }; }
    double dot (const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
    Vec3 cross (const Vec3& a, const Vec3& b)
    {
        return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
    }
    double norm (const Vec3& a) { return std::sqrt (dot (a, a)); }

    Vec3 unitVector (double azDeg, double elDeg)
    {
        const double az = azDeg * kDeg, el = elDeg * kDeg;
        return { std::cos (el) * std::cos (az), std::cos (el) * std::sin (az), std::sin (el) };
    }

    void toPolar (const Vec3& v, double& azRad, double& elRad)
    {
        azRad = std::atan2 (v[1], v[0]);
        elRad = std::atan2 (v[2], std::hypot (v[0], v[1]));
    }

    struct Triangle
    {
        int idx[3];
        double inv[3][3]; // inverse of [v0 v1 v2] (column vectors)
    };

    bool invert3 (const Vec3& a, const Vec3& b, const Vec3& c, double out[3][3])
    {
        // Matrix with columns a, b, c; inverse rows are cross products / det.
        const Vec3 r0 = cross (b, c), r1 = cross (c, a), r2 = cross (a, b);
        const double det = dot (a, r0);
        if (std::abs (det) < 1e-9)
            return false;
        for (int j = 0; j < 3; ++j)
        {
            out[0][j] = r0[(size_t) j] / det;
            out[1][j] = r1[(size_t) j] / det;
            out[2][j] = r2[(size_t) j] / det;
        }
        return true;
    }

    /** Brute-force convex hull of points on the unit sphere (fine for < ~100 points). */
    std::vector<Triangle> convexHull (const std::vector<Vec3>& p)
    {
        std::vector<Triangle> tris;
        const int n = (int) p.size();
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j)
                for (int k = j + 1; k < n; ++k)
                {
                    const Vec3 nrm = cross (sub (p[(size_t) j], p[(size_t) i]), sub (p[(size_t) k], p[(size_t) i]));
                    if (norm (nrm) < 1e-9)
                        continue;
                    bool anyAbove = false, anyBelow = false;
                    for (int l = 0; l < n && ! (anyAbove && anyBelow); ++l)
                    {
                        if (l == i || l == j || l == k)
                            continue;
                        const double d = dot (nrm, sub (p[(size_t) l], p[(size_t) i]));
                        if (d > 1e-9) anyAbove = true;
                        if (d < -1e-9) anyBelow = true;
                    }
                    if (anyAbove && anyBelow)
                        continue;

                    Triangle t { { i, j, k }, {} };
                    if (invert3 (p[(size_t) i], p[(size_t) j], p[(size_t) k], t.inv))
                        tris.push_back (t);
                }
        return tris;
    }

    /** Energy-normalised VBAP gains for direction d. Returns false if no triangle contains d. */
    bool vbap (const std::vector<Triangle>& tris, const Vec3& d, int numSpeakers, std::vector<double>& gains)
    {
        std::fill (gains.begin(), gains.end(), 0.0);
        (void) numSpeakers;
        double bestMin = -1e30;
        const Triangle* best = nullptr;
        double bestG[3] = {};
        for (const auto& t : tris)
        {
            double g[3];
            for (int r = 0; r < 3; ++r)
                g[r] = t.inv[r][0] * d[0] + t.inv[r][1] * d[1] + t.inv[r][2] * d[2];
            const double mn = std::min ({ g[0], g[1], g[2] });
            if (mn > bestMin)
            {
                bestMin = mn;
                best = &t;
                std::copy (g, g + 3, bestG);
            }
            if (mn >= -1e-9)
                break;
        }
        if (best == nullptr || bestMin < -1e-3)
            return false;

        double e = 0;
        for (double& g : bestG)
        {
            g = std::max (g, 0.0);
            e += g * g;
        }
        e = std::sqrt (e);
        for (int r = 0; r < 3; ++r)
            gains[(size_t) best->idx[r]] = bestG[r] / e;
        return true;
    }
} // namespace

std::vector<std::array<double, 3>> fibonacciSphere (int n)
{
    std::vector<Vec3> pts ((size_t) n);
    const double golden = kPi * (3.0 - std::sqrt (5.0));
    for (int i = 0; i < n; ++i)
    {
        const double z = 1.0 - (2.0 * i + 1.0) / n;
        const double r = std::sqrt (1.0 - z * z);
        const double phi = golden * i;
        pts[(size_t) i] = { r * std::cos (phi), r * std::sin (phi), z };
    }
    return pts;
}

DecoderMatrix designAllRad (const std::vector<VirtualSpeaker>& speakers, const DecoderOptions& opt, DecoderReport* report)
{
    const int order = opt.order;
    if (order < 1 || order > kMaxOrder)
        throw std::invalid_argument ("Ambisonic order must be between 1 and 7");

    const int K = numChannelsForOrder (order);

    // --- speaker set used for panning: real (non-LFE) speakers + imaginary helpers
    std::vector<Vec3> dirs;
    std::vector<int> realIndex; // index into speakers, or -1 for nadir, -2 for zenith
    bool hasZenith = false;
    for (size_t s = 0; s < speakers.size(); ++s)
    {
        if (speakers[s].isLFE)
            continue;
        // rotate the scene by +yaw == place speakers at az - yaw in the scene frame
        dirs.push_back (unitVector (speakers[s].azimuthDeg - opt.yawDeg, speakers[s].elevationDeg));
        realIndex.push_back ((int) s);
        if (speakers[s].elevationDeg > 75.0)
            hasZenith = true;
    }
    dirs.push_back (unitVector (0, -90));
    realIndex.push_back (-1);
    if (! hasZenith)
    {
        dirs.push_back (unitVector (0, 90));
        realIndex.push_back (-2);
    }
    const int numPan = (int) dirs.size();

    const auto tris = convexHull (dirs);
    if (tris.empty())
        throw std::runtime_error ("Could not triangulate the speaker layout");

    // --- AllRAD: sample the sphere densely, decode to the grid, VBAP the grid to the speakers
    const int L = 5200;
    const auto grid = fibonacciSphere (L);
    std::vector<double> D ((size_t) numPan * (size_t) K, 0.0); // N3D decoder (pan speakers x K)
    std::vector<double> g ((size_t) numPan), y ((size_t) K);
    for (const auto& p : grid)
    {
        if (! vbap (tris, p, numPan, g))
            continue;
        double az, el;
        toPolar (p, az, el);
        evaluate (order, az, el, Normalization::N3D, y.data());
        for (int s = 0; s < numPan; ++s)
        {
            if (g[(size_t) s] == 0.0)
                continue;
            for (int k = 0; k < K; ++k)
                D[(size_t) (s * K + k)] += g[(size_t) s] * y[(size_t) k] / L;
        }
    }

    // --- max-rE weighting and input normalisation (SN3D input -> N3D decoder)
    const auto w = opt.maxRE ? maxReWeights (order) : std::vector<double> ((size_t) order + 1, 1.0);
    for (int s = 0; s < numPan; ++s)
        for (int k = 0; k < K; ++k)
        {
            const int n = orderOfAcn (k);
            const double normGain = opt.normalization == Normalization::SN3D ? std::sqrt (2.0 * n + 1.0) : 1.0;
            D[(size_t) (s * K + k)] *= w[(size_t) n] * normGain;
        }

    // --- fold imaginary speakers into real ones (or drop them)
    const int S = (int) speakers.size();
    std::vector<double> out ((size_t) S * (size_t) K, 0.0);
    std::vector<int> earLevel, topLayer;
    for (int s = 0; s < S; ++s)
    {
        if (speakers[(size_t) s].isLFE)
            continue;
        if (std::abs (speakers[(size_t) s].elevationDeg) < 10.0)
            earLevel.push_back (s);
        if (speakers[(size_t) s].elevationDeg >= 30.0)
            topLayer.push_back (s);
    }
    int numImaginary = 0;
    for (int p = 0; p < numPan; ++p)
    {
        const int r = realIndex[(size_t) p];
        if (r >= 0)
        {
            for (int k = 0; k < K; ++k)
                out[(size_t) (r * K + k)] += D[(size_t) (p * K + k)];
            continue;
        }
        ++numImaginary;
        const bool isNadir = (r == -1);
        if (isNadir && ! opt.foldBelowHorizon)
            continue;
        const auto& targets = isNadir ? earLevel : topLayer;
        if (targets.empty())
            continue;
        const double share = 1.0 / std::sqrt ((double) targets.size());
        for (int t : targets)
            for (int k = 0; k < K; ++k)
                out[(size_t) (t * K + k)] += share * D[(size_t) (p * K + k)];
    }

    // --- normalise: mean output energy of a unit plane wave over the sphere == 1
    const auto probe = fibonacciSphere (2000);
    auto renderEnergy = [&] (const Vec3& p, std::vector<double>& sig) {
        double az, el;
        toPolar (p, az, el);
        evaluate (order, az, el, opt.normalization, y.data());
        double e = 0;
        for (int s = 0; s < S; ++s)
        {
            double v = 0;
            for (int k = 0; k < K; ++k)
                v += out[(size_t) (s * K + k)] * y[(size_t) k];
            sig[(size_t) s] = v;
            e += v * v;
        }
        return e;
    };
    std::vector<double> sig ((size_t) S);
    double meanE = 0;
    for (const auto& p : probe)
        meanE += renderEnergy (p, sig);
    meanE /= (double) probe.size();
    const double scale = meanE > 0 ? 1.0 / std::sqrt (meanE) : 1.0;
    for (auto& v : out)
        v *= scale;

    if (report != nullptr)
    {
        *report = {};
        report->numTriangles = (int) tris.size();
        report->numImaginary = numImaginary;
        double sumErr = 0, maxErr = 0, sumRe = 0, minE = 1e30, maxE = 0;
        int count = 0;
        for (const auto& p : probe)
        {
            if (p[2] < 0.0)
                continue;
            const double e = renderEnergy (p, sig);
            Vec3 rE { 0, 0, 0 };
            for (int s = 0; s < S; ++s)
            {
                if (speakers[(size_t) s].isLFE)
                    continue;
                const Vec3 u = unitVector (speakers[(size_t) s].azimuthDeg, speakers[(size_t) s].elevationDeg);
                const double e2 = sig[(size_t) s] * sig[(size_t) s];
                for (int c = 0; c < 3; ++c)
                    rE[(size_t) c] += e2 * u[(size_t) c];
            }
            const double len = norm (rE) / std::max (e, 1e-30);
            double az, el;
            toPolar (p, az, el);
            const Vec3 expected = unitVector (az / kDeg + opt.yawDeg, el / kDeg);
            const double err = std::acos (std::clamp (dot (rE, expected) / std::max (norm (rE), 1e-30), -1.0, 1.0)) / kDeg;
            sumErr += err;
            maxErr = std::max (maxErr, err);
            sumRe += len;
            minE = std::min (minE, e);
            maxE = std::max (maxE, e);
            ++count;
        }
        if (count > 0)
        {
            report->meanAngularErrorDeg = sumErr / count;
            report->maxAngularErrorDeg = maxErr;
            report->meanRe = sumRe / count;
            report->energySpreadDb = 10.0 * std::log10 (maxE / std::max (minE, 1e-30));
        }
    }

    DecoderMatrix m;
    m.numOutputs = S;
    m.numInputs = K;
    m.gains.resize (out.size());
    for (size_t i = 0; i < out.size(); ++i)
        m.gains[i] = (float) out[i];
    return m;
}
} // namespace hoa
