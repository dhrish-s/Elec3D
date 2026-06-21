#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in mat4 aInstanceModel;   // consumes locations 2,3,4,5
layout(location = 6) in vec3 aInstanceColor;

uniform mat4 uModel;          // used only when uUseInstancing == false
uniform mat4 uView;
uniform mat4 uProjection;
uniform vec3 uColor;          // used only when uUseInstancing == false
uniform bool uUseInstancing;

out vec3 vNormal;
out vec3 vFragPos;
out vec3 vColor;

void main() {
    mat4 model = uUseInstancing ? aInstanceModel : uModel;
    vColor = uUseInstancing ? aInstanceColor : uColor;
    vFragPos = vec3(model * vec4(aPos, 1.0));
    // Inverse-transpose handles rotation correctly even though most model
    // matrices here are translation-only — components do rotate per frame.
    mat3 normalMatrix = mat3(transpose(inverse(model)));
    vNormal = normalize(normalMatrix * aNormal);
    gl_Position = uProjection * uView * vec4(vFragPos, 1.0);
}
