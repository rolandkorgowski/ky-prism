#version 440
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 tint;
    vec2 texelStep;
    float intensity;
    float motion;
} u;
layout(binding = 1) uniform sampler2D sourceTexture;
void main() {
    vec2 uv = qt_TexCoord0;
    vec2 radial = uv - vec2(0.5);
    float radius = length(radial * 2.0);
    vec2 axis = radial / max(length(radial), 0.01);
    vec2 side = vec2(-axis.y, axis.x) * u.texelStep * 1.7;
    vec2 stepAlong = axis * u.texelStep * (1.5 + u.motion * 5.0);
    float a = texture(sourceTexture, uv).a * 0.20;
    a += texture(sourceTexture, uv + side).a * 0.10;
    a += texture(sourceTexture, uv - side).a * 0.10;
    a += texture(sourceTexture, uv + stepAlong).a * 0.16;
    a += texture(sourceTexture, uv - stepAlong).a * 0.10;
    a += texture(sourceTexture, uv + stepAlong * 2.0).a * 0.12;
    a += texture(sourceTexture, uv - stepAlong * 2.0).a * 0.06;
    a += texture(sourceTexture, uv + stepAlong * 3.0 + side).a * 0.08;
    a += texture(sourceTexture, uv + stepAlong * 3.0 - side).a * 0.08;
    a *= u.intensity * (1.0 - smoothstep(0.76, 1.0, radius));
    a = clamp(a, 0.0, 0.30);
    // Premultiplied alpha: the effect is glow only, never an opaque panel.
    fragColor = vec4(u.tint.rgb * a, a) * u.qt_Opacity;
}
