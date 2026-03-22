#version 330 core
layout(location = 0) in vec2 a_position;
layout(location = 1) in float a_mass;

uniform mat4 u_projection;

out float v_mass;

void main()
{
    gl_Position = u_projection * vec4(a_position, 0.0, 1.0);
    v_mass = a_mass;
    gl_PointSize = clamp(1.0 + log(a_mass + 1.0) * 2.0, 1.0, 20.0);
}
