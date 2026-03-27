#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in float a_mass;
layout(location = 2) in vec3 a_velocity;
layout(location = 3) in float a_type;

uniform mat4 u_view;
uniform mat4 u_projection;

out float v_mass;
out float v_type;
out vec3 v_velocity;
out float v_view_depth;

void main()
{
    vec4 view_pos = u_view * vec4(a_position, 1.0);
    gl_Position = u_projection * view_pos;
    v_mass = a_mass;
    v_type = a_type;
    v_velocity = a_velocity;
    v_view_depth = -view_pos.z;

    float base_size;
    int body_type = int(a_type + 0.5);

    if (body_type == 2) {
        // SMBH: large bright point
        base_size = 30.0;
    } else if (body_type == 3) {
        // JET: elongated, medium size
        base_size = clamp(3.0 + length(a_velocity) * 0.1, 3.0, 15.0);
    } else {
        // STAR/GAS: same as before
        base_size = clamp(1.0 + log(a_mass + 1.0) * 2.0, 1.0, 20.0);
    }

    gl_PointSize = base_size * 300.0 / (-view_pos.z);
    gl_PointSize = clamp(gl_PointSize, 1.0, 60.0);
}
