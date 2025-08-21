#version 450

// Vertex attributes
layout(location=0) in vec3 inPos;
layout(location=1) in vec3 inNrm;
layout(location=2) in vec2 inUV;

// Instance attributes (binding 1): model matrix columns + color + shape type
layout(location=3) in vec4 iM0;
layout(location=4) in vec4 iM1;
layout(location=5) in vec4 iM2;
layout(location=6) in vec4 iM3;
layout(location=7) in vec3 iColor;
layout(location=8) in float iShapeType;  // 9 for rigged meshes

// Uniform buffer
layout(binding = 0) uniform UBO 
{    
    mat4 model;
    mat4 uView;
    mat4 uProj;
} U;

// Output to fragment shader
layout(location=0) out vec3 vNrmWS;
layout(location=1) out vec3 vColor;
layout(location=2) out vec3 vPosWS;
layout(location=3) out float vShapeType;
layout(location=4) out vec2 vTexCoord;

void main() 
{
    // Build instance model matrix
    mat4 instanceModel = mat4(iM0, iM1, iM2, iM3);
    mat4 model = instanceModel * U.model;
    
    // Transform position to world space
    vec4 posWS = model * vec4(inPos, 1.0);
    vPosWS = posWS.xyz;
    
    // Transform normal to world space
    vec3 nrmWS = mat3(transpose(inverse(model))) * inNrm;
    vNrmWS = normalize(nrmWS);
    
    vColor = iColor;
    vShapeType = iShapeType;
    vTexCoord = inUV;

    gl_Position = U.uProj * U.uView * posWS;
}
