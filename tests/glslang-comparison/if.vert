#version 450

layout(location=0) in vec4 position;
layout(location=1) in vec2 iUV;

layout(location=0) out vec2 oUV;

void main() {
    oUV = iUV;
    gl_Position = position;
}
