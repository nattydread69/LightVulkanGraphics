#version 450

layout(location=0) in vec2 vTexCoord;

layout(set = 0, binding = 0) uniform sampler2D textureSampler;

void main()
{
    // Same cutout as rigged_mesh.frag's solid core pass (alpha >= 0.5).
    if (texture(textureSampler, vTexCoord).a < 0.5)
    {
        discard;
    }
}
