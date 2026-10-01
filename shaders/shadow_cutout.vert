#version 450

// shadow_depth.vert plus the texture coordinate, for rigged submeshes whose
// textures are see-through -- shadow_cutout.frag drops their transparent texels
// so hair cards and the eyes' cornea shells don't cast solid shadows.
layout(location=0) in vec3 inPos;
layout(location=2) in vec2 inTexCoord;

layout(location=3) in vec4 iM0;
layout(location=4) in vec4 iM1;
layout(location=5) in vec4 iM2;
layout(location=6) in vec4 iM3;

layout(location=0) out vec2 vTexCoord;

layout(push_constant) uniform ShadowPush
{
    mat4 lightViewProj;
} S;

void main()
{
    mat4 model = mat4(iM0, iM1, iM2, iM3);
    vTexCoord = inTexCoord;
    gl_Position = S.lightViewProj * model * vec4(inPos, 1.0);
}
