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
layout(location=8) in float iShapeType;  // 0=sphere, 1=cube, 2=cone, 3=cylinder, 4=capsule, 5=arrow, 6=line

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
    
    // Transform normal to world space (using normal matrix)
    vec3 nrmWS = mat3(transpose(inverse(model))) * inNrm;
    vNrmWS = normalize(nrmWS);
    
    // Pass through color and shape type
    vColor = iColor;
    vShapeType = iShapeType;
    
    // Prefer author-provided UVs when available
    bool hasCustomUV = any(notEqual(inUV, vec2(0.0)));
    if (hasCustomUV)
    {
        vTexCoord = inUV;
    }
    else if (iShapeType < 0.5) // Sphere
    {
        // Spherical coordinates for sphere
        vec3 n = normalize(inPos);
        vTexCoord = vec2(atan(n.z, n.x) / (2.0 * 3.14159) + 0.5, asin(n.y) / 3.14159 + 0.5);
    }
    else if (iShapeType < 1.5) // Cube/Hexahedral
    {
        // Planar mapping for cube faces
        vec3 absPos = abs(inPos);
        if (absPos.x > absPos.y && absPos.x > absPos.z)
            vTexCoord = inPos.yz * 0.5 + 0.5;
        else if (absPos.y > absPos.z)
            vTexCoord = inPos.xz * 0.5 + 0.5;
        else
            vTexCoord = inPos.xy * 0.5 + 0.5;
    }
    else if (iShapeType < 2.5) // Cone
    {
        // Cylindrical mapping for cone
        float theta = atan(inPos.z, inPos.x) / (2.0 * 3.14159) + 0.5;
        float height = inPos.y * 0.5 + 0.5;
        vTexCoord = vec2(theta, height);
    }
    else if (iShapeType < 3.5) // Cylinder
    {
        // Cylindrical mapping
        float theta = atan(inPos.z, inPos.x) / (2.0 * 3.14159) + 0.5;
        float height = inPos.y * 0.5 + 0.5;
        vTexCoord = vec2(theta, height);
    }
    else if (iShapeType < 4.5) // Capsule
    {
        // Spherical mapping for capsule
        vec3 n = normalize(inPos);
        vTexCoord = vec2(atan(n.z, n.x) / (2.0 * 3.14159) + 0.5, asin(n.y) / 3.14159 + 0.5);
    }
    else if (iShapeType < 5.5) // Arrow
    {
        // Cylindrical mapping for arrow
        float theta = atan(inPos.z, inPos.x) / (2.0 * 3.14159) + 0.5;
        float height = inPos.y * 0.5 + 0.5;
        vTexCoord = vec2(theta, height);
    }
    else // Line
    {
        // Simple linear mapping for lines
        vTexCoord = vec2(inPos.x * 0.5 + 0.5, inPos.y * 0.5 + 0.5);
    }
    
    // Final position
    gl_Position = U.uProj * U.uView * posWS;
}
