#version 330 core

in vec2 v_uv;
uniform sampler2D u_scene;
uniform sampler2D u_bloom;
uniform float u_bloom_intensity;
uniform vec2 u_smbh_screen;
uniform float u_smbh_mass;
uniform float u_lensing_strength;
uniform float u_exposure;

out vec4 frag_color;

void main()
{
    // Apply gravitational lensing distortion to scene UV
    vec2 delta = v_uv - u_smbh_screen;
    float dist_sq = dot(delta, delta);
    float dist = sqrt(dist_sq);

    vec2 scene_uv = v_uv;
    if (dist > 0.001 && u_lensing_strength > 0.0) {
        float deflection = u_lensing_strength / (dist_sq + 0.001);
        deflection = min(deflection, 0.1);
        scene_uv = v_uv + normalize(delta) * deflection;
        scene_uv = clamp(scene_uv, vec2(0.0), vec2(1.0));
    }

    vec3 hdr_color = texture(u_scene, scene_uv).rgb;
    vec3 bloom_color = texture(u_bloom, v_uv).rgb;

    // Combine scene + bloom
    vec3 combined = hdr_color + bloom_color * u_bloom_intensity;

    // Exposure-compensated Reinhard tonemap
    vec3 exposed = combined * u_exposure;
    vec3 ldr = exposed / (vec3(1.0) + exposed);

    // Gamma correction
    ldr = pow(ldr, vec3(1.0 / 2.2));

    frag_color = vec4(ldr, 1.0);
}
