#version 330 core

in vec2 v_uv;
uniform sampler2D u_scene;
uniform sampler2D u_bloom;
uniform float u_bloom_intensity;
uniform vec2 u_smbh_screen;
uniform float u_smbh_mass;
uniform float u_lensing_strength;
uniform float u_exposure;
uniform float u_eh_radius;
uniform float u_aspect;

out vec4 frag_color;

void main()
{
    // Aspect-corrected distance from SMBH center
    vec2 delta = v_uv - u_smbh_screen;
    delta.x *= u_aspect;
    float dist = length(delta);

    // Gravitational lensing: stronger near the event horizon
    vec2 scene_uv = v_uv;
    if (dist > 0.001 && u_lensing_strength > 0.0) {
        float deflection = u_lensing_strength / (dist * dist + 0.0005);
        deflection = min(deflection, 0.15);
        vec2 dir = normalize(delta);
        // Undo aspect correction for UV offset
        dir.x /= u_aspect;
        scene_uv = v_uv + dir * deflection;
        scene_uv = clamp(scene_uv, vec2(0.0), vec2(1.0));
    }

    vec3 hdr_color = texture(u_scene, scene_uv).rgb;
    vec3 bloom_color = texture(u_bloom, v_uv).rgb;

    // Combine scene + bloom
    vec3 combined = hdr_color + bloom_color * u_bloom_intensity;

    // Event horizon shadow and photon ring
    float r_eh = u_eh_radius;
    if (r_eh > 0.001) {
        float r_shadow = r_eh * 2.6;  // black hole shadow ~2.6x Schwarzschild
        float r_photon = r_eh * 3.5;  // photon sphere

        // Event horizon: fully dark inside shadow radius
        float shadow_mask = smoothstep(r_shadow * 0.85, r_shadow, dist);
        combined *= shadow_mask;

        // Photon ring: thin bright ring at photon sphere
        float ring_width = r_eh * 0.25;
        float ring_dist = abs(dist - r_photon);
        float ring = exp(-ring_dist * ring_dist / (ring_width * ring_width));

        // Ring color: hot orange-white (M87*-inspired)
        vec3 ring_color = mix(vec3(1.8, 1.0, 0.2), vec3(2.5, 2.2, 2.0), ring);

        // Doppler beaming: brighter on approaching side
        float angle = atan(delta.y, delta.x);
        float doppler = 1.0 + 0.5 * sin(angle);

        combined += ring_color * ring * doppler * 1.5;

        // Thin inner glow just outside shadow edge
        float inner_glow_dist = dist - r_shadow;
        if (inner_glow_dist > 0.0 && inner_glow_dist < r_eh * 1.5) {
            float glow = exp(-inner_glow_dist / (r_eh * 0.3)) * 0.5;
            combined += vec3(1.2, 0.6, 0.15) * glow * doppler;
        }
    }

    // Exposure-compensated Reinhard tonemap
    vec3 exposed = combined * u_exposure;
    vec3 ldr = exposed / (vec3(1.0) + exposed);

    // Gamma correction
    ldr = pow(ldr, vec3(1.0 / 2.2));

    frag_color = vec4(ldr, 1.0);
}
