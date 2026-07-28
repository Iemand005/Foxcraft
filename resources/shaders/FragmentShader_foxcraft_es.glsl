#version 300 es
precision mediump float;
out vec4 FragColor;

in vec3 Normal;
in vec3 FragPos;
in vec2 TexCoord;

uniform sampler2DArray ourTexture;

void main()
{
    FragColor = texture(ourTexture, vec3(TexCoord, 0.0));
}
