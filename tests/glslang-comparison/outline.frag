#version 450

layout(location=0) in vec2 iUV;

layout(location = 0) out vec4 oColor;

layout(set = 2, binding = 0) uniform uniforms {
    vec4 frame;
    vec2 size;
    vec2 padding;
    float time;
};

layout(set = 3, binding = 0) uniform sampler2D mainTex;


vec3 hsv2rgb(vec3 hsv) {
    vec4 K = vec4 (1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(hsv.xxx + K.xyz) * 6.0 - K.www);
    return mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), hsv.yyy) * hsv.z;
}

vec4 get_outline_color(float t, vec2 pos) {
    vec2 centered = pos - 0.5;
    float angle = atan(centered.y, centered.x);

    return vec4(hsv2rgb(vec3(fract(t + (angle / 6.28)), 1.0, 1.0)), 1.0);
}

void main() {
    vec2 uv = iUV;
    uv.y = 1.0 - uv.y;
    vec2 og_uv = uv;

    vec4 bg_color = vec4(0.0, 0.0, 0.0, 1.0);

    uv = frame.xy + uv.xy * frame.zw;

    int outline = 0;
    vec2 pixel = vec2(1.0, 1.0) / (frame.zw * size);

    vec4 center = texture(mainTex, uv);

    for (int x = -1; x < 2; x++) {
        for (int y = -1; y < 2; y++) {
            if (x == 0 && y == 0) {
                continue;
            }

            vec2 pos = uv + pixel * vec2(x, y);

            vec4 s = texture(mainTex, pos);

            if (s.a > 0.0) {
                outline = 1;
                break;
            }
        }
    }

    if (center.a == 0.0) {
        if (outline > 0) {
            center = get_outline_color(time, og_uv);
        }
        else {
            center = bg_color;
        }
    }

    oColor = center;
}

