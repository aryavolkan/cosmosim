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
float hash2(vec2 p) {
    return fract(sin(dot(p, vec2(269.5, 183.3))) * 43758.5453);
}

void main()
{
    // ── Schwarzschild gravitational lensing ──────────────────────────────
    // Deflection angle α = 4GM/(c²b) ∝ 1/b (impact parameter).
    // Near the photon sphere (b → 1.5 r_s), deflection diverges → light
    // wraps around the BH creating Einstein rings and secondary images.

    vec2 scene_uv = v_uv;
    for (int s = 0; s < 2; s++) {
        if (s >= u_smbh_count) break;
        vec2 delta = v_uv - u_smbh_screen[s];
        delta.x *= u_aspect;
        float dist = length(delta);

        float r_eh = u_eh_radius[s];
        float r_s = r_eh;  // Schwarzschild radius in NDC
        if (r_s < 0.001 || dist < 0.0005) continue;

        // Impact parameter in units of Schwarzschild radius
        float b = dist / r_s;

        // Schwarzschild deflection: α ∝ 1/b, diverging near photon sphere (b=1.5)
        // Physical: α = 2 r_s / b for weak field, stronger near b → 1.5
        float photon_sphere = 1.5;
        float deflect_angle;
        if (b > photon_sphere + 0.1) {
            // Weak-field regime: classic 1/b deflection
            deflect_angle = 2.0 * r_s / b;
            // Add logarithmic correction for moderate b (strong-field enhancement)
            deflect_angle *= 1.0 + 0.8 / (b - photon_sphere);
        } else {
            // Near photon sphere: very strong deflection (light nearly orbits)
            float x = max(b - photon_sphere, 0.01);
            deflect_angle = r_s * 3.0 / (x + 0.02);
        }

        // Clamp to prevent UV going crazy
        deflect_angle = min(deflect_angle, 0.35);

        // Apply deflection: push UV toward the SMBH (light bends inward)
        vec2 dir = normalize(delta);
        dir.x /= u_aspect;
        scene_uv += dir * deflect_angle;
    }
    scene_uv = clamp(scene_uv, vec2(0.0), vec2(1.0));

    vec3 hdr_color = texture(u_scene, scene_uv).rgb;
    vec3 bloom_color = texture(u_bloom, v_uv).rgb;

    // ── Procedural background starfield ──────────────────────────────────
    // Sampled at LENSED UV so stars visibly warp, stretch into arcs,
    // and form Einstein rings around the SMBHs.
    {
        // Dense starfield layer (distant background galaxies + stars)
        vec2 sv = scene_uv * 400.0;
        vec2 sg = floor(sv);
        vec2 sf = fract(sv) - vec2(0.5);

        float h1 = hash(sg);
        float h2 = hash(sg + vec2(1.0, 0.0));
        float h3 = hash2(sg);

        // ~1.2% of cells have a star — denser for more visible lensing arcs
        float star_on = step(0.988, h1);
        float star_r = dot(sf, sf);
        float star_glow = exp(-star_r * 120.0) * star_on;

        // Color: blue-white to warm yellow
        vec3 star_col = mix(vec3(0.5, 0.65, 1.0), vec3(1.0, 0.92, 0.6), h2);
        hdr_color += star_col * star_glow * (0.3 + 0.5 * h3);

        // Fainter secondary layer (very distant, tiny) for more density
        vec2 sv2 = scene_uv * 800.0;
        vec2 sg2 = floor(sv2);
        vec2 sf2 = fract(sv2) - vec2(0.5);
        float h4 = hash(sg2 + vec2(42.0, 17.0));
        float faint_on = step(0.992, h4);
        float faint_glow = exp(-dot(sf2, sf2) * 200.0) * faint_on;
        hdr_color += vec3(0.6, 0.7, 0.9) * faint_glow * 0.15;
    }

    // ── Einstein ring glow ───────────────────────────────────────────────
    // At the Einstein radius, lensed background light accumulates into
    // a bright ring. Render as a subtle glow at the critical curve.
    for (int s = 0; s < 2; s++) {
        if (s >= u_smbh_count) break;
        float r_eh = u_eh_radius[s];
        if (r_eh < 0.001) continue;

        vec2 delta = v_uv - u_smbh_screen[s];
        delta.x *= u_aspect;
        float dist = length(delta);

        // Einstein ring radius ≈ 4-5x Schwarzschild radius in our projection
        float r_einstein = r_eh * 4.5;
        float ring_dist = abs(dist - r_einstein);
        float ring_width = r_eh * 0.8;
        float einstein_ring = exp(-ring_dist * ring_dist / (ring_width * ring_width));

        // Subtle blue-white glow from accumulated lensed starlight
        vec3 er_color = vec3(0.4, 0.5, 0.8);
        hdr_color += er_color * einstein_ring * 0.3;
    }

    // Combine scene + bloom
    vec3 combined = hdr_color + bloom_color * u_bloom_intensity;

    // ── Event horizon shadow and photon ring ─────────────────────────────
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

        // Secondary photon ring: dimmer, thinner, just inside the primary
        float r_photon2 = r_eh * 2.9;
        float ring2_dist = abs(dist - r_photon2);
        float ring2_width = r_eh * 0.1;
        float ring2 = exp(-ring2_dist * ring2_dist / (ring2_width * ring2_width)) * 0.3;

        // Ring color: hot orange-white (M87*-inspired)
        vec3 ring_color = mix(vec3(1.8, 1.0, 0.2), vec3(2.5, 2.2, 2.0), ring);

        // Frame-dragging / Lense-Thirring beaming
        float approach_factor;
        vec2 spin2d = u_smbh_spin[s];
        float spin_len = length(spin2d);
        if (spin_len > 0.05) {
            vec2 spin_corr = normalize(vec2(spin2d.x * u_aspect, spin2d.y));
            vec2 approach_dir = normalize(vec2(-spin_corr.y, spin_corr.x));
            vec2 disk_dir = normalize(delta);
            approach_factor = dot(disk_dir, approach_dir);
        } else {
            float angle = atan(delta.y, delta.x);
            approach_factor = sin(angle);
        }
        float doppler = 1.0 + 0.8 * approach_factor;
        vec3 ring_beamed = mix(ring_color * vec3(1.1, 1.0, 0.8),
                               ring_color * vec3(0.9, 1.0, 1.3),
                               clamp(approach_factor * 0.5 + 0.5, 0.0, 1.0));

        // Primary + secondary photon rings
        combined += ring_beamed * (ring + ring2) * doppler * 1.5;

        // Inner accretion glow
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
