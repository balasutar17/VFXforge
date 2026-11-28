#include "vfx/editor/Psd.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace vfx::editor {

namespace {

// Reads big-endian numbers from the file, never past its end: a damaged or
// cut-off file only sets `bad`.
struct Reader {
    std::string_view data;
    std::size_t at = 0;
    bool bad = false;

    bool has(std::uint64_t n) const { return !bad && n <= data.size() - at; }
    std::uint64_t number(int bytes) {
        if (!has(static_cast<std::uint64_t>(bytes))) {
            bad = true;
            at = data.size();
            return 0;
        }
        std::uint64_t v = 0;
        for (int i = 0; i < bytes; ++i) {
            v = (v << 8) | static_cast<std::uint8_t>(data[at + static_cast<std::size_t>(i)]);
        }
        at += static_cast<std::size_t>(bytes);
        return v;
    }
    std::uint8_t u8() { return static_cast<std::uint8_t>(number(1)); }
    std::uint16_t u16() { return static_cast<std::uint16_t>(number(2)); }
    std::int16_t i16() { return static_cast<std::int16_t>(u16()); }
    std::uint32_t u32() { return static_cast<std::uint32_t>(number(4)); }
    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
    std::string_view take(std::uint64_t n) {
        if (!has(n)) {
            bad = true;
            at = data.size();
            return {};
        }
        const std::string_view s = data.substr(at, static_cast<std::size_t>(n));
        at += static_cast<std::size_t>(n);
        return s;
    }
    void skip(std::uint64_t n) { (void)take(n); }
    // Moves to an absolute place, if it is inside the file.
    void seek(std::uint64_t place) {
        if (bad || place > data.size()) {
            bad = true;
            at = data.size();
            return;
        }
        at = static_cast<std::size_t>(place);
    }
};

// One channel's rows, `depth` bits a value, turned into one byte a pixel
// (16-bit values keep their top byte).
struct Plane {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> values;
};

// PackBits, as Photoshop squeezes each row.
bool unpackRow(std::string_view in, std::uint8_t* out, std::size_t length) {
    std::size_t i = 0;
    std::size_t o = 0;
    while (o < length && i < in.size()) {
        const auto n = static_cast<std::int8_t>(in[i++]);
        if (n >= 0) {
            const std::size_t count = static_cast<std::size_t>(n) + 1;
            if (i + count > in.size() || o + count > length) {
                return false;
            }
            std::memcpy(out + o, in.data() + i, count);
            i += count;
            o += count;
        } else if (n != -128) {
            const std::size_t count = static_cast<std::size_t>(1 - n);
            if (i >= in.size() || o + count > length) {
                return false;
            }
            std::memset(out + o, static_cast<std::uint8_t>(in[i++]), count);
            o += count;
        }
    }
    return o == length;
}

// Turns a row of raw values (1 or 2 bytes each) into one byte each.
void narrow(const std::uint8_t* row, int width, int depth, std::uint8_t* out) {
    if (depth == 8) {
        std::memcpy(out, row, static_cast<std::size_t>(width));
    } else {
        for (int x = 0; x < width; ++x) {
            out[x] = row[2 * x];  // the top byte of a big-endian 16-bit value
        }
    }
}

// Undoes ZIP-with-prediction: each value was stored as the difference from
// the one before it in its row.
void unpredict(std::uint8_t* rows, int width, int height, int depth) {
    const std::size_t rowBytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(depth / 8);
    for (int y = 0; y < height; ++y) {
        std::uint8_t* row = rows + static_cast<std::size_t>(y) * rowBytes;
        if (depth == 8) {
            for (int x = 1; x < width; ++x) {
                row[x] = static_cast<std::uint8_t>(row[x] + row[x - 1]);
            }
        } else {
            for (int x = 1; x < width; ++x) {
                const unsigned before = (unsigned{row[2 * x - 2]} << 8) | row[2 * x - 1];
                const unsigned now = ((unsigned{row[2 * x]} << 8) | row[2 * x + 1]) + before;
                row[2 * x] = static_cast<std::uint8_t>((now >> 8) & 0xff);
                row[2 * x + 1] = static_cast<std::uint8_t>(now & 0xff);
            }
        }
    }
}

// Reads one channel whose bytes (without the compression number) are
// `body`. RLE row lengths are at the start of body.
Result<Plane> readPlane(std::string_view body, int compression, int width, int height, int depth, bool big) {
    Plane plane;
    plane.width = width;
    plane.height = height;
    plane.values.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    if (width <= 0 || height <= 0) {
        return plane;
    }
    const std::size_t rowBytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(depth / 8);
    std::vector<std::uint8_t> row(rowBytes);
    if (compression == 0) {
        if (body.size() < rowBytes * static_cast<std::size_t>(height)) {
            return makeError("The Photoshop file is cut short.");
        }
        for (int y = 0; y < height; ++y) {
            narrow(reinterpret_cast<const std::uint8_t*>(body.data()) + rowBytes * static_cast<std::size_t>(y), width,
                   depth, &plane.values[static_cast<std::size_t>(y) * static_cast<std::size_t>(width)]);
        }
        return plane;
    }
    if (compression == 1) {
        Reader counts{body};
        std::vector<std::uint64_t> lengths(static_cast<std::size_t>(height));
        for (auto& n : lengths) {
            n = counts.number(big ? 4 : 2);
        }
        for (int y = 0; y < height; ++y) {
            const std::string_view packed = counts.take(lengths[static_cast<std::size_t>(y)]);
            if (counts.bad || !unpackRow(packed, row.data(), rowBytes)) {
                return makeError("The Photoshop file is damaged.", "a squeezed row does not unpack");
            }
            narrow(row.data(), width, depth, &plane.values[static_cast<std::size_t>(y) * static_cast<std::size_t>(width)]);
        }
        return plane;
    }
    if (compression == 2 || compression == 3) {
        auto raw = zlibDecompress(body, rowBytes * static_cast<std::size_t>(height));
        if (!raw) {
            return raw.error();
        }
        std::string& bytes = raw.value();
        if (bytes.size() < rowBytes * static_cast<std::size_t>(height)) {
            return makeError("The Photoshop file is damaged.", "a ZIP channel is too short");
        }
        auto* rows = reinterpret_cast<std::uint8_t*>(bytes.data());
        if (compression == 3) {
            unpredict(rows, width, height, depth);
        }
        for (int y = 0; y < height; ++y) {
            narrow(rows + rowBytes * static_cast<std::size_t>(y), width, depth,
                   &plane.values[static_cast<std::size_t>(y) * static_cast<std::size_t>(width)]);
        }
        return plane;
    }
    return makeError("This Photoshop file is squeezed in a way VFX Forge can't read.");
}

struct Header {
    bool big = false;  // a .psb, with longer lengths
    int channels = 0;
    int width = 0;
    int height = 0;
    int depth = 8;
    int mode = 3;
};

// Additional layer information whose length is 8 bytes long in a .psb.
bool longInPsb(std::string_view key) {
    static const char* keys[] = {"LMsk", "Lr16", "Lr32", "Layr", "Mt16", "Mt32", "Mtrn",
                                 "Alph", "FMsk", "lnk2", "FEid", "FXid", "PxSD"};
    for (const char* k : keys) {
        if (key == k) {
            return true;
        }
    }
    return false;
}

std::string utf8FromUtf16(std::string_view be) {
    std::string out;
    for (std::size_t i = 0; i + 1 < be.size(); i += 2) {
        unsigned c = (unsigned{static_cast<std::uint8_t>(be[i])} << 8) | static_cast<std::uint8_t>(be[i + 1]);
        if (c >= 0xd800 && c < 0xdc00 && i + 3 < be.size()) {
            const unsigned low = (unsigned{static_cast<std::uint8_t>(be[i + 2])} << 8) |
                                 static_cast<std::uint8_t>(be[i + 3]);
            if (low >= 0xdc00 && low < 0xe000) {
                c = 0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00);
                i += 2;
            }
        }
        if (c == 0) {
            break;
        }
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xc0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3f));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xe0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (c & 0x3f));
        } else {
            out += static_cast<char>(0xf0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3f));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (c & 0x3f));
        }
    }
    return out;
}

