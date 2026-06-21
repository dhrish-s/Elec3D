#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vNormal;
out vec3 vFragPos;

void main() {
    vFragPos = vec3(uModel * vec4(aPos, 1.0));
    // Inverse-transpose handles rotation correctly even though most model
    // matrices here are translation-only — components do rotate per frame.
    mat3 normalMatrix = mat3(transpose(inverse(uModel)));
    vNormal = normalize(normalMatrix * aNormal);
    gl_Position = uProjection * uView * vec4(vFragPos, 1.0);
}
