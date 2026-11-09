#include "AtmosLayout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace hoa
{
const char* toString (LayoutPreset p)
{
    switch (p)
    {
        case LayoutPreset::Atmos714: return "7.1.4";
        case LayoutPreset::Extended: return "extended";
        case LayoutPreset::Dense:    return "dense";
    }
    return "?";
}

namespace
{
    struct Anchor
    {
        double az; // 0..180 (left half)
        double x, y;
    };

    // Point on the square perimeter (left half, x <= 0) for azimuth 0..180, where the front
    // corner sits at frontCornerAz.
    void perimeterPoint (double azAbs, double frontCornerAz, double& x, double& y)
    {
        const Anchor anchors[] = { { 0.0, 0.0, 1.0 },
                                   { frontCornerAz, -1.0, 1.0 },
                                   { 90.0, -1.0, 0.0 },
                                   { 135.0, -1.0, -1.0 },
                                   { 180.0, 0.0, -1.0 } };
        for (int i = 0; i < 4; ++i)
        {
            const auto& a = anchors[i];
            const auto& b = anchors[i + 1];
            if (azAbs <= b.az || i == 3)
            {
                const double t = std::clamp ((azAbs - a.az) / (b.az - a.az), 0.0, 1.0);
                x = a.x + t * (b.x - a.x);
                y = a.y + t * (b.y - a.y);
                return;
            }
        }
    }

    double wrap180 (double az)
    {
        az = std::fmod (az + 180.0, 360.0);
        if (az < 0)
            az += 360.0;
        return az - 180.0;
    }

    std::string formatLabel (char prefix, double az, double el)
    {
        (void) el;
        char buf[32];
        const int a = (int) std::lround (az);
        std::snprintf (buf, sizeof (buf), "%c%c%03d", prefix, a < 0 ? '-' : '+', std::abs (a));
        return buf;
    }
} // namespace

CubePosition polarToCube (double azimuthDeg, double elevationDeg)
{
    const double az = wrap180 (azimuthDeg);
    const double el = std::clamp (elevationDeg, 0.0, 90.0);

    // Wall height up to 45 deg, then move inwards across the ceiling towards the zenith.
    double r = 1.0, z = el / 45.0;
    if (el > 45.0)
    {
        z = 1.0;
        r = (90.0 - el) / 45.0;
    }

    const double frontCorner = 30.0 + 15.0 * std::min (el, 45.0) / 45.0;
    double x, y;
    perimeterPoint (std::abs (az), frontCorner, x, y);
    if (az < 0)
        x = -x; // right half

    CubePosition p;
    p.x = r * x;
    p.y = r * y;
    p.z = z;
    // tidy -0.0 and rounding noise
    auto tidy = [] (double v) { return std::abs (v) < 1e-9 ? 0.0 : v; };
    p.x = tidy (p.x);
    p.y = tidy (p.y);
    p.z = tidy (p.z);
    return p;
}

std::vector<VirtualSpeaker> makeLayout (LayoutPreset preset, bool useBed)
{
    std::vector<VirtualSpeaker> out;

    auto add = [&] (std::string name, std::string label, double az, double el, bool bedCandidate) {
        VirtualSpeaker s;
        s.name = std::move (name);
        s.azimuthDeg = az;
        s.elevationDeg = el;
        s.cube = polarToCube (az, el);
        s.isBed = useBed && bedCandidate;
        s.speakerLabel = s.isBed ? std::move (label) : std::string();
        out.push_back (std::move (s));
    };

    // Ear-level 7.1 (bed order: L R C LFE Lss Rss Lrs Rrs)
    add ("L", "RC_L", 30, 0, true);
    add ("R", "RC_R", -30, 0, true);
    add ("C", "RC_C", 0, 0, true);
    if (useBed)
    {
        VirtualSpeaker lfe;
        lfe.name = "LFE";
        lfe.speakerLabel = "RC_LFE";
        lfe.isBed = true;
        lfe.isLFE = true;
        lfe.cube = { -1.0, 1.0, -1.0 };
        out.push_back (lfe);
    }
    add ("Lss", "RC_Lss", 90, 0, true);
    add ("Rss", "RC_Rss", -90, 0, true);
    add ("Lrs", "RC_Lrs", 135, 0, true);
    add ("Rrs", "RC_Rrs", -135, 0, true);

    // Heights of 7.1.4 (always objects: Atmos beds stop at 7.1.2)
    add ("Ltf", "", 45, 45, false);
    add ("Rtf", "", -45, 45, false);
    add ("Ltr", "", 135, 45, false);
    add ("Rtr", "", -135, 45, false);

    auto addObj = [&] (char prefix, double az, double el) { add (formatLabel (prefix, az, el), "", az, el, false); };

    if (preset == LayoutPreset::Extended || preset == LayoutPreset::Dense)
    {
        addObj ('M', 60, 0);
        addObj ('M', -60, 0);
        addObj ('M', 180, 0);
        addObj ('U', 0, 45);
        addObj ('U', 90, 45);
        addObj ('U', -90, 45);
        addObj ('U', 180, 45);
        add ("T+000", "", 0, 90, false);
    }

    if (preset == LayoutPreset::Dense)
    {
        // Ear level fill-ins
        for (double az : { 15.0, -15.0, 112.5, -112.5, 157.5, -157.5 })
            addObj ('M', az, 0);
        // Mid-height ring (el 22.5)
        for (double az : { 0.0, 45.0, -45.0, 90.0, -90.0, 135.0, -135.0, 180.0 })
            addObj ('H', az, 22.5);
        // Upper ring (el 67.5)
        for (double az : { 45.0, -45.0, 135.0, -135.0 })
            addObj ('V', az, 67.5);
    }

    return out;
}
} // namespace hoa
