#version 450

layout (location = 0) out vec4 FragColor;

layout (location = 2) in vec3 TexCoord;

layout(binding = 0) uniform UBO {
    mat4 view;
    mat4 projection;
};

layout(binding = 1) uniform sampler2DArray ourTexture;

void main()
{
    FragColor = texture(ourTexture, TexCoord);
}
