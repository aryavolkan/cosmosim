#version 330 core
in float v_mass;
out vec4 frag_color;

void main()
{
    vec2 coord = gl_PointCoord - vec2(0.5);
    float r_sq = dot(coord, coord);

    if (r_sq > 0.25) discard;

    float intensity = exp(-r_sq * 16.0);

    // Color by mass: low = blue-white, high = yellow-orange
    float t = clamp(log(v_mass + 1.0) / 5.0, 0.0, 1.0);
    vec3 color = mix(vec3(0.6, 0.7, 1.0), vec3(1.0, 0.8, 0.3), t);

    frag_color = vec4(color * intensity, intensity * 0.8);
}
