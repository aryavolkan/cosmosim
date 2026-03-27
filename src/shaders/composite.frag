#version 330 core

in vec2 v_uv;
uniform sampler2D u_scene;
uniform sampler2D u_bloom;
uniform float u_bloom_intensity;
uniform float u_exposure;
uniform float u_aspect;

// Up to 2 SMBHs
uniform int u_smbh_count;
uniform vec2 u_smbh_screen[2];
uniform float u_smbh_mass[2];
uniform float u_lensing_strength[2];
uniform float u_eh_radius[2];
// 2D screen-space projection of each SMBH's spin axis (for frame-dragging beaming)
uniform vec2 u_smbh_spin[2];

out vec4 frag_color;

// Fast hash for procedural starfield
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main()
{
    // Accumulate gravitational lensing from all SMBHs
    // Wider scale (5× EH radius) and higher max deflection for dramatic warping
    vec2 scene_uv = v_uv;
    for (int s = 0; s < 2; s++) {
        if (s >= u_smbh_count) break;
        vec2 delta = v_uv - u_smbh_screen[s];
        delta.x *= u_aspect;
        float dist = length(delta);

        if (dist > 0.001 && u_lensing_strength[s] > 0.0) {
            float r_eh = u_eh_radius[s];
            // Wider lensing radius: 5× event horizon (was 3×)
            float scale = max(r_eh * 5.0, 0.015);
            float deflection = u_lensing_strength[s] * scale / (dist * dist + scale * scale);
            // Wider Gaussian envelope so background stars clearly show distortion
            deflection *= exp(-dist * dist / (scale * scale * 18.0));
            // Higher max deflection for dramatically visible Einstein ring warping
            deflection = min(deflection, 0.20);
            vec2 dir = normalize(delta);
            dir.x /= u_aspect;
            scene_uv += dir * deflection;
        }
    }
    scene_uv = clamp(scene_uv, vec2(0.0), vec2(1.0));

    vec3 hdr_color = texture(u_scene, scene_uv).rgb;
    vec3 bloom_color = texture(u_bloom, v_uv).rgb;

    // Procedural background starfield — sampled at lensed UV so stars visibly
    // warp and shift around SMBHs, making gravitational lensing obvious
    {
        vec2 sv  = scene_uv * 320.0;
        vec2 sg  = floor(sv);
        vec2 sf  = fract(sv) - vec2(0.5);

        float h1 = hash(sg);
        float h2 = hash(sg + vec2(1.0, 0.0));
        float h3 = hash(sg + vec2(0.0, 1.0));

        // ~0.6% of cells contain a star
        float star_on = step(0.994, h1);
        float star_r  = dot(sf, sf);
        float star_glow = exp(-star_r * 90.0) * star_on;

        // Color varies: blue-white to warm yellow-white
        vec3 star_col = mix(vec3(0.55, 0.70, 1.0), vec3(1.0, 0.95, 0.65), h2);
        // Brightness varies between stars
        hdr_color += star_col * star_glow * (0.35 + 0.55 * h3);
    }

    // Combine scene + bloom
    vec3 combined = hdr_color + bloom_color * u_bloom_intensity;

    // Event horizon shadow and photon ring for each SMBH
    for (int s = 0; s < 2; s++) {
        if (s >= u_smbh_count) break;
        float r_eh = u_eh_radius[s];
        if (r_eh < 0.001) continue;

        vec2 delta = v_uv - u_smbh_screen[s];
        delta.x *= u_aspect;
        float dist = length(delta);

        float r_shadow = r_eh * 2.6;
        float r_photon = r_eh * 3.5;

        // Event horizon: fully dark inside shadow radius
        float shadow_mask = smoothstep(r_shadow * 0.85, r_shadow, dist);
        combined *= shadow_mask;

        // Photon ring: thin bright ring at photon sphere
        float ring_width = r_eh * 0.25;
        float ring_dist  = abs(dist - r_photon);
        float ring = exp(-ring_dist * ring_dist / (ring_width * ring_width));

        // Ring color: hot orange-white (M87*-inspired)
        vec3 ring_color = mix(vec3(1.8, 1.0, 0.2), vec3(2.5, 2.2, 2.0), ring);

        // ── Frame-dragging / Lense-Thirring beaming ─────────────────────────
        // The spin axis projects to a 2D direction on-screen. The approaching
        // limb of the accretion disk (90° from spin axis, in the rotation direction)
        // is Doppler-boosted significantly brighter than the receding limb.
        float approach_factor;
        vec2 spin2d = u_smbh_spin[s];
        float spin_len = length(spin2d);
        if (spin_len > 0.05) {
            // Aspect-correct the spin for the same space as delta
            vec2 spin_corr = normalize(vec2(spin2d.x * u_aspect, spin2d.y));
            // Approach direction: 90° CCW from spin (disk limb moving toward viewer)
            vec2 approach_dir = normalize(vec2(-spin_corr.y, spin_corr.x));
            vec2 disk_dir = normalize(delta); // delta already aspect-corrected
            approach_factor = dot(disk_dir, approach_dir);
        } else {
            float angle = atan(delta.y, delta.x);
            approach_factor = sin(angle); // fallback
        }
        // Relativistic beaming: I ∝ (1 + β cosθ)^3; approximate here with strong asymmetry
        float doppler = 1.0 + 0.8 * approach_factor;
        // Color shift: approaching side slightly bluer, receding side redder
        vec3 ring_beamed = mix(ring_color * vec3(1.1, 1.0, 0.8),   // receding (warm)
                               ring_color * vec3(0.9, 1.0, 1.3),   // approaching (cool-blue)
                               clamp(approach_factor * 0.5 + 0.5, 0.0, 1.0));

        combined += ring_beamed * ring * doppler * 1.5;

        // Thin inner glow just outside shadow edge (accretion disk emission)
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
