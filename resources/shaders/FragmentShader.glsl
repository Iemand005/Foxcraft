#version 330 core
out vec4 FragColor;

in vec3 Normal;
in vec3 FragPos;
in vec3 TexCoord;

uniform sampler2DArray ourTexture; 

void main()
{
    vec3 n = normalize(Normal);

    vec3 lighting = vec3(1.0); 

    vec4 texSample = texture(ourTexture, TexCoord);
    
    if(texSample.a < 0.1) {
        discard;
    }

    FragColor = vec4(texSample.rgb * lighting, texSample.a);
}
