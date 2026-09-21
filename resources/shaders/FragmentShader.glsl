#version 330 core
out vec4 FragColor;

in vec3 Normal;
in vec3 FragPos;
in vec3 TexCoord;
in float BlockLight;

uniform sampler2DArray ourTexture; 

// The base keeps the flat look but is dimmed slightly so the emissive glow
// reads as a light filling in; the smoothstep removes the hard lit/unlit
// step, giving a soft falloff from the glowing block.
const float baseBrightness = 0.75;
const float glowStrength = 1.0;

void main()
{
    vec4 texColor = texture(ourTexture, TexCoord);

    float glow = smoothstep(0.2, 1.0, BlockLight) * glowStrength;
    float brightness = baseBrightness + glow;

    FragColor = vec4(texColor.rgb * brightness, texColor.a);
}
