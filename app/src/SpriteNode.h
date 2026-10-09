// VFX Forge app: handing a particle mesh to the graphics card.
//
// The viewport, the library's live previews and the shape icons all draw
// the same way: the editor layer builds a SpriteMesh, and this turns it
// into scene-graph nodes. The built-in shapes all share one material; each
// of the artist's pictures gets a material of its own, with its texture.
#pragma once

#include <QSGNode>

#include "vfx/editor/SpriteMesh.h"

class QQuickWindow;

namespace vfx::editor {
class ImageSet;
}

// Creates or refreshes the node for a mesh. Call from updatePaintNode.
// images and window are needed only when the mesh draws pictures.
QSGNode* updateSpriteNode(QSGNode* old, const vfx::editor::SpriteMesh& mesh,
                          const vfx::editor::ImageSet* images = nullptr,
                          QQuickWindow* window = nullptr);
