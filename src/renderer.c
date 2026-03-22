#include "renderer.h"
#include <glad/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SHADER_DIR
#define SHADER_DIR "src/shaders/"
#endif

static GLuint shader_program;
static GLuint vao, vbo;
static GLint u_projection_loc;
static float *upload_buf = NULL;
static int upload_buf_capacity = 0;

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open shader: %s\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(len + 1);
    size_t read = fread(buf, 1, len, f);
    (void)read;
    buf[len] = '\0';
    fclose(f);
    return buf;
}

static GLuint compile_shader(const char *path, GLenum type)
{
    char *src = read_file(path);
    if (!src) return 0;

    GLuint s = glCreateShader(type);
    const char *src_ptr = src;
    glShaderSource(s, 1, &src_ptr, NULL);
    glCompileShader(s);

    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "Shader compile error (%s):\n%s\n", path, log);
        glDeleteShader(s);
        free(src);
        return 0;
    }
    free(src);
    return s;
}

int renderer_init(void)
{
    char vert_path[512], frag_path[512];
    snprintf(vert_path, sizeof(vert_path), "%sparticle.vert", SHADER_DIR);
    snprintf(frag_path, sizeof(frag_path), "%sparticle.frag", SHADER_DIR);

    GLuint vs = compile_shader(vert_path, GL_VERTEX_SHADER);
    GLuint fs = compile_shader(frag_path, GL_FRAGMENT_SHADER);
    if (!vs || !fs) return -1;

    shader_program = glCreateProgram();
    glAttachShader(shader_program, vs);
    glAttachShader(shader_program, fs);
    glLinkProgram(shader_program);

    GLint ok;
    glGetProgramiv(shader_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(shader_program, sizeof(log), NULL, log);
        fprintf(stderr, "Shader link error:\n%s\n", log);
        return -1;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);

    u_projection_loc = glGetUniformLocation(shader_program, "u_projection");

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    // position (vec2)
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    // mass (float)
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); // additive blending
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    return 0;
}

void renderer_draw(const Body *bodies, int n, const Camera *cam,
                   int window_width, int window_height)
{
    // Ensure upload buffer capacity
    if (n * 3 > upload_buf_capacity) {
        upload_buf_capacity = n * 3;
        upload_buf = realloc(upload_buf, upload_buf_capacity * sizeof(float));
    }

    // Convert double bodies to float upload buffer
    for (int i = 0; i < n; i++) {
        upload_buf[i * 3 + 0] = (float)bodies[i].x;
        upload_buf[i * 3 + 1] = (float)bodies[i].y;
        upload_buf[i * 3 + 2] = (float)bodies[i].mass;
    }

    glClear(GL_COLOR_BUFFER_BIT);

    // Build orthographic projection matrix
    float aspect = (float)window_width / (float)window_height;
    float z = cam->zoom;
    float l = -aspect * z + cam->offset_x;
    float r = aspect * z + cam->offset_x;
    float b = -z + cam->offset_y;
    float t = z + cam->offset_y;

    // Column-major orthographic matrix
    float proj[16] = {0};
    proj[0]  = 2.0f / (r - l);
    proj[5]  = 2.0f / (t - b);
    proj[10] = -1.0f;
    proj[12] = -(r + l) / (r - l);
    proj[13] = -(t + b) / (t - b);
    proj[15] = 1.0f;

    glUseProgram(shader_program);
    glUniformMatrix4fv(u_projection_loc, 1, GL_FALSE, proj);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, n * 3 * sizeof(float), upload_buf, GL_STREAM_DRAW);
    glDrawArrays(GL_POINTS, 0, n);
    glBindVertexArray(0);
}

void renderer_cleanup(void)
{
    glDeleteProgram(shader_program);
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    free(upload_buf);
}
