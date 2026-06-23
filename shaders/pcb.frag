#version 330 core
in vec2 vUv;

uniform vec3 uBoardColor;
uniform vec3 uCopperColor;

out vec4 fragColor;

const float COPPER_GRID_SCALE = 20.0;
const float GRID_LINE_THRESHOLD = 0.97;
const float COPPER_GRID_STRENGTH = 0.3;

void main() {
    float gridX = step(GRID_LINE_THRESHOLD, fract(vUv.x * COPPER_GRID_SCALE));
    float gridY = step(GRID_LINE_THRESHOLD, fract(vUv.y * COPPER_GRID_SCALE));
    float copperAmount = clamp((gridX + gridY) * COPPER_GRID_STRENGTH, 0.0, 1.0);
    vec3 color = mix(uBoardColor, uCopperColor, copperAmount);
    fragColor = vec4(color, 1.0);
}
