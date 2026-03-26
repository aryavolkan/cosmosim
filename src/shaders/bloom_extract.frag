#version 330 core

in vec2 v_uv;
uniform sampler2D u_scene;
uniform float u_threshold;

out vec4 frag_color;

void main()
{
    vec3 color = texture(u_scene, v_uv).rgb;
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));

    if (brightness > u_threshold) {
        frag_color = vec4(color - vec3(u_threshold), 1.0);
    } else {
        frag_color = vec4(0.0, 0.0, 0.0, 1.0);
    }
}
