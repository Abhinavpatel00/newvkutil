



#version 450
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform Params {
    vec2  uResolution;
    float uTime;
} params;

float rand(float x) { return fract(sin(x) * 43758.5453); }

void main() {
    vec2 uv = gl_FragCoord.xy / params.uResolution;
    vec2 p = uv * 2.0 - 1.0;
    p.x *= params.uResolution.x / params.uResolution.y;

    float t = params.uTime * 1.5;

    float a = atan(p.y, p.x);
    float r = length(p);

    float z = 1.0 / max(r, 0.001);
    float streaks = smoothstep(0.0, 0.2, abs(sin(a * 20.0 + t * 3.0)));

    float depth = fract(z + t);
    float stars = smoothstep(0.97, 1.0, rand(floor(z * 20.0 + a * 10.0)));

    vec3 col = vec3(0.02, 0.03, 0.05);
    col += vec3(0.2, 0.6, 1.0) * streaks * (1.0 - depth) * 0.8;

    outColor = vec4(col, 1.0);
}
