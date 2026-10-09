// VFX Forge editor: the artist's own pictures.
//
// A layer can draw a picture or a sprite sheet the artist painted instead of
// a built-in shape. The picture lives as a PNG file in the effect's project
// folder (next to the .vfx file, in "images/"), and the effect refers to it
// by that relative path. This file reads those PNGs and keeps the decoded
// pixels for the renderers.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "vfx/Effect.h"
#include "vfx/Id.h"
#include "vfx/Result.h"

namespace vfx::editor {

// Pixels as painted: four bytes a pixel (red, green, blue, alpha), top row
// first, colours as the screen shows them, alpha not multiplied in.
struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

// The picture and ever smaller copies of it, each half the size of the one
// before, down to one pixel. Colours are premultiplied, so see-through edges
// never darken when pixels are averaged. Drawing a picture small reads the
// copy closest in size, which keeps it from shimmering.
struct MipChain {
    std::vector<Image> levels;
};
MipChain makeMipChain(const Image& image);

// The colour at (u, v), 0 to 1 across the whole picture, premultiplied, 0 to
// 1 per channel, smoothed between pixels and clamped at the edges. footprint
// is how many of the picture's pixels one screen pixel covers.
void sampleMips(const MipChain& mips, float u, float v, float footprint, float out[4]);

// The DEFLATE decoder, for data any PNG or zlib writer produced.
// Fails on damaged data rather than reading past the end.
Result<std::string> inflate(std::string_view deflated, std::size_t sizeHint = 0);
Result<std::string> zlibDecompress(std::string_view zlib, std::size_t sizeHint = 0);

// Reads a PNG file's bytes: every colour type, bit depth and interlacing the
// format allows. Pictures larger than 8192 pixels a side are refused.
Result<Image> decodePng(std::string_view bytes);

// The opposite, for writing an Image back out.
std::string encodePng(const Image& image);

// Where the pictures of a project go, relative to its folder.
inline constexpr const char* kImagesFolder = "images";

// A file name for a picture that will not collide with a different one: the
// artist's own name, cleaned up, plus a short fingerprint of the contents.
// "Fire Sheet.png" becomes "images/fire-sheet-1a2b3c4d.png".
std::string imageAssetPath(std::string_view originalName, std::string_view pngBytes);

// The decoded pictures of one effect, by asset ID.
class ImageSet {
public:
    const Image* find(Id asset) const;
    const MipChain* mips(Id asset) const;
    std::shared_ptr<const Image> shared(Id asset) const;
    bool has(Id asset) const { return find(asset) != nullptr; }
    void put(Id asset, std::shared_ptr<const Image> image, std::string path);
    void clear() { images_.clear(); }
    std::size_t size() const { return images_.size(); }

    // Loads every texture asset of the effect from the project folder, and
    // forgets ones that are gone. A picture already loaded from the same
    // path is kept. Problems (missing or unreadable files) are returned as
    // messages, one per asset; the rest still load.
    std::vector<std::string> sync(const Effect& effect, const std::filesystem::path& folder);

    // Bumps whenever a picture is added, replaced or forgotten.
    std::uint64_t revision() const { return revision_; }

    // A picture that could not be loaded is not tried again on every sync
    // (a missing file would otherwise be looked for 60 times a second).
    // This makes the next sync try them all again.
    void retryFailed() { failed_.clear(); }

private:
    struct Entry {
        std::shared_ptr<const Image> image;
        mutable std::shared_ptr<const MipChain> mips;
        std::string path;
    };
    std::map<std::uint64_t, Entry> images_;
    std::map<std::uint64_t, std::pair<std::string, std::string>> failed_;  // path, problem
    std::uint64_t revision_ = 0;
};

}  // namespace vfx::editor
