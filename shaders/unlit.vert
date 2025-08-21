#version 450

// Vertex attributes
layout(location=0) in vec3 inPos;
layout(location=1) in vec3 inNrm;

// Instance attributes (binding 1): model matrix columns + color
layout(location=2) in vec4 iM0;
layout(location=3) in vec4 iM1;
layout(location=4) in vec4 iM2;
layout(location=5) in vec4 iM3;
layout(location=6) in vec3 iColor;

// Uniform buffer
layout(binding = 0) uniform UBO 
{    
    mat4 model;
    mat4 uView;
    mat4 uProj;
} U;

// Output to fragment shader
layout(location=0) out vec3 vColor;

void main() 
{
    // Build instance model matrix
    mat4 instanceModel = mat4(iM0, iM1, iM2, iM3);
    mat4 model = instanceModel * U.model;
    
    // Transform position to world space
    vec4 posWS = model * vec4(inPos, 1.0);
    
    // Pass through color
    vColor = iColor;
    
    // Final position
    gl_Position = U.uProj * U.uView * posWS;
}


