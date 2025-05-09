#version 410 core
layout (location = 0) in vec3 aPos;

out vec3 TexCoords;

uniform mat4 u_projection;
uniform mat4 u_view;
uniform mat4 u_model;

void main()
{
    TexCoords = aPos.xzy;
    vec4 pos = u_projection * u_view * u_model * vec4(aPos, 1.0);
    gl_Position = pos;
}  