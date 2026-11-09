#include "AdmBwfWriter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace hoa
{
namespace
{
    std::string hex4 (int v)
    {
        char b[8];
        std::snprintf (b, sizeof (b), "%04X", v & 0xffff);
        return b;
    }

    std::string hex8 (int v)
    {
        char b[12];
        std::snprintf (b, sizeof (b), "%08X", (unsigned) v);
        return b;
    }

    std::string timecode (int64_t frames, int sampleRate)
    {
        // hh:mm:ss.fffff (BS.2076 time format, 5 fractional digits)
        const int64_t units = (frames * 100000 + sampleRate / 2) / sampleRate; // 1e-5 s
        const int64_t frac = units % 100000;
        const int64_t totalSec = units / 100000;
        char b[32];
        std::snprintf (b, sizeof (b), "%02lld:%02lld:%02lld.%05lld",
                       (long long) (totalSec / 3600), (long long) ((totalSec / 60) % 60),
                       (long long) (totalSec % 60), (long long) frac);
        return b;
    }

    std::string num (double v)
    {
        char b[32];
        std::snprintf (b, sizeof (b), "%.6f", std::abs (v) < 5e-7 ? 0.0 : v);
        return b;
    }

    std::string xmlEscape (const std::string& s)
    {
        std::string o;
        for (char c : s)
        {
            switch (c)
            {
                case '&': o += "&amp;"; break;
                case '<': o += "&lt;"; break;
                case '>': o += "&gt;"; break;
                case '"': o += "&quot;"; break;
                default: o += c;
            }
        }
        return o;
    }

    struct ChannelIds
    {
        std::string channelFormat, stream, track, pack, trackUid;
    };

    // Bed channels share one pack (type 0001 DirectSpeakers), each object has its own pack (type 0003 Objects).
    std::vector<ChannelIds> assignIds (const std::vector<VirtualSpeaker>& ch)
    {
        std::vector<ChannelIds> ids (ch.size());
        int bedCounter = 0x1001, objCounter = 0x1001;
        for (size_t i = 0; i < ch.size(); ++i)
        {
            const bool bed = ch[i].isBed;
            const std::string type = bed ? "0001" : "0003";
            const int n = bed ? bedCounter++ : objCounter++;
            const std::string tail = type + hex4 (n);
            ids[i].channelFormat = "AC_" + tail;
            ids[i].stream = "AS_" + tail;
            ids[i].track = "AT_" + tail + "_01";
            ids[i].pack = bed ? "AP_00011001" : "AP_" + tail;
            ids[i].trackUid = "ATU_" + hex8 ((int) i + 1);
        }
        return ids;
    }

    void put16 (std::vector<uint8_t>& v, uint32_t x)
    {
        v.push_back ((uint8_t) (x & 0xff));
        v.push_back ((uint8_t) ((x >> 8) & 0xff));
    }
    void put32 (std::vector<uint8_t>& v, uint32_t x)
    {
        for (int i = 0; i < 4; ++i)
            v.push_back ((uint8_t) ((x >> (8 * i)) & 0xff));
    }
    void put64 (std::vector<uint8_t>& v, uint64_t x)
    {
        for (int i = 0; i < 8; ++i)
            v.push_back ((uint8_t) ((x >> (8 * i)) & 0xff));
    }
    void putStr (std::vector<uint8_t>& v, const std::string& s, size_t len)
    {
        for (size_t i = 0; i < len; ++i)
            v.push_back (i < s.size() ? (uint8_t) s[i] : 0);
    }
    void putChunk (std::vector<uint8_t>& v, const char* id, const std::vector<uint8_t>& body)
    {
        putStr (v, id, 4);
        put32 (v, (uint32_t) body.size());
        v.insert (v.end(), body.begin(), body.end());
        if (body.size() & 1)
            v.push_back (0);
    }
} // namespace

std::string AdmBwfWriter::buildAxml (const std::vector<VirtualSpeaker>& ch, int sampleRate, int64_t numFrames, const std::string& programmeName)
{
    const auto ids = assignIds (ch);
    const std::string dur = timecode (numFrames, sampleRate);
    const std::string zero = "00:00:00.00000";

    std::ostringstream x;
    x << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      << "<ebuCoreMain xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns=\"urn:ebu:metadata-schema:ebuCore_2014\" "
         "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" schema=\"EBU_CORE_20140201.xsd\" xml:lang=\"en\">\n"
      << " <coreMetadata>\n  <format>\n   <audioFormatExtended version=\"ITU-R_BS.2076-2\">\n";

    // Objects (one bed object + one per static object), in channel order
    struct Obj { std::string id, name, pack; std::vector<size_t> channels; };
    std::vector<Obj> objs;
    int objCounter = 0x1001;
    {
        Obj bed { "AO_" + hex4 (objCounter), "Bed 7.1", "AP_00011001", {} };
        for (size_t i = 0; i < ch.size(); ++i)
            if (ch[i].isBed)
                bed.channels.push_back (i);
        if (! bed.channels.empty())
        {
            objs.push_back (bed);
            ++objCounter;
        }
        for (size_t i = 0; i < ch.size(); ++i)
            if (! ch[i].isBed)
                objs.push_back ({ "AO_" + hex4 (objCounter++), "HOA " + ch[i].name, ids[i].pack, { i } });
    }

    // audioProgramme
    x << "    <audioProgramme audioProgrammeID=\"APR_1001\" audioProgrammeName=\"" << xmlEscape (programmeName)
      << "\" start=\"" << zero << "\" end=\"" << dur << "\">\n";
    for (size_t o = 0; o < objs.size(); ++o)
        x << "     <audioContentIDRef>ACO_" << hex4 (0x1001 + (int) o) << "</audioContentIDRef>\n";
    x << "    </audioProgramme>\n";

    // audioContent
    for (size_t o = 0; o < objs.size(); ++o)
        x << "    <audioContent audioContentID=\"ACO_" << hex4 (0x1001 + (int) o) << "\" audioContentName=\""
          << xmlEscape (objs[o].name) << "\">\n"
          << "     <audioObjectIDRef>" << objs[o].id << "</audioObjectIDRef>\n"
          << "     <dialogue nonDialogueContentKind=\"0\">0</dialogue>\n"
          << "    </audioContent>\n";

    // audioObject
    for (const auto& o : objs)
    {
        x << "    <audioObject audioObjectID=\"" << o.id << "\" audioObjectName=\"" << xmlEscape (o.name)
          << "\" start=\"" << zero << "\" duration=\"" << dur << "\">\n"
          << "     <audioPackFormatIDRef>" << o.pack << "</audioPackFormatIDRef>\n";
        for (size_t c : o.channels)
            x << "     <audioTrackUIDRef>" << ids[c].trackUid << "</audioTrackUIDRef>\n";
        x << "    </audioObject>\n";
    }

    // audioPackFormat
    for (const auto& o : objs)
    {
        const bool bed = ch[o.channels.front()].isBed;
        x << "    <audioPackFormat audioPackFormatID=\"" << o.pack << "\" audioPackFormatName=\""
          << (bed ? "RoomCentric_7.1" : xmlEscape (o.name)) << "\" typeLabel=\"" << (bed ? "0001" : "0003")
          << "\" typeDefinition=\"" << (bed ? "DirectSpeakers" : "Objects") << "\">\n";
        for (size_t c : o.channels)
            x << "     <audioChannelFormatIDRef>" << ids[c].channelFormat << "</audioChannelFormatIDRef>\n";
        x << "    </audioPackFormat>\n";
    }

    // audioChannelFormat
    for (size_t i = 0; i < ch.size(); ++i)
    {
        const auto& s = ch[i];
        const std::string cf = ids[i].channelFormat;
        const std::string blockId = "AB_" + cf.substr (3) + "_00000001";
        const std::string name = s.isBed ? s.speakerLabel : ("HOA " + s.name);
        x << "    <audioChannelFormat audioChannelFormatID=\"" << cf << "\" audioChannelFormatName=\"" << xmlEscape (name)
          << "\" typeLabel=\"" << (s.isBed ? "0001" : "0003") << "\" typeDefinition=\""
          << (s.isBed ? "DirectSpeakers" : "Objects") << "\">\n";
        if (s.isLFE)
            x << "     <frequency typeDefinition=\"lowPass\">120</frequency>\n";
        if (s.isBed)
        {
            x << "     <audioBlockFormat audioBlockFormatID=\"" << blockId << "\">\n"
              << "      <speakerLabel>" << s.speakerLabel << "</speakerLabel>\n"
              << "      <cartesian>1</cartesian>\n"
              << "      <position coordinate=\"X\">" << num (s.cube.x) << "</position>\n"
              << "      <position coordinate=\"Y\">" << num (s.cube.y) << "</position>\n"
              << "      <position coordinate=\"Z\">" << num (s.cube.z) << "</position>\n"
              << "     </audioBlockFormat>\n";
        }
        else
        {
            x << "     <audioBlockFormat audioBlockFormatID=\"" << blockId << "\" rtime=\"" << zero
              << "\" duration=\"" << dur << "\">\n"
              << "      <cartesian>1</cartesian>\n"
              << "      <position coordinate=\"X\">" << num (s.cube.x) << "</position>\n"
              << "      <position coordinate=\"Y\">" << num (s.cube.y) << "</position>\n"
              << "      <position coordinate=\"Z\">" << num (s.cube.z) << "</position>\n"
              << "      <width>0.000000</width>\n"
              << "      <depth>0.000000</depth>\n"
              << "      <height>0.000000</height>\n"
              << "      <gain>1.000000</gain>\n"
              << "      <jumpPosition>0</jumpPosition>\n"
              << "     </audioBlockFormat>\n";
        }
        x << "    </audioChannelFormat>\n";
    }

    // audioStreamFormat / audioTrackFormat
    for (size_t i = 0; i < ch.size(); ++i)
    {
        const std::string nm = "PCM_" + (ch[i].isBed ? ch[i].speakerLabel : ch[i].name);
        x << "    <audioStreamFormat audioStreamFormatID=\"" << ids[i].stream << "\" audioStreamFormatName=\"" << xmlEscape (nm)
          << "\" formatLabel=\"0001\" formatDefinition=\"PCM\">\n"
          << "     <audioChannelFormatIDRef>" << ids[i].channelFormat << "</audioChannelFormatIDRef>\n"
          << "     <audioTrackFormatIDRef>" << ids[i].track << "</audioTrackFormatIDRef>\n"
          << "    </audioStreamFormat>\n";
        x << "    <audioTrackFormat audioTrackFormatID=\"" << ids[i].track << "\" audioTrackFormatName=\"" << xmlEscape (nm)
          << "\" formatLabel=\"0001\" formatDefinition=\"PCM\">\n"
          << "     <audioStreamFormatIDRef>" << ids[i].stream << "</audioStreamFormatIDRef>\n"
          << "    </audioTrackFormat>\n";
    }

    // audioTrackUID
    for (size_t i = 0; i < ch.size(); ++i)
        x << "    <audioTrackUID UID=\"" << ids[i].trackUid << "\" sampleRate=\"" << sampleRate << "\" bitDepth=\"24\">\n"
          << "     <audioTrackFormatIDRef>" << ids[i].track << "</audioTrackFormatIDRef>\n"
          << "     <audioPackFormatIDRef>" << ids[i].pack << "</audioPackFormatIDRef>\n"
          << "    </audioTrackUID>\n";

    x << "   </audioFormatExtended>\n  </format>\n </coreMetadata>\n</ebuCoreMain>\n";
    return x.str();
}

AdmBwfWriter::~AdmBwfWriter()
{
    if (file.is_open())
        file.close();
}

bool AdmBwfWriter::open (const std::string& path, const std::vector<VirtualSpeaker>& ch, int sampleRate, int64_t numFrames,
                         const std::string& programmeName, std::string& error)
{
    numChannels = (int) ch.size();
    framesExpected = numFrames;
    framesWritten = 0;
    if (numChannels < 1 || numChannels > 128)
    {
        error = "ADM BWF for Dolby Atmos supports 1..128 channels";
        return false;
    }

    const int bytesPerSample = 3;
    const uint32_t blockAlign = (uint32_t) (numChannels * bytesPerSample);
    const uint64_t dataBytes = (uint64_t) numFrames * blockAlign;

    // fmt
    std::vector<uint8_t> fmt;
    put16 (fmt, 1); // WAVE_FORMAT_PCM
    put16 (fmt, (uint32_t) numChannels);
    put32 (fmt, (uint32_t) sampleRate);
    put32 (fmt, (uint32_t) sampleRate * blockAlign);
    put16 (fmt, blockAlign);
    put16 (fmt, 24);

    // chna
    const auto ids = assignIds (ch);
    std::vector<uint8_t> chna;
    put16 (chna, (uint32_t) numChannels);
    put16 (chna, (uint32_t) numChannels);
    for (int i = 0; i < numChannels; ++i)
    {
        put16 (chna, (uint32_t) i + 1);
        putStr (chna, ids[(size_t) i].trackUid, 12);
        putStr (chna, ids[(size_t) i].track, 14);
        putStr (chna, ids[(size_t) i].pack, 11);
        chna.push_back (0);
    }

    // axml
    const std::string xml = buildAxml (ch, sampleRate, numFrames, programmeName);
    const std::vector<uint8_t> axml (xml.begin(), xml.end());

    std::vector<uint8_t> chunks;
    putChunk (chunks, "fmt ", fmt);
    putChunk (chunks, "chna", chna);
    putChunk (chunks, "axml", axml);

    const uint64_t ds64ChunkBytes = 8 + 28;
    const uint64_t dataPad = dataBytes & 1;
    const uint64_t riffSize = 4 + ds64ChunkBytes + chunks.size() + 8 + dataBytes + dataPad;
    const bool rf64 = riffSize > 0xFFFFFFFFull;

    std::vector<uint8_t> head;
    putStr (head, rf64 ? "RF64" : "RIFF", 4);
    put32 (head, rf64 ? 0xFFFFFFFFu : (uint32_t) riffSize);
    putStr (head, "WAVE", 4);
    // ds64 (RF64) or a same-sized JUNK placeholder (RIFF)
    putStr (head, rf64 ? "ds64" : "JUNK", 4);
    put32 (head, 28);
    if (rf64)
    {
        put64 (head, riffSize);
        put64 (head, dataBytes);
        put64 (head, (uint64_t) numFrames);
        put32 (head, 0);
    }
    else
    {
        for (int i = 0; i < 28; ++i)
            head.push_back (0);
    }
    head.insert (head.end(), chunks.begin(), chunks.end());
    putStr (head, "data", 4);
    put32 (head, rf64 ? 0xFFFFFFFFu : (uint32_t) dataBytes);

    file.open (path, std::ios::binary | std::ios::trunc);
    if (! file)
    {
        error = "Could not open output file: " + path;
        return false;
    }
    file.write ((const char*) head.data(), (std::streamsize) head.size());
    return (bool) file;
}

bool AdmBwfWriter::write (const float* const* data, int n)
{
    if (! file.is_open())
        return false;
    n = (int) std::min<int64_t> (n, framesExpected - framesWritten);
    if (n <= 0)
        return true;
    scratch.resize ((size_t) n * (size_t) numChannels * 3);
    uint8_t* p = scratch.data();
    for (int i = 0; i < n; ++i)
        for (int c = 0; c < numChannels; ++c)
        {
            const double v = std::clamp ((double) data[c][i], -1.0, 8388607.0 / 8388608.0);
            const int32_t s = (int32_t) std::lrint (v * 8388608.0);
            *p++ = (uint8_t) (s & 0xff);
            *p++ = (uint8_t) ((s >> 8) & 0xff);
            *p++ = (uint8_t) ((s >> 16) & 0xff);
        }
    file.write ((const char*) scratch.data(), (std::streamsize) scratch.size());
    framesWritten += n;
    return (bool) file;
}

bool AdmBwfWriter::close (std::string& error)
{
    if (! file.is_open())
        return false;
    // Pad data to the announced length so the header stays valid even after an early stop.
    const bool complete = framesWritten == framesExpected;
    if (! complete)
    {
        std::vector<uint8_t> zeros ((size_t) numChannels * 3 * 4096, 0);
        while (framesWritten < framesExpected)
        {
            const int64_t n = std::min<int64_t> (4096, framesExpected - framesWritten);
            file.write ((const char*) zeros.data(), (std::streamsize) (n * numChannels * 3));
            framesWritten += n;
        }
        error = "Output was padded with silence: fewer frames were rendered than expected";
    }
    if (((uint64_t) framesExpected * (uint64_t) numChannels * 3) & 1)
        file.put (0);
    file.close();
    return complete && ! file.fail();
}
} // namespace hoa
