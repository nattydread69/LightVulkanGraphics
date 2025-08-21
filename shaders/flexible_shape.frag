#version 450

// Input from vertex shader
layout(location=0) in vec3 vNrmWS;
layout(location=1) in vec3 vColor;
layout(location=2) in vec3 vPosWS;
layout(location=3) in float vShapeType;
layout(location=4) in vec2 vTexCoord;

// Output
layout(location=0) out vec4 outColor;

// Lighting parameters
const vec3 lightDir = normalize(vec3(0.4, 0.7, 0.2));
const vec3 lightColor = vec3(1.0, 1.0, 1.0);
const vec3 ambientColor = vec3(0.1, 0.1, 0.15);
const float specularPower = 32.0;

// Shape-specific parameters
const float sphereRoughness = 0.3;
const float cubeRoughness = 0.8;
const float coneRoughness = 0.6;
const float cylinderRoughness = 0.7;
const float capsuleRoughness = 0.4;
const float arrowRoughness = 0.5;
const float lineRoughness = 1.0;

vec3 calculateLighting(vec3 normal, vec3 viewDir, vec3 baseColor, float roughness)
{
    // Normalize inputs
    normal = normalize(normal);
    viewDir = normalize(viewDir);
    
    // Ambient
    vec3 ambient = ambientColor * baseColor;
    
    // Diffuse
    float NdotL = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = lightColor * baseColor * NdotL;
    
    // Specular (Blinn-Phong)
    vec3 halfDir = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfDir), 0.0);
    float specular = pow(NdotH, specularPower * (1.0 - roughness));
    vec3 specularColor = lightColor * specular * (1.0 - roughness);
    
    return ambient + diffuse + specularColor;
}

vec3 getShapeSpecificColor(vec3 baseColor, vec2 texCoord, float shapeType)
{
    vec3 color = baseColor;
    
    // Add shape-specific visual effects
    if (shapeType < 0.5) // Sphere
    {
        // Add subtle checkerboard pattern
        vec2 grid = floor(texCoord * 8.0);
        float checker = mod(grid.x + grid.y, 2.0);
        color = mix(color, color * 0.8, checker * 0.3);
    }
    else if (shapeType < 1.5) // Cube/Hexahedral
    {
        // Add edge highlighting
        vec2 edge = abs(fract(texCoord * 4.0) - 0.5);
        float edgeFactor = smoothstep(0.0, 0.1, min(edge.x, edge.y));
        color = mix(color * 1.2, color, edgeFactor);
    }
    else if (shapeType < 2.5) // Cone
    {
        // Add radial gradient
        float radial = length(texCoord - 0.5);
        color = mix(color * 1.1, color, radial);
    }
    else if (shapeType < 3.5) // Cylinder
    {
        // Add vertical stripes
        float stripe = sin(texCoord.x * 3.14159 * 4.0) * 0.1 + 0.9;
        color *= stripe;
    }
    else if (shapeType < 4.5) // Capsule
    {
        // Add subtle gradient
        float gradient = texCoord.y;
        color = mix(color * 0.8, color, gradient);
    }
    else if (shapeType < 5.5) // Arrow
    {
        // Add directional gradient
        float gradient = texCoord.y;
        color = mix(color * 0.9, color * 1.1, gradient);
    }
    else // Line
    {
        // Keep lines simple
        color = baseColor;
    }
    
    return color;
}

void main() 
{
    // Calculate view direction
    vec3 viewDir = -normalize(vPosWS);
    
    // Get shape-specific color
    vec3 baseColor = getShapeSpecificColor(vColor, vTexCoord, vShapeType);
    
    // Get shape-specific roughness
    float roughness;
    if (vShapeType < 0.5) roughness = sphereRoughness;
    else if (vShapeType < 1.5) roughness = cubeRoughness;
    else if (vShapeType < 2.5) roughness = coneRoughness;
    else if (vShapeType < 3.5) roughness = cylinderRoughness;
    else if (vShapeType < 4.5) roughness = capsuleRoughness;
    else if (vShapeType < 5.5) roughness = arrowRoughness;
    else roughness = lineRoughness;
    
    // Calculate lighting
    vec3 finalColor = calculateLighting(vNrmWS, viewDir, baseColor, roughness);
    
    // Special handling for lines (make them brighter)
    if (vShapeType > 5.5)
    {
        finalColor = baseColor * 2.0; // Make lines very bright
    }
    
    outColor = vec4(finalColor, 1.0);
}
