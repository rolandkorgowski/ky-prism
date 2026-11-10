#pragma once
namespace ThumbnailShaders {
inline constexpr const char *vertex = R"(#version 330 core
layout(location=0) in vec2 position;
out vec2 uv; uniform vec4 dest; uniform vec2 viewportSize;
void main(){ vec2 p=dest.xy+position*dest.zw;
gl_Position=vec4(2.0*p.x/viewportSize.x-1.0,1.0-2.0*p.y/viewportSize.y,0,1); uv=position; })";
inline constexpr const char *fragment = R"(#version 330 core
in vec2 uv; out vec4 color; uniform sampler2D photo;
void main(){ color=texture(photo,uv); color.rgb*=color.a; })";
} // namespace ThumbnailShaders
