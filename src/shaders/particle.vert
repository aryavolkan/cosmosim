#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in float a_mass;

uniform mat4 u_view;
uniform mat4 u_projection;

out float v_mass;

void main()
{
    vec4 view_pos = u_view * vec4(a_position, 1.0);
    gl_Position = u_projection * view_pos;
    v_mass = a_mass;

    // Perspective-attenuated point size
    float base_size = clamp(1.0 + log(a_mass + 1.0) * 2.0, 1.0, 20.0);
    gl_PointSize = base_size * 300.0 / (-view_pos.z);
    gl_PointSize = clamp(gl_PointSize, 1.0, 40.0);
}
