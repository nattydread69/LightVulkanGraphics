#version 450
layout(location=0) in vec3 vNrmWS;
layout(location=1) in vec3 vColor;
layout(location=0) out vec4 outColor;

void main() {
    vec3 N = normalize(vNrmWS);
    vec3 L = normalize(vec3(0.4, 0.7, 0.2));
    float diff = max(dot(N, L), 0.0);
    vec3 base = vColor;
    vec3 c = base * (0.25 + 0.75 * diff); // small ambient so nothing is black
    outColor = vec4(c, 1.0);
}

