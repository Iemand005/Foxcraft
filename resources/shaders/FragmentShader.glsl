#version 330 core
out vec4 FragColor;

in vec3 Normal;
in vec3 FragPos;
in vec3 TexCoord;

uniform sampler2DArray ourTexture; 

void main()
{
    FragColor = texture(ourTexture, TexCoord);
}
