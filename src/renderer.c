#include "renderer.h"
#include <glad/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef SHADER_DIR
#define SHADER_DIR "src/shaders/"
#endif

static GLuint shader_program;
static GLuint vao, vbo;
static GLint u_view_loc, u_proj_loc;
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

    u_view_loc = glGetUniformLocation(shader_program, "u_view");
    u_proj_loc = glGetUniformLocation(shader_program, "u_projection");

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    // position (vec3)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    // mass (float)
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); // additive blending
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    return 0;
}

static void build_perspective(float *m, float fov_rad, float aspect, float near, float far)
{
    float f = 1.0f / tanf(fov_rad * 0.5f);
    memset(m, 0, 16 * sizeof(float));
    m[0]  = f / aspect;
    m[5]  = f;
    m[10] = (far + near) / (near - far);
    m[11] = -1.0f;
    m[14] = (2.0f * far * near) / (near - far);
}

static void build_look_at(float *m, float ex, float ey, float ez,
                           float tx, float ty, float tz)
{
    // up = (0, 0, 1)
    float fx = tx - ex, fy = ty - ey, fz = tz - ez;
    float fl = sqrtf(fx*fx + fy*fy + fz*fz);
    if (fl < 1e-12f) fl = 1.0f;
    fx /= fl; fy /= fl; fz /= fl;

    // right = forward x up
    float rx = fy * 1.0f - fz * 0.0f;
    float ry = fz * 0.0f - fx * 1.0f;
    float rz = fx * 0.0f - fy * 0.0f;
    float rl = sqrtf(rx*rx + ry*ry + rz*rz);
    if (rl < 1e-12f) {
        // forward is parallel to up, pick a different up
        rx = 1.0f; ry = 0.0f; rz = 0.0f;
    } else {
        rx /= rl; ry /= rl; rz /= rl;
    }

    // true up = right x forward
    float ux = ry * fz - rz * fy;
    float uy = rz * fx - rx * fz;
    float uz = rx * fy - ry * fx;

    // Column-major
    memset(m, 0, 16 * sizeof(float));
    m[0] = rx;  m[4] = ry;  m[8]  = rz;  m[12] = -(rx*ex + ry*ey + rz*ez);
    m[1] = ux;  m[5] = uy;  m[9]  = uz;  m[13] = -(ux*ex + uy*ey + uz*ez);
    m[2] = -fx; m[6] = -fy; m[10] = -fz; m[14] =  (fx*ex + fy*ey + fz*ez);
    m[3] = 0;   m[7] = 0;   m[11] = 0;   m[15] = 1.0f;
}

void renderer_draw(const Body *bodies, int n, const Camera *cam,
                   int window_width, int window_height)
{
    // Ensure upload buffer capacity
    if (n * 4 > upload_buf_capacity) {
        upload_buf_capacity = n * 4;
        upload_buf = realloc(upload_buf, upload_buf_capacity * sizeof(float));
    }

    // Convert double bodies to float upload buffer (x, y, z, mass)
    for (int i = 0; i < n; i++) {
        upload_buf[i * 4 + 0] = (float)bodies[i].x;
        upload_buf[i * 4 + 1] = (float)bodies[i].y;
        upload_buf[i * 4 + 2] = (float)bodies[i].z;
        upload_buf[i * 4 + 3] = (float)bodies[i].mass;
    }

    glClear(GL_COLOR_BUFFER_BIT);

    // Camera position from spherical coordinates
    float cos_el = cosf(cam->elevation);
    float eye_x = cam->target_x + cam->distance * cos_el * cosf(cam->azimuth);
    float eye_y = cam->target_y + cam->distance * cos_el * sinf(cam->azimuth);
    float eye_z = cam->target_z + cam->distance * sinf(cam->elevation);

    float view[16], proj[16];
    build_look_at(view, eye_x, eye_y, eye_z,
                  cam->target_x, cam->target_y, cam->target_z);

    float aspect = (float)window_width / (float)window_height;
    build_perspective(proj, 45.0f * 3.14159265f / 180.0f, aspect, 0.1f, cam->distance * 10.0f);

    glUseProgram(shader_program);
    glUniformMatrix4fv(u_view_loc, 1, GL_FALSE, view);
    glUniformMatrix4fv(u_proj_loc, 1, GL_FALSE, proj);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, n * 4 * sizeof(float), upload_buf, GL_STREAM_DRAW);
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
