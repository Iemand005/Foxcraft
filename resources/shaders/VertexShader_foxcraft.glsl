#version 330 core
layout (location = 0) in ivec3 aPos;
layout (location = 1) in uint aNormalLayer;

out vec3 Normal;
out vec3 FragPos;
out vec3 TexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

vec3 decodeNormal(uint idx) {
    vec3 dirs[6] = vec3[](
        vec3(1,0,0), vec3(-1,0,0),
        vec3(0,1,0), vec3(0,-1,0),
        vec3(0,0,1), vec3(0,0,-1)
    );
    return dirs[idx];
}

void main()
{
    vec3 pos = vec3(aPos);
    uint face = aNormalLayer & 0x7u;
    uint layer = aNormalLayer >> 3;
    vec3 aNormal = decodeNormal(face);

    vec2 uv;
    if (face < 2) uv = pos.zy;
    else if (face < 4) uv = pos.xz;
    else uv = pos.xy;

    vec4 worldPos = model * vec4(pos, 1.0);
    gl_Position = projection * view * worldPos;

    mat3 normalMat = transpose(inverse(mat3(model)));
    Normal = normalMat * aNormal;
    FragPos = worldPos.xyz;
    TexCoord = vec3(uv, float(layer));
}
