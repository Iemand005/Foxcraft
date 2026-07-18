#version 450

layout (location = 0) in ivec3 aPos;
layout (location = 1) in uvec4 aMeta;

layout (location = 0) out vec3 Normal;
layout (location = 1) out vec3 FragPos;
layout (location = 2) out vec3 TexCoord;

layout(push_constant) uniform PushConstants {
    mat4 model;
} pc;

layout(binding = 0) uniform UBO {
    mat4 view;
    mat4 projection;
};

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
    vec3 aPos = vec3(aPos);
    vec3 aNormal = decodeNormal(aMeta.x);
    vec3 aTexCoord = vec3(float(aMeta.y), float(aMeta.z), float(aMeta.w));

    vec4 worldPos = pc.model * vec4(aPos, 1.0);
    gl_Position = projection * view * worldPos;

    mat3 normalMat = transpose(inverse(mat3(pc.model)));
    Normal = normalMat * aNormal;
    FragPos = worldPos.xyz;
    TexCoord = aTexCoord;
}
