// VFX Forge editor: the byte formats exports are written in.
//
// Small, dependency-free writers for compressed data (zlib and gzip), tar
// archives and PNG images. They exist so exports need nothing that is not
// already in this repository.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vfx::editor {

std::uint32_t crc32(std::string_view bytes, std::uint32_t crc = 0);
std::uint32_t adler32(std::string_view bytes);

// Raw DEFLATE data. It finds runs of repeated bytes and repeated rows (the
// two things effect pictures are full of) and codes them with the fixed
// Huffman tables. Any inflater reads it.
std::string deflate(std::string_view bytes, std::size_t rowLength = 0);

// The same, wrapped as zlib (for PNG) or gzip (for .unitypackage files).
std::string zlibCompress(std::string_view bytes, std::size_t rowLength = 0);
std::string gzipCompress(std::string_view bytes);

// A tar archive of regular files, in the ustar layout.
struct TarEntry {
    std::string path;  // forward slashes, at most 255 bytes
    std::string bytes;
};
std::string makeTar(const std::vector<TarEntry>& entries);

// A PNG image. channels is 1 (grey), 2 (grey and alpha), 3 (RGB) or 4 (RGBA);
// pixels holds width * height * channels bytes, top row first.
std::string encodePngImage(int width, int height, int channels, std::string_view pixels);

}  // namespace vfx::editor
