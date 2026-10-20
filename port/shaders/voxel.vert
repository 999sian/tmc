#version 450
// Voxel view (port_voxel.cpp): world-space quads for the room layers and
// sprite billboards, plus screen-space HUD quads drawn with an ortho matrix.

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;    // GBA-pixel coordinates, decoded in the fragment shader
layout(location = 2) in uvec4 aParams;

layout(set = 1, binding = 0) uniform Camera {
    mat4 uMvp;
};

layout(location = 0) out vec2 vUv;
layout(location = 1) flat out uvec4 vParams;
layout(location = 2) out vec3 vWorld; // world position, for the fade in voxel.frag

void main() {
    vUv = aUv;
    vParams = aParams;
    vWorld = aPos;
    gl_Position = uMvp * vec4(aPos, 1.0);
}
