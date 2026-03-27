#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in float a_mass;
layout(location = 2) in vec3 a_velocity;
layout(location = 3) in float a_type;

uniform mat4 u_view;
uniform mat4 u_projection;

// SMBH world-space positions for per-particle temperature (dust/gas heating)
uniform int u_smbh_count;
uniform vec3 u_smbh_world_pos[2];

out float v_mass;
out float v_type;
out vec3 v_velocity;
out float v_view_depth;
out float v_temperature;  // 0 = cold, 1 = near SMBH
out vec3 v_world_pos;     // world-space position for Doppler computation

void main()
{
    vec4 view_pos = u_view * vec4(a_position, 1.0);
    gl_Position = u_projection * view_pos;
    v_mass = a_mass;
    v_type = a_type;
    v_velocity = a_velocity;
    v_view_depth = -view_pos.z;
    v_world_pos = a_position;

    // Temperature: proximity to nearest SMBH in world space
    float min_dist = 1e10;
    for (int i = 0; i < u_smbh_count && i < 2; i++) {
        vec3 d = a_position - u_smbh_world_pos[i];
        float dist = length(d);
        if (dist < min_dist) min_dist = dist;
    }
    // 0 at dist >= 12 world units, 1 at dist <= 0
    v_temperature = clamp(1.0 - min_dist / 12.0, 0.0, 1.0);

    float base_size;
    int body_type = int(a_type + 0.5);

    if (body_type == 2) {
        // SMBH: large bright point
        base_size = 30.0;
    } else if (body_type == 3) {
        // JET: bright, visible streaks — size varies with speed (knot density effect)
        base_size = clamp(5.0 + length(a_velocity) * 0.3, 5.0, 28.0);
    } else if (body_type == 4) {
        // DUST: slightly diffuse, moderate size
        base_size = clamp(2.0 + log(a_mass + 1.0) * 1.5, 2.0, 12.0);
    } else if (body_type == 5) {
        // LOBE: diffuse hotspot/cocoon
        base_size = clamp(8.0 + log(a_mass + 1.0) * 3.0, 8.0, 30.0);
    } else {
        // STAR/GAS: mass-based
        base_size = clamp(1.0 + log(a_mass + 1.0) * 2.0, 1.0, 20.0);
    }

    gl_PointSize = base_size * 300.0 / (-view_pos.z);
    gl_PointSize = clamp(gl_PointSize, 1.0, 60.0);
}
