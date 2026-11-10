#pragma once
namespace PrismShaders {
inline constexpr const char *vertex=R"(#version 330 core
 layout(location=0) in vec2 position;
 layout(location=1) in vec2 texcoord;
 out vec2 uv;
 void main(){uv=texcoord;gl_Position=vec4(position,0,1);})";
inline constexpr const char *fragment=R"(#version 330 core
 in vec2 uv; out vec4 color; uniform sampler2D photo; uniform float opacity=1.0;
 void main(){vec4 s=texture(photo,uv);color=vec4(mix(vec3(0.075),s.rgb,s.a),opacity);})";
}