// What a layer record says, before its pixels are read.
struct Record {
    int top = 0, left = 0, bottom = 0, right = 0;
    std::vector<std::pair<int, std::uint64_t>> channels;  // id and length
    std::string blend;
    double opacity = 1.0;
    double fill = 1.0;
    bool hidden = false;
    std::string name;
    int section = 0;  // 0 a layer, 1 or 2 a group (open or closed), 3 the end of a group
};

// Puts `top` over `under`, both straight (not premultiplied), at `alpha`.
void over(float* under, const float* top, float alpha) {
    const float a = top[3] * alpha;
    if (a <= 0.0f) {
        return;
    }
    const float outA = a + under[3] * (1.0f - a);
    for (int c = 0; c < 3; ++c) {
        under[c] = (top[c] * a + under[c] * under[3] * (1.0f - a)) / outA;
    }
    under[3] = outA;
}

}  // namespace

bool looksLikePsd(std::string_view bytes) {
    return bytes.size() >= 26 && bytes.substr(0, 4) == "8BPS";
}

Result<PsdPicture> readPsd(std::string_view bytes) {
    if (!looksLikePsd(bytes)) {
        return makeError("This is not a Photoshop file.");
    }
    Reader r{bytes};
    r.skip(4);
    Header h;
    const int version = r.u16();
    if (version != 1 && version != 2) {
        return makeError("This Photoshop file is a kind VFX Forge doesn't know.");
    }
    h.big = version == 2;
    r.skip(6);
    h.channels = r.u16();
    h.height = static_cast<int>(r.u32());
    h.width = static_cast<int>(r.u32());
    h.depth = r.u16();
    h.mode = r.u16();
    if (h.mode != 3 && h.mode != 1) {
        return makeError("Save the picture in RGB colour (Image > Mode > RGB Color) and try again.",
                         "colour mode " + std::to_string(h.mode));
    }
    if (h.depth != 8 && h.depth != 16) {
        return makeError("Save the picture at 8 or 16 bits a channel (Image > Mode) and try again.",
                         std::to_string(h.depth) + " bits");
    }
    if (h.width <= 0 || h.height <= 0 || h.width > 30000 || h.height > 30000 || h.channels < 1 || h.channels > 56) {
        return makeError("This Photoshop file is damaged.", "impossible size");
    }
    const int colours = h.mode == 3 ? 3 : 1;
    if (h.channels < colours) {
        return makeError("This Photoshop file is damaged.", "too few channels");
    }

    // Colour mode data: only for indexed and duotone pictures.
    r.skip(r.u32());

    // Image resources: is the flattened copy real?
    bool realMerged = true;
    {
        const std::uint64_t length = r.u32();
        const std::uint64_t end = r.at + length;
        while (!r.bad && r.at + 12 <= end) {
            if (r.take(4) != "8BIM") {
                break;
            }
            const int id = r.u16();
            const int nameLength = r.u8();
            r.skip(static_cast<std::uint64_t>(nameLength + ((nameLength + 1) % 2)));
            const std::uint64_t size = r.u32();
            const std::uint64_t next = r.at + size + (size % 2);
            if (id == 1057 && size >= 5) {
                (void)r.u32();
                realMerged = r.u8() != 0;
            }
            r.seek(next);
        }
        r.seek(end);
    }
    if (r.bad) {
        return makeError("The Photoshop file is cut short.");
    }

    PsdPicture picture;

    // Layers.
    bool mergedHasAlpha = false;
    std::vector<Record> records;
    {
        const std::uint64_t length = r.number(h.big ? 8 : 4);
        const std::uint64_t end = r.at + length;
        if (length > 0) {
            const std::uint64_t infoLength = r.number(h.big ? 8 : 4);
            const std::uint64_t infoEnd = r.at + infoLength;
            if (infoLength > 0) {
                int count = r.i16();
                if (count < 0) {
                    mergedHasAlpha = true;
                    count = -count;
                }
                for (int i = 0; i < count && !r.bad; ++i) {
                    Record rec;
                    rec.top = r.i32();
                    rec.left = r.i32();
                    rec.bottom = r.i32();
                    rec.right = r.i32();
                    const int channels = r.u16();
                    if (channels > 64) {
                        return makeError("This Photoshop file is damaged.", "too many layer channels");
                    }
                    for (int c = 0; c < channels; ++c) {
                        const int id = r.i16();
                        rec.channels.emplace_back(id, r.number(h.big ? 8 : 4));
                    }
                    if (r.take(4) != "8BIM") {
                        return makeError("This Photoshop file is damaged.", "a layer has no blend mode");
                    }
                    rec.blend = std::string(r.take(4));
                    rec.opacity = r.u8() / 255.0;
                    (void)r.u8();  // clipping
                    const int flags = r.u8();
                    rec.hidden = (flags & 2) != 0;
                    (void)r.u8();
                    const std::uint64_t extra = r.u32();
                    const std::uint64_t extraEnd = r.at + extra;
                    r.skip(r.u32());  // mask
                    r.skip(r.u32());  // blending ranges
                    const int nameLength = r.u8();
                    rec.name = std::string(r.take(static_cast<std::uint64_t>(nameLength)));
                    r.skip(static_cast<std::uint64_t>((4 - (nameLength + 1) % 4) % 4));
                    while (!r.bad && r.at + 12 <= extraEnd) {
                        const std::string_view signature = r.take(4);
                        if (signature != "8BIM" && signature != "8B64") {
                            break;
                        }
                        const std::string key(r.take(4));
                        const std::uint64_t size = r.number(h.big && longInPsb(key) ? 8 : 4);
                        const std::uint64_t next = r.at + size;
                        if (next > extraEnd) {
                            break;
                        }
                        if ((key == "lsct" || key == "lsdk") && size >= 4) {
                            rec.section = static_cast<int>(r.u32());
                        } else if (key == "luni" && size >= 4) {
                            const std::uint64_t letters = r.u32();
                            if (letters * 2 <= size - 4) {
                                rec.name = utf8FromUtf16(r.take(letters * 2));
                            }
                        } else if (key == "iOpa" && size >= 1) {
                            rec.fill = r.u8() / 255.0;
                        }
                        r.seek(next);
                    }
                    r.seek(extraEnd);
                    records.push_back(std::move(rec));
                }

                // Hidden groups hide what is in them. Records run bottom to
                // top, and a group's own record comes after (above) its
                // layers, its end marker before (below) them.
                std::vector<bool> effectiveHidden(records.size(), false);
                std::vector<int> depth(records.size(), 0);
                std::vector<bool> hiddenGroups;
                for (std::size_t k = records.size(); k-- > 0;) {
                    const Record& rec = records[k];
                    const bool insideHidden = std::find(hiddenGroups.begin(), hiddenGroups.end(), true) !=
                                              hiddenGroups.end();
                    if (rec.section == 1 || rec.section == 2) {
                        depth[k] = static_cast<int>(hiddenGroups.size());
                        hiddenGroups.push_back(rec.hidden || insideHidden);
                    } else if (rec.section == 3) {
                        if (!hiddenGroups.empty()) {
                            hiddenGroups.pop_back();
                        }
                        depth[k] = static_cast<int>(hiddenGroups.size());
                    } else {
                        effectiveHidden[k] = rec.hidden || insideHidden;
                        depth[k] = static_cast<int>(hiddenGroups.size());
                    }
                }

                // The pixels, channel by channel, in record order.
                for (std::size_t k = 0; k < records.size() && !r.bad; ++k) {
                    const Record& rec = records[k];
                    const int w = rec.right - rec.left;
                    const int hgt = rec.bottom - rec.top;
                    PsdLayer layer;
                    layer.name = rec.name;
                    layer.left = rec.left;
                    layer.top = rec.top;
                    layer.visible = !effectiveHidden[k];
                    layer.opacity = rec.opacity * rec.fill;
                    layer.blend = rec.blend;
                    layer.group = depth[k];
                    const bool pixels = rec.section == 0 && w > 0 && hgt > 0 && w <= 30000 && hgt <= 30000;
                    if (pixels) {
                        layer.image.width = w;
                        layer.image.height = hgt;
                        layer.image.rgba.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(hgt) * 4u, 255);
                    }
                    bool hasAlpha = false;
                    for (const auto& [id, channelLength] : rec.channels) {
                        const std::string_view block = r.take(channelLength);
                        if (r.bad) {
                            break;
                        }
                        if (!pixels || block.size() < 2 || id < -1 || id >= colours) {
                            continue;  // masks, spot colours, groups
                        }
                        const int compression = (static_cast<std::uint8_t>(block[0]) << 8) |
                                                static_cast<std::uint8_t>(block[1]);
                        auto plane = readPlane(block.substr(2), compression, w, hgt, h.depth, h.big);
                        if (!plane) {
                            return plane.error();
                        }
                        const int slot = id == -1 ? 3 : id;
                        hasAlpha = hasAlpha || id == -1;
                        for (std::size_t p = 0; p < plane.value().values.size(); ++p) {
                            if (colours == 1 && slot == 0) {
                                layer.image.rgba[p * 4] = layer.image.rgba[p * 4 + 1] = layer.image.rgba[p * 4 + 2] =
                                    plane.value().values[p];
                            } else {
                                layer.image.rgba[p * 4 + static_cast<std::size_t>(slot)] = plane.value().values[p];
                            }
                        }
                    }
                    (void)hasAlpha;
                    if (pixels) {
                        picture.layers.push_back(std::move(layer));
                    }
                }
            }
            r.seek(infoEnd);
        }
        r.seek(end);
    }
    if (r.bad) {
        return makeError("The Photoshop file is cut short.");
    }

    // The flattened copy.
    Image merged;
    merged.width = h.width;
    merged.height = h.height;
    merged.rgba.assign(static_cast<std::size_t>(h.width) * static_cast<std::size_t>(h.height) * 4u, 255);
    bool mergedOk = false;
    std::string mergedProblem;
    {
        const int compression = r.u16();
        const std::size_t pixels = static_cast<std::size_t>(h.width) * static_cast<std::size_t>(h.height);
        const std::size_t rowBytes = static_cast<std::size_t>(h.width) * static_cast<std::size_t>(h.depth / 8);
        const int wanted = std::min(h.channels, colours + (mergedHasAlpha ? 1 : 0));
        std::vector<std::vector<std::uint8_t>> planes;
        if (compression == 0) {
            for (int c = 0; c < wanted && !r.bad; ++c) {
                auto plane = readPlane(r.take(rowBytes * static_cast<std::size_t>(h.height)), 0, h.width, h.height,
                                       h.depth, h.big);
                if (!plane) {
                    mergedProblem = plane.error().message;
                    break;
                }
                planes.push_back(std::move(plane.value().values));
            }
        } else if (compression == 1) {
            const std::size_t countSize = h.big ? 4 : 2;
            std::vector<std::uint64_t> lengths(static_cast<std::size_t>(h.channels) * static_cast<std::size_t>(h.height));
            for (auto& n : lengths) {
                n = r.number(static_cast<int>(countSize));
            }
            for (int c = 0; c < wanted && !r.bad; ++c) {
                std::vector<std::uint8_t> values(pixels);
                std::vector<std::uint8_t> row(rowBytes);
                for (int y = 0; y < h.height; ++y) {
                    const std::string_view packed =
                        r.take(lengths[static_cast<std::size_t>(c) * static_cast<std::size_t>(h.height) +
                                       static_cast<std::size_t>(y)]);
                    if (r.bad || !unpackRow(packed, row.data(), rowBytes)) {
                        mergedProblem = "a squeezed row of the flattened copy does not unpack";
                        r.bad = true;
                        break;
                    }
                    narrow(row.data(), h.width, h.depth, &values[static_cast<std::size_t>(y) * static_cast<std::size_t>(h.width)]);
                }
                planes.push_back(std::move(values));
            }
        } else {
            mergedProblem = "the flattened copy is squeezed in an unknown way";
        }
        if (!r.bad && mergedProblem.empty() && static_cast<int>(planes.size()) == wanted) {
            mergedOk = true;
            for (std::size_t p = 0; p < pixels; ++p) {
                for (int c = 0; c < 3; ++c) {
                    merged.rgba[p * 4 + static_cast<std::size_t>(c)] = planes[colours == 3 ? static_cast<std::size_t>(c) : 0][p];
                }
                merged.rgba[p * 4 + 3] = mergedHasAlpha ? planes[static_cast<std::size_t>(colours)][p] : 255;
            }
            // Photoshop lays the see-through parts of the flattened copy over
            // white; take the white back out.
            if (mergedHasAlpha) {
                for (std::size_t p = 0; p < pixels; ++p) {
                    std::uint8_t* px = &merged.rgba[p * 4];
                    const float a = px[3] / 255.0f;
                    if (a <= 0.0f) {
                        px[0] = px[1] = px[2] = 0;
                        continue;
                    }
                    for (int c = 0; c < 3; ++c) {
                        const float v = (px[c] / 255.0f - (1.0f - a)) / a;
                        px[c] = static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
                    }
                }
            }
        }
    }

    const bool anyVisibleLayer = std::any_of(picture.layers.begin(), picture.layers.end(),
                                             [](const PsdLayer& l) { return l.visible; });
    if (mergedOk && (realMerged || !anyVisibleLayer)) {
        picture.merged = std::move(merged);
        picture.fromFlattenedCopy = true;
        return picture;
    }
    if (!anyVisibleLayer) {
        return makeError("This Photoshop file has nothing VFX Forge can show.", mergedProblem);
    }

    // No flattened copy: put the visible layers together as Normal layers.
    picture.fromFlattenedCopy = false;
    picture.notes.push_back(
        "Photoshop didn't keep a flattened copy in this file, so layer styles, adjustment layers and masks "
        "are not shown. Turn on Maximize PSD and PSB File Compatibility in Photoshop's File Handling settings "
        "and save again.");
    std::vector<float> canvas(static_cast<std::size_t>(h.width) * static_cast<std::size_t>(h.height) * 4u, 0.0f);
    std::vector<std::string> otherBlends;
    for (const PsdLayer& layer : picture.layers) {
        if (!layer.visible || layer.opacity <= 0.0) {
            continue;
        }
        if (layer.blend != "norm" && layer.blend != "pass" &&
            std::find(otherBlends.begin(), otherBlends.end(), layer.name) == otherBlends.end()) {
            otherBlends.push_back(layer.name);
        }
        for (int y = 0; y < layer.image.height; ++y) {
            const int cy = layer.top + y;
            if (cy < 0 || cy >= h.height) {
                continue;
            }
            for (int x = 0; x < layer.image.width; ++x) {
                const int cx = layer.left + x;
                if (cx < 0 || cx >= h.width) {
                    continue;
                }
                const std::uint8_t* src = &layer.image.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(layer.image.width) +
                                                             static_cast<std::size_t>(x)) * 4u];
                const float top[4] = {src[0] / 255.0f, src[1] / 255.0f, src[2] / 255.0f, src[3] / 255.0f};
                over(&canvas[(static_cast<std::size_t>(cy) * static_cast<std::size_t>(h.width) + static_cast<std::size_t>(cx)) * 4u],
                     top, static_cast<float>(layer.opacity));
            }
        }
    }
    if (!otherBlends.empty()) {
        std::string list;
        for (std::size_t i = 0; i < otherBlends.size() && i < 4; ++i) {
            list += (i ? ", " : "") + otherBlends[i];
        }
        picture.notes.push_back("Drawn as Normal, not with their blend mode: " + list + ".");
    }
    Image out;
    out.width = h.width;
    out.height = h.height;
    out.rgba.resize(canvas.size());
    for (std::size_t i = 0; i < canvas.size(); ++i) {
        out.rgba[i] = static_cast<std::uint8_t>(std::lround(std::clamp(canvas[i], 0.0f, 1.0f) * 255.0f));
    }
    picture.merged = std::move(out);
    return picture;
}

