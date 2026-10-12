#version 440

// One corner of a particle, already placed in viewport pixels by the editor.
layout(location = 0) in vec4 position;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in vec4 color;
layout(location = 3) in vec4 shapeInfo;  // which shape, the size of a pixel across and down it, and
                                         // how brightly a dissolving edge glows
layout(location = 4) in vec4 extra;      // the corner on the particle's own -1..1 square, then how far
                                         // it is eaten away (0 to 1) and the particle's own random number
layout(location = 5) in vec2 softDepth;  // how soft a dissolving edge is (0 to 1), and how far away
                                         // the corner is (see SpriteVertex::depth)

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec4 vColor;
layout(location = 2) out vec4 vShape;
layout(location = 3) out vec4 vExtra;
layout(location = 4) out float vSoft;
layout(location = 5) out float vDepth;
layout(location = 6) out vec2 vScreen;  // where on the viewport, 0 to 1 across and down

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float modelOn;       // 1: what is behind the model is hidden
    float perspective;   // 1: a corner's depth comes as one over the distance (3D)
    float fade;          // how far behind the model's surface a particle fades out, in world units
    vec2 itemSize;       // the viewport, in its own pixels
    vec2 depthRange;     // the nearest and furthest of the model's surface
};

void main() {
    vTexCoord = texCoord;
    vColor = color;
    vShape = shapeInfo;
    vExtra = extra;
    vSoft = softDepth.x;
    vDepth = softDepth.y;
    vScreen = position.xy / max(itemSize, vec2(1.0));
    gl_Position = qt_Matrix * position;
}
