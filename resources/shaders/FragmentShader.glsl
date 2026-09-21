#version 330 core
out vec4 FragColor;

in vec3 Normal;
in vec3 FragPos;
in vec3 TexCoord;
in float BlockLight;

uniform sampler2DArray ourTexture; 

const float glowStrength = 1.6;

void main()
{
    vec4 texColor = texture(ourTexture, TexCoord);

    // Keep the basic flat (unlit) look, then add the emissive block-light
    // glow on top. Light values are baked per vertex, so smooth lighting
    // interpolates across faces while flat lighting is constant per face.
    float glow = max(0.0, BlockLight - 0.5) * glowStrength;
    float brightness = 1.0 + glow;

    FragColor = vec4(texColor.rgb * brightness, texColor.a);
}
