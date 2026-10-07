// VFX Forge app: handing a particle mesh to the graphics card.
//
// The viewport, the library's live previews and the shape icons all draw
// the same way: the editor layer builds a SpriteMesh, and this turns it
// into one scene-graph node with one material.
#pragma once

#include <QSGNode>

#include "vfx/editor/SpriteMesh.h"

// Creates or refreshes the node for a mesh. Returns null (and deletes the
// old node) when there is nothing to draw. Call from updatePaintNode.
QSGNode* updateSpriteNode(QSGNode* old, const vfx::editor::SpriteMesh& mesh);
