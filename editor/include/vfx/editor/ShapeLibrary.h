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

// ---- shapes that can be changed
//
// Some shapes have settings of their own: how many points a star has, how
// thick a ring is, how round a square's corners are (ShapeEdit, in
// Program.h; a layer keeps them in its Sprite module). With every setting
// at zero a shape is the shape as it was drawn. A changed shape is drawn
// from its own picture, like a library shape, whichever kind it started as.

// What can be changed about a shape. For most shapes, nothing.
struct ShapeEditInfo {
    int countLowest = 0, countHighest = 0;  // both 0: nothing to count
    int countOwn = 0;                       // how many it has as drawn
    const char* countLabel = "";            // "Points", "Sides", "Teeth"...
    bool thickness = false;
    bool sharpness = false;
    bool round = false;
    bool sharperToo = false;  // roundness goes below 0 as well: corners can be made sharper than drawn

    bool count() const { return countHighest > 0; }
    bool any() const { return count() || thickness || sharpness || round; }
};
const ShapeEditInfo& shapeEditInfo(int number);

// The part of a change this shape can use, in the exact form it is drawn
// with: settings the shape does not have are dropped, the count is held to
// the shape's range (and its own count becomes 0, "as drawn"), amounts are
// held to whole steps.
ShapeEdit usableEdit(int number, const ShapeEdit& asked);

// What any shape looks like at one point with a change applied. With
// nothing to change this is sampleShape.
ShapeSample sampleShapeEdited(int number, const ShapeEdit& edit, float x, float y, float aaX, float aaY);

// True when the graphics card draws this from a picture and not from its
// own formula: every library shape, and any shape that has been changed.
bool drawnFromPicture(int number, const ShapeEdit& edit);

// ---- pictures of shapes, for the graphics card

// How many pixels a side a library shape's picture is.
inline constexpr int kShapePictureSize = 512;

// A shape as a picture the size asked for: red is the tone (128 for none,
// lighter above, darker below), green the shine, blue the coverage; alpha
// is always full. Any shape can be asked for.
Image drawShapePicture(int number, int size, const ShapeEdit& edit = {});

// The same, drawn the first time it is asked for and kept. Safe to call
// from any thread. The main view draws from pictures of kShapePictureSize;
// small previews ask for small ones (size is held between 8 and that).
// A shape as drawn is kept for good; of changed shapes only the last few
// asked for are, since every stop of a slider is another.
std::shared_ptr<const Image> shapePicture(int number, int size = kShapePictureSize, const ShapeEdit& edit = {});

// The picture a shape is drawn with stands in the list of a mesh's pictures
// under an ID of its own, which says which shape and which change to it.
// No asset has such an ID.
Id shapeTextureId(int number, const ShapeEdit& edit = {});
// The shape such an ID is for (and the change to it), or -1 when it is an
// ordinary asset's, or names a shape that is not drawn from a picture.
int shapeFromTextureId(Id id, ShapeEdit* edit = nullptr);

}  // namespace vfx::editor
