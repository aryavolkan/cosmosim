#version 330 core

in vec2 v_uv;
uniform sampler2D u_image;
uniform vec2 u_direction;

out vec4 frag_color;

void main()
{
    // 9-tap Gaussian kernel
    float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

    vec3 result = texture(u_image, v_uv).rgb * weights[0];

    for (int i = 1; i < 5; i++) {
        vec2 offset = u_direction * float(i);
        result += texture(u_image, v_uv + offset).rgb * weights[i];
        result += texture(u_image, v_uv - offset).rgb * weights[i];
    }

    frag_color = vec4(result, 1.0);
}
