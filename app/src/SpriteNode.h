// VFX Forge app: handing a particle mesh to the graphics card.
//
// The viewport, the library's live previews and the shape icons all draw
// the same way: the editor layer builds a SpriteMesh, and this turns it
// into scene-graph nodes. The shapes worked out from formulas all share one
// material; each of the artist's pictures, and each shape from the shape
// library, gets a material of its own, with its texture.
#pragma once

#include <QSGNode>
#include <QSizeF>

#include "vfx/editor/SpriteMesh.h"

class QQuickWindow;
class QSGTexture;

namespace vfx::editor {
class ImageSet;
}

// A model's distances, as a picture the graphics card reads, so that the
// particles behind the model are hidden pixel by pixel. Sixteen bits of
// distance in red and green between nearest and furthest, blue where there
// is any model.
struct SpriteDepth {
    QSGTexture* texture = nullptr;  // null: nothing is hidden
    bool perspective = false;       // a 3D effect: corners carry one over their distance
    float fade = 0.15f;
    float nearest = 0.0f, furthest = 1.0f;
};

// Creates or refreshes the node for a mesh. Call from updatePaintNode.
// images is needed only when the mesh draws the artist's pictures. window
// is needed always. shapePixels is how big a picture a library shape is
// drawn from: the full size for the main view, far less for a small
// preview. itemSize is the item's own size; depth, when given, hides what
// is behind a model.
QSGNode* updateSpriteNode(QSGNode* old, const vfx::editor::SpriteMesh& mesh,
                          const vfx::editor::ImageSet* images, QQuickWindow* window, int shapePixels,
                          QSizeF itemSize, const SpriteDepth* depth = nullptr);