namespace {

struct Writer {
    std::string out;
    void number(std::uint64_t v, int bytes) {
        for (int i = bytes - 1; i >= 0; --i) {
            out += static_cast<char>((v >> (8 * i)) & 0xff);
        }
    }
    void text(std::string_view s) { out.append(s); }
};

}  // namespace

std::string writePsd(const Image& image, std::string_view layerName) {
    const int w = image.width;
    const int h = image.height;
    const std::size_t pixels = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    Writer f;
    f.text("8BPS");
    f.number(1, 2);
    f.number(0, 6);
    f.number(4, 2);  // red, green, blue and the see-through channel
    f.number(static_cast<std::uint64_t>(h), 4);
    f.number(static_cast<std::uint64_t>(w), 4);
    f.number(8, 2);
    f.number(3, 2);  // RGB
    f.number(0, 4);  // no colour mode data

    // Image resources: say the flattened copy is real.
    Writer res;
    res.text("8BIM");
    res.number(1057, 2);
    res.number(0, 2);  // empty name, padded
    Writer info;
    info.number(1, 4);
    info.number(1, 1);
    info.number(0, 4);  // writer name: none
    info.number(0, 4);  // reader name: none
    info.number(1, 4);
    res.number(info.out.size(), 4);
    res.text(info.out);
    if (info.out.size() % 2) {
        res.number(0, 1);
    }
    f.number(res.out.size(), 4);
    f.text(res.out);

    // One layer.
    std::string name(layerName.substr(0, 255));
    Writer layer;
    layer.number(static_cast<std::uint64_t>(static_cast<std::uint16_t>(static_cast<std::int16_t>(-1))), 2);
    layer.number(0, 4);
    layer.number(0, 4);
    layer.number(static_cast<std::uint64_t>(h), 4);
    layer.number(static_cast<std::uint64_t>(w), 4);
    layer.number(4, 2);
    const int ids[4] = {-1, 0, 1, 2};
    for (int id : ids) {
        layer.number(static_cast<std::uint16_t>(static_cast<std::int16_t>(id)), 2);
        layer.number(2 + pixels, 4);
    }
    layer.text("8BIMnorm");
    layer.number(255, 1);
    layer.number(0, 1);
    layer.number(8, 1);
    layer.number(0, 1);
    const std::size_t namePadded = (name.size() + 1 + 3) / 4 * 4;
    // A Unicode name too, which newer Photoshops prefer.
    Writer unicode;
    unicode.number(name.size(), 4);
    for (unsigned char c : name) {
        unicode.number(c < 0x80 ? c : '_', 2);
    }
    while (unicode.out.size() % 4) {
        unicode.number(0, 1);
    }
    layer.number(4 + 4 + namePadded + 12 + unicode.out.size(), 4);
    layer.number(0, 4);
    layer.number(0, 4);
    layer.number(name.size(), 1);
    layer.text(name);
    for (std::size_t i = name.size() + 1; i < namePadded; ++i) {
        layer.number(0, 1);
    }
    layer.text("8BIMluni");
    layer.number(unicode.out.size(), 4);
    layer.text(unicode.out);
    const int order[4] = {3, 0, 1, 2};
    for (int slot : order) {
        layer.number(0, 2);
        for (std::size_t p = 0; p < pixels; ++p) {
            layer.out += static_cast<char>(image.rgba[p * 4 + static_cast<std::size_t>(slot)]);
        }
    }
    if (layer.out.size() % 2) {
        layer.number(0, 1);
    }
    f.number(4 + layer.out.size() + 4, 4);
    f.number(layer.out.size(), 4);
    f.text(layer.out);
    f.number(0, 4);  // no global mask

    // The flattened copy, laid over white as Photoshop does.
    f.number(0, 2);
    for (int c = 0; c < 4; ++c) {
        for (std::size_t p = 0; p < pixels; ++p) {
            const std::uint8_t* px = &image.rgba[p * 4];
            if (c == 3) {
                f.out += static_cast<char>(px[3]);
            } else {
                const float a = px[3] / 255.0f;
                const float v = px[c] / 255.0f * a + (1.0f - a);
                f.out += static_cast<char>(std::lround(v * 255.0f));
            }
        }
    }
    return f.out;
}

}  // namespace vfx::editor
