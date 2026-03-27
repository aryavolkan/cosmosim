#version 330 core
in float v_mass;
in float v_type;
in vec3 v_velocity;
in float v_view_depth;
in float v_temperature;
in vec3 v_world_pos;

uniform vec3 u_smbh_pos;
uniform float u_smbh_luminosity;
uniform vec3 u_camera_pos;  // world-space camera position for Doppler

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
        // SMBH: dim placeholder — photon ring + EH rendered in composite pass
        color = vec3(0.3, 0.3, 0.4);
        intensity = exp(-r_sq * 20.0) * 0.3;
    } else if (body_type == 3) {
        // JET: bright blue-white beams with knot/blob modulation
        float core = exp(-r_sq * 12.0);
        vec3 core_color = vec3(3.0, 3.5, 5.0);
        vec3 edge_color = vec3(0.5, 1.2, 2.0);
        color = mix(edge_color, core_color, core);

        // Speed-based knot brightness: fast particles = bright knot cores
        float v_speed = length(v_velocity);
        float knot_boost = clamp((v_speed - 12.0) / 10.0, 0.0, 1.0);
        color *= (1.0 + knot_boost * 2.0);
        // Mass-based blob boost: burst blobs (heavier mass) appear as bright condensations
        float mass_boost = clamp((v_mass - 0.6) / 1.2, 0.0, 1.0);
        color *= (1.0 + mass_boost * 1.8);
        // Intensity boost so blobs are visibly larger
        intensity *= (1.0 + (knot_boost * 0.5 + mass_boost * 0.3));

    } else if (body_type == 1) {
        // GAS: temperature-based + shock front visualization
        float t = clamp(log(v_mass + 1.0) / 5.0, 0.0, 1.0);
        vec3 cool_gas = vec3(1.0, 0.4, 0.1);
        vec3 hot_gas  = vec3(1.5, 1.3, 1.2);
        float brightness_boost = u_smbh_luminosity * 0.01;
        color = mix(cool_gas, hot_gas, t) * (1.0 + brightness_boost);
        // Shock front: high-velocity gas glows blue-white
        float v_speed = length(v_velocity);
        float shock = clamp((v_speed - 6.0) / 14.0, 0.0, 1.0);
        color = mix(color, vec3(0.8, 1.4, 2.5), shock * 0.55);

    } else if (body_type == 4) {
        // DUST: temperature gradient cold → warm → hot near SMBH
        vec3 cold_dust = vec3(0.65, 0.22, 0.05);
        vec3 warm_dust = vec3(1.1,  0.55, 0.08);
        vec3 hot_dust  = vec3(1.8,  1.4,  2.6);
        if (v_temperature < 0.5) {
            color = mix(cold_dust, warm_dust, v_temperature * 2.0);
        } else {
            color = mix(warm_dust, hot_dust, (v_temperature - 0.5) * 2.0);
        }
        intensity = exp(-r_sq * 7.0) * 0.35;

    } else {
        // STAR: blue-orange mass gradient
        float t = clamp(log(v_mass + 1.0) / 5.0, 0.0, 1.0);
        color = mix(vec3(0.6, 0.7, 1.0), vec3(1.0, 0.8, 0.3), t);
    }

    // ── Relativistic Doppler shift + beaming intensity ────────────────────
    // Approaching particles are blue-shifted AND brighter (I ∝ D^3 where D = 1/(γ(1-β·cosθ)))
    // Receding particles are red-shifted AND dimmer.
    float v_spd = length(v_velocity);
    if (v_spd > 0.5) {
        float beta     = clamp(v_spd / 40.0, 0.0, 0.92);
        vec3  to_cam   = normalize(u_camera_pos - v_world_pos);
        float cos_theta = dot(v_velocity / v_spd, to_cam); // +1 = approaching

        // Relativistic beaming: D = 1 / (gamma * (1 - beta * cos_theta))
        // For jets (body_type 3), apply full D^3 intensity scaling
        // For other particles, apply mild D^1 scaling (just color shift)
        float D = 1.0 / (sqrt(1.0 - beta * beta) * (1.0 - beta * cos_theta));
        D = clamp(D, 0.1, 12.0);

        if (body_type == 3) {
            // Full relativistic beaming for jets: I ∝ D^3
            float beam = D * D * D;
            beam = clamp(beam, 0.02, 10.0);
            intensity *= beam;
            color *= beam * 0.3 + 0.7; // partial color boost to avoid total whiteout
        }

        // Color shift for all particle types
        float shift = cos_theta * beta;
        if (shift > 0.025) {
            vec3 blue_mod = vec3(0.62, 0.80, 1.48);
            color = mix(color, color * blue_mod, clamp(shift * 1.6, 0.0, 0.82));
        } else if (shift < -0.025) {
            vec3 red_mod = vec3(1.48, 0.58, 0.20);
            color = mix(color, color * red_mod, clamp(-shift * 1.3, 0.0, 0.75));
        }
    }

    frag_color = vec4(color * intensity, intensity * 0.8);
}
