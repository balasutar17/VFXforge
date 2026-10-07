#version 440

// One corner of a particle, already placed in viewport pixels by the editor.
layout(location = 0) in vec4 position;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in vec4 color;
layout(location = 3) in vec3 shapeInfo;  // which shape, then the size of a pixel across and down it

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec4 vColor;
layout(location = 2) out vec3 vShape;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
};

void main() {
    vTexCoord = texCoord;
    vColor = color;
    vShape = shapeInfo;
    gl_Position = qt_Matrix * position;
}
