// VFX Forge editor: the shape library.
//
// The first thirty-five particle shapes are formulas written twice, once
// here for the picture renderer and once for the graphics card (see
// Shapes.h). The library carries on from there with many more, and these are
// written once: a formula here, turned into a picture the first time a
// shape is used, which the graphics card (and Unity) then draws like any
// other picture. So a library shape costs nothing until it is used, looks
// the same everywhere, and adding one never touches a shader.
//
// A shape's number is its place in the "shape" property's list of options,
// whichever kind it is: 0 to kSpriteShapeCount - 1 are the formula shapes,
// the rest are library shapes.
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "vfx/Id.h"
#include "vfx/Program.h"
#include "vfx/editor/Image.h"
#include "vfx/editor/Shapes.h"

namespace vfx::editor {

struct ShapeInfo {
    const char* name;      // as stored in files: lower-case letters, no spaces
    const char* label;     // for the artist
    const char* category;  // "Basic", "Stars and sparkles", ...
};

// Every shape there is, formula shapes first, in the order the "shape"
// property lists them.
int shapeCount();
const ShapeInfo& shapeInfo(int number);  // number is clamped into range
// A shape's number from its name, or -1.
int shapeNumber(std::string_view name);
inline bool isLibraryShape(int number) { return number >= kSpriteShapeCount && number < shapeCount(); }

// The categories, in the order the picker shows them.
const std::vector<std::string>& shapeCategories();

// What a library shape looks like at one point, exactly as sampleShape says
// for the formula shapes. (sampleShape itself answers for both kinds; this
// is the part behind it.)
ShapeSample sampleLibraryShape(int number, float x, float y, float aaX, float aaY);

// ---- pictures of shapes, for the graphics card

// How many pixels a side a library shape's picture is.
inline constexpr int kShapePictureSize = 512;

// A shape as a picture the size asked for: red is the tone (128 for none,
// lighter above, darker below), green the shine, blue the coverage; alpha
// is always full. Any shape can be asked for.
Image drawShapePicture(int number, int size);

// The same, drawn the first time it is asked for and kept. Safe to call
// from any thread. The main view draws from pictures of kShapePictureSize;
// small previews ask for small ones (size is held between 8 and that).
std::shared_ptr<const Image> shapePicture(int number, int size = kShapePictureSize);

// The picture a library shape is drawn with stands in the list of a mesh's
// pictures under an ID of its own, which no asset can have.
Id shapeTextureId(int number);
// The shape such an ID is for, or -1 when it is an ordinary asset's.
int shapeFromTextureId(Id id);

}  // namespace vfx::editor
