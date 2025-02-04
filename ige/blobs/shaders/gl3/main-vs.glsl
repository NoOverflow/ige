#version 330 core

uniform mat4 u_ProjViewModel;
uniform mat4 u_NormalMatrix;

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoords;

out vec3 v_Normal;
out vec2 v_TexCoords;

void main()
{
    v_Normal = (u_NormalMatrix * vec4(a_Normal, 1.0)).xyz;
    v_TexCoords = a_TexCoords;

    gl_Position = u_ProjViewModel * vec4(a_Position, 1.0);
}
