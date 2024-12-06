#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoords;

out vec3 Normal;
out vec2 TexCoords;
out vec3 FragPos;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;
uniform mat4 u_normal;

void main()
{
    TexCoords = aTexCoords;
    gl_Position = u_projection * u_view * u_model * vec4(aPos, 1.0);
    FragPos = vec3(u_model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(u_normal))) * aNormal;
}