#version 450

// Input from vertex shader
layout(location=0) in vec3 vNrmWS;
layout(location=1) in vec3 vColor;
layout(location=2) in vec3 vPosWS;
layout(location=3) in float vShapeType;
layout(location=4) in vec2 vTexCoord;

// Output
layout(location=0) out vec4 outColor;

const float specularPower = 32.0;
const int LVG_MAX_LIGHTS = 16;
const int LVG_LIGHT_DIRECTIONAL = 0;
const int LVG_LIGHT_POINT = 1;
const int LVG_LIGHT_SPOT = 2;

layout(set = 0, binding = 0) uniform UBO
{
    mat4 model;
    mat4 uView;
    mat4 uProj;
} U;

struct ShaderLight
{
    vec4 positionRange;
    vec4 directionType;
    vec4 colorIntensity;
    vec4 spotAngles;
};

layout(set = 0, binding = 1) uniform Lighting
{
    vec4 ambientAndCount;
    ShaderLight lights[LVG_MAX_LIGHTS];
} lighting;

// Shape-specific parameters
const float sphereRoughness = 0.3;
const float cubeRoughness = 0.8;
const float coneRoughness = 0.6;
const float cylinderRoughness = 0.7;
const float capsuleRoughness = 0.4;
const float arrowRoughness = 0.5;
const float lineRoughness = 1.0;

float distanceAttenuation(float distanceToLight, float range)
{
    float inverseSquare = 1.0 / max(distanceToLight * distanceToLight, 1.0);
    if (range <= 0.0)
    {
        return inverseSquare;
    }

    float normalizedDistance = clamp(1.0 - distanceToLight / range, 0.0, 1.0);
    return inverseSquare * normalizedDistance * normalizedDistance;
}

float spotAttenuation(ShaderLight light, vec3 lightToFragment)
{
    float cosTheta = dot(normalize(light.directionType.xyz), normalize(lightToFragment));
    float innerCos = light.spotAngles.x;
    float outerCos = light.spotAngles.y;
    return clamp((cosTheta - outerCos) / max(innerCos - outerCos, 0.001), 0.0, 1.0);
}

vec3 calculateLighting(vec3 normal, vec3 viewDir, vec3 baseColor, float roughness)
{
    normal = normalize(normal);
    viewDir = normalize(viewDir);

    vec3 result = lighting.ambientAndCount.rgb * baseColor;
    int lightCount = min(int(lighting.ambientAndCount.w + 0.5), LVG_MAX_LIGHTS);

    for (int i = 0; i < lightCount; ++i)
    {
        ShaderLight light = lighting.lights[i];
        int type = int(light.directionType.w + 0.5);
        float intensity = light.colorIntensity.a;
        if (intensity <= 0.0)
        {
            continue;
        }

        vec3 lightDir;
        float attenuation = 1.0;
        if (type == LVG_LIGHT_DIRECTIONAL)
        {
            lightDir = normalize(-light.directionType.xyz);
        }
        else
        {
            vec3 toLight = light.positionRange.xyz - vPosWS;
            float distanceToLight = length(toLight);
            if (distanceToLight <= 0.0001)
            {
                continue;
            }
            lightDir = toLight / distanceToLight;
            attenuation = distanceAttenuation(distanceToLight, light.positionRange.w);
            if (type == LVG_LIGHT_SPOT)
            {
                attenuation *= spotAttenuation(light, -lightDir);
            }
        }

        float NdotL = max(dot(normal, lightDir), 0.0);
        vec3 radiance = light.colorIntensity.rgb * intensity * attenuation;
        vec3 diffuse = radiance * baseColor * NdotL;

        vec3 halfDir = normalize(lightDir + viewDir);
        float NdotH = max(dot(normal, halfDir), 0.0);
        float shininess = mix(4.0, specularPower, 1.0 - roughness);
        float specular = pow(NdotH, shininess);
        vec3 specularColor = radiance * specular * (1.0 - roughness);

        result += diffuse + specularColor;
    }

    return result;
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
    vec3 cameraPosWS = vec3(inverse(U.uView)[3]);
    vec3 viewDir = normalize(cameraPosWS - vPosWS);
    
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
