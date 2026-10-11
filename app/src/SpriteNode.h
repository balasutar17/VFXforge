// VFX Forge app: handing a particle mesh to the graphics card.
//
// The viewport, the library's live previews and the shape icons all draw
// the same way: the editor layer builds a SpriteMesh, and this turns it
// into scene-graph nodes. The shapes worked out from formulas all share one
// material; each of the artist's pictures, and each shape from the shape
// library, gets a material of its own, with its texture.
#pragma once

#include <QSGNode>

#include "vfx/editor/SpriteMesh.h"

class QQuickWindow;

namespace vfx::editor {
class ImageSet;
}

// Creates or refreshes the node for a mesh. Call from updatePaintNode.
// images is needed only when the mesh draws the artist's pictures; window
// when it draws those or shapes from the library (without it they are shown
// as soft dots). shapePixels is how big a picture a library shape is drawn
// from: the full size for the main view, far less for a small preview.
QSGNode* updateSpriteNode(QSGNode* old, const vfx::editor::SpriteMesh& mesh,
                          const vfx::editor::ImageSet* images = nullptr,
                          QQuickWindow* window = nullptr, int shapePixels = 512);
