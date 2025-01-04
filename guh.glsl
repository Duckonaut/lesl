#version 450 core

layout(location = 0) in vec4 color;

layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform sampler2D tex;

void main() {
    fragColor = color + texture(tex, color.xy);
}
