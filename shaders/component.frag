#version 330 core
in vec3 vNormal;
in vec3 vFragPos;

uniform vec3 uColor;
uniform vec3 uLightDir;
uniform vec3 uViewPos;

out vec4 fragColor;

const float AMBIENT_STRENGTH = 0.15;
const float SPECULAR_STRENGTH = 0.4;
const float SHININESS = 32.0;

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightDir);
    vec3 V = normalize(uViewPos - vFragPos);
    vec3 H = normalize(L + V);

    vec3 ambient = AMBIENT_STRENGTH * uColor;
    vec3 diffuse = max(dot(N, L), 0.0) * uColor;
    vec3 specular = pow(max(dot(N, H), 0.0), SHININESS) * vec3(SPECULAR_STRENGTH);

    fragColor = vec4(ambient + diffuse + specular, 1.0);
}
