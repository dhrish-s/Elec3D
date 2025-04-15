#version 330 core
out vec4 FragColor;

uniform vec3 objectColor;
uniform float brightness;

void main()
{
    FragColor = vec4(objectColor * brightness, 1.0);
}
