#version 440

// One corner of a particle, already placed in viewport pixels by the editor.
layout(location = 0) in vec4 position;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in vec4 color;
layout(location = 3) in vec4 shapeInfo;  // which shape, the size of a pixel across and down it, and
                                         // how brightly a dissolving edge glows
layout(location = 4) in vec4 extra;      // the corner on the particle's own -1..1 square, then how far
                                         // it is eaten away (0 to 1) and the particle's own random number
layout(location = 5) in float soft;      // how soft a dissolving edge is, 0 to 1

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec4 vColor;
layout(location = 2) out vec4 vShape;
layout(location = 3) out vec4 vExtra;
layout(location = 4) out float vSoft;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
};

void main() {
    vTexCoord = texCoord;
    vColor = color;
    vShape = shapeInfo;
    vExtra = extra;
    vSoft = soft;
    gl_Position = qt_Matrix * position;
}
