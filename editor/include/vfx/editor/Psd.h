// VFX Forge editor: Photoshop files.
//
// A layer's picture can come straight from a Photoshop document (.psd or
// .psb), so an artist can keep painting in layers and save. VFX Forge reads
// the flattened copy Photoshop keeps inside the file, which shows layer
// styles, adjustments and blend modes exactly as Photoshop does. When that
// copy is missing (Maximize Compatibility turned off) it puts the visible
// layers together itself, as Normal layers, and says what it could not show.
//
// It also writes a simple one-layer document with a see-through background,
// for "Edit in Photoshop".
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "vfx/Result.h"
#include "vfx/editor/Image.h"

namespace vfx::editor {

// One pixel layer of a document, as stored.
struct PsdLayer {
    std::string name;
    int left = 0;
    int top = 0;
    Image image;             // the layer's own pixels, its own size
    bool visible = true;     // false also when a group it is in is hidden
    double opacity = 1.0;    // the layer's opacity times its fill
    std::string blend;       // Photoshop's four-letter key: "norm", "scrn", "lddg"...
    int group = 0;           // how many groups deep it is (0 at the top)
};

struct PsdPicture {
    Image merged;                    // the whole picture as Photoshop shows it
    std::vector<PsdLayer> layers;    // pixel layers, bottom to top
    bool fromFlattenedCopy = true;   // false when VFX Forge put the layers together
    std::vector<std::string> notes;  // what is not shown exactly as Photoshop does
};

// True when the bytes start like a Photoshop document.
bool looksLikePsd(std::string_view bytes);

// Reads an RGB or greyscale document, 8 or 16 bits a channel.
Result<PsdPicture> readPsd(std::string_view bytes);

// A document of one layer, named layerName, holding the picture with its
// see-through parts see-through. Photoshop opens it as an ordinary layer.
std::string writePsd(const Image& image, std::string_view layerName);

}  // namespace vfx::editor
