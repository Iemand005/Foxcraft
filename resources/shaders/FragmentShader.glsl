#version 330 core
out vec4 FragColor;

in vec3 Normal;
in vec3 FragPos;
in vec2 TexCoord;

uniform sampler2D ourTexture;

struct PointLight {
    vec3 position;
    vec3 color;
    float intensity;
    float radius;
};

uniform int lightCount;
uniform PointLight pointLights[8];

void main()
{
    vec3 n = normalize(Normal);

    vec3 lighting = vec3(1);


    vec4 texSample = texture(ourTexture, TexCoord);
    FragColor = vec4(texSample.rgb * lighting, texSample.a);
}
