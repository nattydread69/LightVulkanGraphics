#version 450

// Input from vertex shader
layout(location=0) in vec3 vNrmWS;
layout(location=1) in vec3 vColor;
layout(location=2) in vec3 vPosWS;
layout(location=3) in float vShapeType;
layout(location=4) in vec2 vTexCoord;

// Output
layout(location=0) out vec4 outColor;

layout(set = 0, binding = 0) uniform UBO
{
    mat4 model;
    mat4 uView;
    mat4 uProj;
} U;

layout(set = 1, binding = 0) uniform sampler2D textureSampler;

const float specularPower = 32.0;
const float diffuseWrap = 0.2;
const int LVG_MAX_LIGHTS = 16;
const int LVG_LIGHT_DIRECTIONAL = 0;
const int LVG_LIGHT_SPOT = 2;

struct ShaderLight
{
    vec4 positionRange;
    vec4 directionType;
    vec4 colorIntensity;
    vec4 spotAngles;
    vec4 shadowInfo;
};

layout(set = 0, binding = 1) uniform Lighting
{
    vec4 ambientAndCount;
    ShaderLight lights[LVG_MAX_LIGHTS];
    mat4 shadowMatrices[LVG_MAX_LIGHTS];
} lighting;

layout(set = 0, binding = 2) uniform sampler2DArrayShadow shadowMaps;

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

float sampleShadow(int lightIndex, vec3 normal)
{
    ShaderLight light = lighting.lights[lightIndex];
    float layer = light.shadowInfo.x;
    if (layer < 0.0)
    {
        return 1.0;
    }

    vec3 biasedPos = vPosWS + normalize(normal) * light.shadowInfo.z;
    vec4 shadowPos = lighting.shadowMatrices[lightIndex] * vec4(biasedPos, 1.0);
    if (shadowPos.w <= 0.0)
    {
        return 1.0;
    }

    vec3 proj = shadowPos.xyz / shadowPos.w;
    proj.xy = proj.xy * 0.5 + 0.5;
    if (proj.x < 0.0 || proj.x > 1.0 ||
        proj.y < 0.0 || proj.y > 1.0 ||
        proj.z < 0.0 || proj.z > 1.0)
    {
        return 1.0;
    }

    float visibility = texture(shadowMaps, vec4(proj.xy, layer, proj.z - light.shadowInfo.y));
    return mix(1.0, visibility, light.shadowInfo.w);
}

vec3 calculateLighting(
    vec3 normal,
    vec3 viewDir,
    vec3 baseColor,
    float roughness)
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

        float NdotL = max((dot(normal, lightDir) + diffuseWrap) / (1.0 + diffuseWrap), 0.0);
        vec3 radiance = light.colorIntensity.rgb * intensity * attenuation;
        vec3 diffuse = radiance * baseColor * NdotL;

        vec3 halfDir = normalize(lightDir + viewDir);
        float NdotH = max(dot(normal, halfDir), 0.0);
        float shininess = mix(4.0, specularPower, 1.0 - roughness);
        float specular = pow(NdotH, shininess);
        vec3 specularColor = radiance * specular * (1.0 - roughness);
        float shadow = sampleShadow(i, normal);

        result += (diffuse + specularColor) * shadow;
    }

    return result;
}

void main() 
{
    vec3 cameraPosWS = vec3(inverse(U.uView)[3]);
    vec3 viewDir = normalize(cameraPosWS - vPosWS);

    vec4 texSample = texture(textureSampler, vTexCoord);
    vec3 baseColor = texSample.rgb;
    float roughness = 0.4;
    
    vec3 finalColor = calculateLighting(
        vNrmWS,
        viewDir,
        baseColor,
        roughness);
    outColor = vec4(finalColor, texSample.a);
}
