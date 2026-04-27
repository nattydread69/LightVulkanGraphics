#version 450

layout(location=0) in vec3 inPos;

layout(location=3) in vec4 iM0;
layout(location=4) in vec4 iM1;
layout(location=5) in vec4 iM2;
layout(location=6) in vec4 iM3;

layout(push_constant) uniform ShadowPush
{
    mat4 lightViewProj;
} S;

void main()
{
    mat4 model = mat4(iM0, iM1, iM2, iM3);
    gl_Position = S.lightViewProj * model * vec4(inPos, 1.0);
}
