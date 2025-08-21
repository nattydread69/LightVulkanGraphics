#version 450

// Input from vertex shader
layout(location=0) in vec3 vColor;
layout(location=1) in vec3 vPosWS;

// Output
layout(location=0) out vec4 outColor;

void main() 
{
    // Simple wireframe rendering - just use the color directly
    // You could add distance-based fading or other effects here
    outColor = vec4(vColor, 1.0);
}


