#include "TrackInfo.h"

namespace tp
{
namespace
{
juce::uint32 be32 (const juce::uint8* p) { return ((juce::uint32) p[0] << 24) | ((juce::uint32) p[1] << 16) | ((juce::uint32) p[2] << 8) | p[3]; }
juce::uint32 le32 (const juce::uint8* p) { return ((juce::uint32) p[3] << 24) | ((juce::uint32) p[2] << 16) | ((juce::uint32) p[1] << 8) | p[0]; }
juce::uint32 syncsafe (const juce::uint8* p) { return ((juce::uint32) (p[0] & 0x7f) << 21) | ((juce::uint32) (p[1] & 0x7f) << 14) | ((juce::uint32) (p[2] & 0x7f) << 7) | (p[3] & 0x7f); }

/** Decodes an ID3 text payload, returns the number of bytes consumed when stopAtNull is set. */
juce::String decodeId3Text (int encoding, const juce::uint8* data, size_t size, bool stopAtNull = false, size_t* consumed = nullptr)
{
    if (size == 0) { if (consumed) *consumed = 0; return {}; }

    const bool wide = encoding == 1 || encoding == 2;
    size_t end = size;

    if (stopAtNull)
    {
        if (wide) { for (size_t i = 0; i + 1 < size; i += 2) if (data[i] == 0 && data[i + 1] == 0) { end = i; break; } }
        else      { for (size_t i = 0; i < size; ++i) if (data[i] == 0) { end = i; break; } }
        if (consumed) *consumed = juce::jmin (size, end + (wide ? 2 : 1));
    }

    juce::String s;
    if (encoding == 0)
    {
        for (size_t i = 0; i < end && data[i] != 0; ++i)
            s += (juce::juce_wchar) data[i];   // ISO-8859-1 maps 1:1 onto Unicode
    }
    else if (encoding == 3)
    {
        s = juce::String::fromUTF8 ((const char*) data, (int) end);
    }
    else
    {
        bool bigEndian = encoding == 2;
        size_t i = 0;
        if (encoding == 1 && end >= 2)
        {
            if (data[0] == 0xfe && data[1] == 0xff) { bigEndian = true; i = 2; }
            else if (data[0] == 0xff && data[1] == 0xfe) { bigEndian = false; i = 2; }
        }
        juce::Array<juce::juce_wchar> chars;
        for (; i + 1 < end; i += 2)
        {
            juce::uint32 u = bigEndian ? (juce::uint32) ((data[i] << 8) | data[i + 1]) : (juce::uint32) ((data[i + 1] << 8) | data[i]);
            if (u == 0) break;
            if (u >= 0xd800 && u <= 0xdbff && i + 3 < end)
            {
                juce::uint32 lo = bigEndian ? (juce::uint32) ((data[i + 2] << 8) | data[i + 3]) : (juce::uint32) ((data[i + 3] << 8) | data[i + 2]);
                u = 0x10000 + ((u - 0xd800) << 10) + (lo - 0xdc00);
                i += 2;
            }
            chars.add ((juce::juce_wchar) u);
        }
        for (auto c : chars) s += c;
    }
    return s.trim();
}

void parseId3 (const juce::MemoryBlock& block, TrackInfo& info)
{
    auto* d = (const juce::uint8*) block.getData();
    const size_t size = block.getSize();
    if (size < 10 || d[0] != 'I' || d[1] != 'D' || d[2] != '3')
        return;

    const int version = d[3];
    if (version < 3 || version > 4)
        return;

    const size_t tagSize = juce::jmin ((size_t) syncsafe (d + 6) + 10, size);
    size_t pos = 10;

    if ((d[5] & 0x40) != 0 && pos + 4 <= tagSize)   // extended header
        pos += version == 4 ? syncsafe (d + pos) : be32 (d + pos) + 4;

    while (pos + 10 <= tagSize)
    {
        const juce::String id ((const char*) d + pos, 4);
        if (d[pos] == 0) break;

        const size_t frameSize = version == 4 ? syncsafe (d + pos + 4) : be32 (d + pos + 4);
        const size_t body = pos + 10;
        if (frameSize == 0 || body + frameSize > tagSize) break;

        const auto* p = d + body;
        if (id == "TIT2") info.title  = decodeId3Text (p[0], p + 1, frameSize - 1);
        else if (id == "TPE1") info.artist = decodeId3Text (p[0], p + 1, frameSize - 1);
        else if (id == "TALB") info.album  = decodeId3Text (p[0], p + 1, frameSize - 1);
        else if (id == "APIC" && ! info.cover.isValid())
        {
            const int enc = p[0];
            size_t q = 1;
            while (q < frameSize && p[q] != 0) ++q;   // MIME type
            q += 2;                                   // null + picture type
            size_t used = 0;
            if (q < frameSize)
                decodeId3Text (enc, p + q, frameSize - q, true, &used);   // description
            q += used;
            if (q < frameSize)
                info.cover = juce::ImageFileFormat::loadFrom (p + q, frameSize - q);
        }
        pos = body + frameSize;
    }
}

void parseFlac (juce::InputStream& in, TrackInfo& info)
{
    char magic[4];
    if (in.read (magic, 4) != 4 || std::memcmp (magic, "fLaC", 4) != 0)
        return;

    for (bool last = false; ! last && ! in.isExhausted();)
    {
        juce::uint8 h[4];
        if (in.read (h, 4) != 4) break;
        last = (h[0] & 0x80) != 0;
        const int type = h[0] & 0x7f;
        const size_t len = ((size_t) h[1] << 16) | ((size_t) h[2] << 8) | h[3];

        if (type == 4 || (type == 6 && ! info.cover.isValid()))
        {
            juce::MemoryBlock mb;
            if (in.readIntoMemoryBlock (mb, (juce::pointer_sized_int) len) != len) break;
            auto* p = (const juce::uint8*) mb.getData();

            if (type == 4)
            {
                size_t q = 0;
                auto u32 = [&] { const auto v = q + 4 <= len ? le32 (p + q) : 0u; q += 4; return (size_t) v; };
                q += u32();                       // vendor string
                const size_t count = u32();
                for (size_t i = 0; i < count && q < len; ++i)
                {
                    const size_t l = u32();
                    if (q + l > len) break;
                    const auto entry = juce::String::fromUTF8 ((const char*) p + q, (int) l);
                    q += l;
                    const auto key = entry.upToFirstOccurrenceOf ("=", false, false).toUpperCase();
                    const auto value = entry.fromFirstOccurrenceOf ("=", false, false);
                    if (key == "TITLE") info.title = value;
                    else if (key == "ARTIST") info.artist = value;
                    else if (key == "ALBUM") info.album = value;
                }
            }
            else
            {
                size_t q = 4;
                auto u32 = [&] { const auto v = q + 4 <= len ? be32 (p + q) : 0u; q += 4; return (size_t) v; };
                q += u32();   // mime
                q += u32();   // description
                q += 16;      // width, height, depth, colours
                const size_t dataLen = u32();
                if (q + dataLen <= len)
                    info.cover = juce::ImageFileFormat::loadFrom (p + q, dataLen);
            }
        }
        else if (! in.setPosition (in.getPosition() + (juce::int64) len))
        {
            break;
        }
    }
}
} // namespace

juce::String TrackInfo::displayName (const juce::File& file) const
{
    if (title.isNotEmpty())
        return artist.isNotEmpty() ? artist + " - " + title : title;
    return file.getFileNameWithoutExtension();
}

TrackInfo readTrackInfo (const juce::File& file)
{
    TrackInfo info;
    const auto ext = file.getFileExtension().toLowerCase();

    if (auto in = file.createInputStream())
    {
        if (ext == ".mp3")
        {
            juce::MemoryBlock header;
            in->readIntoMemoryBlock (header, 10);
            if (header.getSize() == 10)
            {
                auto* h = (const juce::uint8*) header.getData();
                if (h[0] == 'I' && h[1] == 'D' && h[2] == '3')
                {
                    const auto total = (size_t) syncsafe (h + 6) + 10;
                    juce::MemoryBlock rest;
                    in->readIntoMemoryBlock (rest, (juce::pointer_sized_int) juce::jmin<size_t> (total - 10, 32 * 1024 * 1024));
                    header.append (rest.getData(), rest.getSize());
                    parseId3 (header, info);
                }
            }
        }
        else if (ext == ".flac")
        {
            parseFlac (*in, info);
        }
    }

    if (! info.cover.isValid())
    {
        for (auto name : { "cover", "folder", "front", "Cover", "Folder", "Front", "album", "AlbumArt" })
            for (auto e : { ".jpg", ".jpeg", ".png" })
            {
                auto f = file.getSiblingFile (juce::String (name) + e);
                if (f.existsAsFile())
                {
                    info.cover = juce::ImageFileFormat::loadFrom (f);
                    if (info.cover.isValid())
                        return info;
                }
            }
    }
    return info;
}

bool isAudioFile (const juce::File& file)
{
    return file.hasFileExtension ("mp3;wav;flac;aif;aiff;ogg;m4a;aac;wma");
}

juce::Array<juce::File> siblingAudioFiles (const juce::File& file)
{
    juce::Array<juce::File> result;
    for (const auto& entry : juce::RangedDirectoryIterator (file.getParentDirectory(), false, "*", juce::File::findFiles))
        if (isAudioFile (entry.getFile()))
            result.add (entry.getFile());

    struct Sorter { int compareElements (const juce::File& a, const juce::File& b) const { return a.getFileName().compareNatural (b.getFileName()); } } sorter;
    result.sort (sorter);
    return result;
}
} // namespace tp
