#pragma once

#include <string>
#include <vector>

namespace hoa
{
/** Position in the Dolby Atmos / ADM allocentric room cube.
    x: -1 left .. +1 right, y: -1 back .. +1 front, z: 0 ear level .. +1 ceiling (-1 for LFE). */
struct CubePosition
{
    double x = 0, y = 0, z = 0;
};

/** One output channel of the conversion: either a bed channel or a static object
    acting as a virtual loudspeaker. Angles use the ambisonic convention
    (azimuth counter-clockwise from front, degrees). */
struct VirtualSpeaker
{
    std::string name;         // short display / file name, e.g. "L", "M+060"
    std::string speakerLabel; // ADM bed label, e.g. "RC_L" (bed only)
    double azimuthDeg = 0;
    double elevationDeg = 0;
    CubePosition cube;
    bool isBed = false;
    bool isLFE = false;
};

enum class LayoutPreset
{
    Atmos714, // 7.1 bed + 4 height objects (= 7.1.4)
    Extended, // 7.1.4 + wides, rear centre, top middles, top front/rear centre, zenith (19 + LFE)
    Dense     // denser grid incl. a mid-height ring (36 + LFE)
};

const char* toString (LayoutPreset);

/** Build the output channel list. With useBed the ear-level 7.1 positions (plus LFE) form a
    Dolby Atmos 7.1 bed; everything else (and everything when !useBed) is an object. */
std::vector<VirtualSpeaker> makeLayout (LayoutPreset preset, bool useBed);

/** Map a direction to the Atmos room cube, consistent with a 7.1.4 reference layout:
    L/R +-30 -> front corners, Lss/Rss +-90 -> side walls, Lrs/Rrs +-135 -> rear corners,
    Ltf/Rtf +-45 el 45 and Ltr/Rtr +-135 el 45 -> ceiling corners, zenith -> ceiling centre.
    Elevations below 0 are clamped to ear level. */
CubePosition polarToCube (double azimuthDeg, double elevationDeg);
} // namespace hoa
