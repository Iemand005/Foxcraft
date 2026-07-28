#version 300 es
precision mediump float;
precision highp sampler2DArray;
out vec4 FragColor;

in vec3 Normal;
in vec3 FragPos;
in vec3 TexCoord;

uniform sampler2DArray ourTexture;

void main()
{
    FragColor = texture(ourTexture, TexCoord);
}
