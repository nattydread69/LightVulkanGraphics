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

// Lighting parameters
const vec3 keyLightColor = vec3(1.0, 1.0, 1.0);
const vec3 fillLightColor = vec3(0.6, 0.7, 0.9);
const vec3 ambientColor = vec3(0.22, 0.22, 0.28);
const float specularPower = 32.0;
const float diffuseWrap = 0.2;

vec3 calculateLighting(
    vec3 normal,
    vec3 viewDir,
    vec3 keyLightDir,
    vec3 fillLightDir,
    vec3 baseColor,
    float roughness)
{
    normal = normalize(normal);
    viewDir = normalize(viewDir);
    keyLightDir = normalize(keyLightDir);
    fillLightDir = normalize(fillLightDir);
    
    vec3 ambient = ambientColor * baseColor;
    
    float NdotL = max((dot(normal, keyLightDir) + diffuseWrap) / (1.0 + diffuseWrap), 0.0);
    float NdotFill = max((dot(normal, fillLightDir) + diffuseWrap) / (1.0 + diffuseWrap), 0.0);
    vec3 diffuse = keyLightColor * baseColor * NdotL;
    vec3 diffuseFill = fillLightColor * baseColor * NdotFill;
    
    vec3 halfDir = normalize(keyLightDir + viewDir);
    float NdotH = max(dot(normal, halfDir), 0.0);
    float specular = pow(NdotH, specularPower * (1.0 - roughness));
    vec3 specularColor = keyLightColor * specular * (1.0 - roughness);
    
    return ambient + diffuse + diffuseFill + specularColor;
}

void main() 
{
    vec3 cameraPosWS = vec3(inverse(U.uView)[3]);
    vec3 viewDir = normalize(cameraPosWS - vPosWS);

    // Keep the character lit from the viewer side with a slight overhead bias.
    vec3 keyLightDir = normalize(viewDir + vec3(0.0, 0.65, 0.0));
    vec3 fillLightDir = normalize(viewDir + vec3(-0.6, 0.2, 0.35));

    vec4 texSample = texture(textureSampler, vTexCoord);
    vec3 baseColor = texSample.rgb;
    float roughness = 0.4;
    
    vec3 finalColor = calculateLighting(
        vNrmWS,
        viewDir,
        keyLightDir,
        fillLightDir,
        baseColor,
        roughness);
    outColor = vec4(finalColor, texSample.a);
}
