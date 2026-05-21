#version 450

layout(location = 0) in vec2 iUV;

layout(location = 0) out vec4 oColor;

layout(set = 2, binding = 0) uniform uniforms {
    vec4 data;
};

void main() {
    vec2 uv = iUV;
    float t = data.x;
    if (uv.x < 0.5) {
        if (uv.y < 0.5) {
            discard;
        }
        float f = cos(uv.x * 20.0 + t);
        oColor = vec4(f, f, f, 1.0);
    }
    else {
        if (uv.y > 0.5) {
            float f = cos(uv.y * 20.0 + t);
            oColor = vec4(f, f, f, 1.0);
        }
        else {
            float f = cos(uv.y * 40.0 + t);
            oColor = vec4(f, f, f, 1.0);
        }
    }
}
