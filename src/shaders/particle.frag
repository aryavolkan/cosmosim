#version 330 core
in float v_mass;
in float v_type;
in vec3 v_velocity;
in float v_view_depth;

uniform vec3 u_smbh_pos;
uniform float u_smbh_luminosity;

out vec4 frag_color;

void main()
{
    vec2 coord = gl_PointCoord - vec2(0.5);
    float r_sq = dot(coord, coord);

    if (r_sq > 0.25) discard;

    float intensity = exp(-r_sq * 16.0);
    int body_type = int(v_type + 0.5);
    vec3 color;

    if (body_type == 2) {
        // SMBH: dim point — event horizon + photon ring rendered in composite pass
        color = vec3(0.3, 0.3, 0.4);
        intensity = exp(-r_sq * 20.0) * 0.3;
    } else if (body_type == 3) {
        // JET: bright blue-white beams
        float core = exp(-r_sq * 12.0);
        vec3 core_color = vec3(3.0, 3.5, 5.0);
        vec3 edge_color = vec3(0.5, 1.2, 2.0);
        color = mix(edge_color, core_color, core);
    } else if (body_type == 1) {
        // GAS: temperature-based
        float t = clamp(log(v_mass + 1.0) / 5.0, 0.0, 1.0);
        vec3 cool_gas = vec3(1.0, 0.4, 0.1);
        vec3 hot_gas = vec3(1.5, 1.3, 1.2);
        float brightness_boost = u_smbh_luminosity * 0.01;
        color = mix(cool_gas, hot_gas, t) * (1.0 + brightness_boost);
    } else {
        // STAR: original gradient
        float t = clamp(log(v_mass + 1.0) / 5.0, 0.0, 1.0);
        color = mix(vec3(0.6, 0.7, 1.0), vec3(1.0, 0.8, 0.3), t);
    }

    frag_color = vec4(color * intensity, intensity * 0.8);
}
