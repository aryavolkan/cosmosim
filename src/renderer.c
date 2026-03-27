#include "renderer.h"
#include <glad/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef SHADER_DIR
#define SHADER_DIR "src/shaders/"
#endif

// Particle rendering
static GLuint particle_program;
static GLuint vao, vbo;
static GLint u_view_loc, u_proj_loc;
static GLint u_smbh_pos_loc, u_smbh_lum_loc;
static float *upload_buf = NULL;
static int upload_buf_capacity = 0;

// HDR framebuffer
static GLuint hdr_fbo, hdr_color_tex, hdr_depth_rbo;
static int hdr_width = 0, hdr_height = 0;
static int hdr_active = 0;

// Fullscreen quad VAO
static GLuint fullscreen_vao;

// Post-process programs
static GLuint bloom_extract_program = 0;
static GLuint bloom_blur_program = 0;
static GLuint composite_program = 0;

// Bloom FBOs
static GLuint bloom_fbo[2], bloom_tex[2];
static int bloom_width = 0, bloom_height = 0;

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
    size_t rd = fread(buf, 1, len, f);
    (void)rd;
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

static GLuint link_program(GLuint vs, GLuint fs)
{
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        fprintf(stderr, "Shader link error:\n%s\n", log);
        return 0;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

static GLuint load_program(const char *vert_name, const char *frag_name)
{
    char vp[512], fp[512];
    snprintf(vp, sizeof(vp), "%s%s", SHADER_DIR, vert_name);
    snprintf(fp, sizeof(fp), "%s%s", SHADER_DIR, frag_name);
    GLuint vs = compile_shader(vp, GL_VERTEX_SHADER);
    GLuint fs = compile_shader(fp, GL_FRAGMENT_SHADER);
    if (!vs || !fs) return 0;
    return link_program(vs, fs);
}

static void setup_hdr_fbo(int width, int height)
{
    if (hdr_fbo) {
        glDeleteFramebuffers(1, &hdr_fbo);
        glDeleteTextures(1, &hdr_color_tex);
        glDeleteRenderbuffers(1, &hdr_depth_rbo);
    }

    glGenFramebuffers(1, &hdr_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, hdr_fbo);

    glGenTextures(1, &hdr_color_tex);
    glBindTexture(GL_TEXTURE_2D, hdr_color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, hdr_color_tex, 0);

    glGenRenderbuffers(1, &hdr_depth_rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, hdr_depth_rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, hdr_depth_rbo);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    hdr_width = width;
    hdr_height = height;
}

static void setup_bloom_fbos(int width, int height)
{
    int bw = width / 2;
    int bh = height / 2;

    if (bloom_fbo[0]) {
        glDeleteFramebuffers(2, bloom_fbo);
        glDeleteTextures(2, bloom_tex);
    }

    for (int i = 0; i < 2; i++) {
        glGenFramebuffers(1, &bloom_fbo[i]);
        glGenTextures(1, &bloom_tex[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, bloom_fbo[i]);
        glBindTexture(GL_TEXTURE_2D, bloom_tex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, bw, bh, 0, GL_RGBA, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloom_tex[i], 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    bloom_width = bw;
    bloom_height = bh;
}

int renderer_init(const RendererConfig *rcfg)
{
    hdr_active = rcfg ? rcfg->hdr_enabled : 0;

    particle_program = load_program("particle.vert", "particle.frag");
    if (!particle_program) return -1;

    u_view_loc = glGetUniformLocation(particle_program, "u_view");
    u_proj_loc = glGetUniformLocation(particle_program, "u_projection");
    u_smbh_pos_loc = glGetUniformLocation(particle_program, "u_smbh_pos");
    u_smbh_lum_loc = glGetUniformLocation(particle_program, "u_smbh_luminosity");

    // VAO/VBO: pos(3) + mass(1) + vel(3) + type(1) = 8 floats
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    int stride = 8 * sizeof(float);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, stride, (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (void *)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, (void *)(7 * sizeof(float)));
    glEnableVertexAttribArray(3);

    glBindVertexArray(0);

    // Fullscreen VAO (uses gl_VertexID)
    glGenVertexArrays(1, &fullscreen_vao);

    // Post-processing (HDR only)
    if (hdr_active) {
        bloom_extract_program = load_program("fullscreen.vert", "bloom_extract.frag");
        bloom_blur_program = load_program("fullscreen.vert", "bloom_blur.frag");
        composite_program = load_program("fullscreen.vert", "composite.frag");

        if (!bloom_extract_program || !bloom_blur_program || !composite_program) {
            fprintf(stderr, "Warning: some post-processing shaders failed to load\n");
        }
    }

    glEnable(GL_PROGRAM_POINT_SIZE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    return 0;
}

void renderer_update_smbh(RendererConfig *rcfg, const Body *bodies, int n)
{
    for (int i = 0; i < n; i++) {
        if (bodies[i].type == BODY_SMBH && bodies[i].mass > 0.0) {
            rcfg->smbh_x = (float)bodies[i].x;
            rcfg->smbh_y = (float)bodies[i].y;
            rcfg->smbh_z = (float)bodies[i].z;
            rcfg->smbh_luminosity = (float)bodies[i].luminosity;
            rcfg->smbh_mass = (float)bodies[i].mass;
            break;
        }
    }
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
    float fx = tx - ex, fy = ty - ey, fz = tz - ez;
    float fl = sqrtf(fx*fx + fy*fy + fz*fz);
    if (fl < 1e-12f) fl = 1.0f;
    fx /= fl; fy /= fl; fz /= fl;

    float rx = fy * 1.0f - fz * 0.0f;
    float ry = fz * 0.0f - fx * 1.0f;
    float rz = fx * 0.0f - fy * 0.0f;
    float rl = sqrtf(rx*rx + ry*ry + rz*rz);
    if (rl < 1e-12f) {
        rx = 1.0f; ry = 0.0f; rz = 0.0f;
    } else {
        rx /= rl; ry /= rl; rz /= rl;
    }

    float ux = ry * fz - rz * fy;
    float uy = rz * fx - rx * fz;
    float uz = rx * fy - ry * fx;

    memset(m, 0, 16 * sizeof(float));
    m[0] = rx;  m[4] = ry;  m[8]  = rz;  m[12] = -(rx*ex + ry*ey + rz*ez);
    m[1] = ux;  m[5] = uy;  m[9]  = uz;  m[13] = -(ux*ex + uy*ey + uz*ez);
    m[2] = -fx; m[6] = -fy; m[10] = -fz; m[14] =  (fx*ex + fy*ey + fz*ez);
    m[3] = 0;   m[7] = 0;   m[11] = 0;   m[15] = 1.0f;
}

static void draw_fullscreen_quad(void)
{
    glBindVertexArray(fullscreen_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void renderer_draw(const Body *bodies, int n, const Camera *cam,
                   int window_width, int window_height,
                   const RendererConfig *rcfg)
{
    // Ensure upload buffer (8 floats per body)
    if (n * 8 > upload_buf_capacity) {
        upload_buf_capacity = n * 8;
        float *tmp = realloc(upload_buf, upload_buf_capacity * sizeof(float));
        if (tmp) upload_buf = tmp;
    }

    // Upload: x, y, z, mass, vx, vy, vz, type (skip dead bodies)
    int count = 0;
    for (int i = 0; i < n; i++) {
        if (bodies[i].mass <= 0.0) continue;
        int off = count * 8;
        upload_buf[off + 0] = (float)bodies[i].x;
        upload_buf[off + 1] = (float)bodies[i].y;
        upload_buf[off + 2] = (float)bodies[i].z;
        upload_buf[off + 3] = (float)bodies[i].mass;
        upload_buf[off + 4] = (float)bodies[i].vx;
        upload_buf[off + 5] = (float)bodies[i].vy;
        upload_buf[off + 6] = (float)bodies[i].vz;
        upload_buf[off + 7] = (float)bodies[i].type;
        count++;
    }

    // Compute CPU-side exposure estimate
    if (rcfg) {
        float cam_x = cam->target_x + cam->distance * cosf(cam->elevation) * cosf(cam->azimuth);
        float cam_y = cam->target_y + cam->distance * cosf(cam->elevation) * sinf(cam->azimuth);
        float cam_z = cam->target_z + cam->distance * sinf(cam->elevation);

        float total_lum = 0.0f;
        for (int i = 0; i < count; i++) {
            int off = i * 8;
            float dx = upload_buf[off + 0] - cam_x;
            float dy = upload_buf[off + 1] - cam_y;
            float dz = upload_buf[off + 2] - cam_z;
            float dist_sq = dx * dx + dy * dy + dz * dz + 1.0f;
            float mass = upload_buf[off + 3];
            total_lum += mass / dist_sq;
        }
        float avg_lum = count > 0 ? total_lum / (float)count : 0.001f;
        if (avg_lum < 0.001f) avg_lum = 0.001f;

        float target_exposure = 0.18f / avg_lum;
        if (target_exposure < 0.5f) target_exposure = 0.5f;
        if (target_exposure > 20.0f) target_exposure = 20.0f;

        // Cast away const for exposure update (exposure is mutable render state)
        ((RendererConfig *)rcfg)->exposure = 0.9f * rcfg->exposure + 0.1f * target_exposure;
    }

    // HDR FBO setup
    if (hdr_active && rcfg) {
        if (window_width != hdr_width || window_height != hdr_height) {
            setup_hdr_fbo(window_width, window_height);
            setup_bloom_fbos(window_width, window_height);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, hdr_fbo);
        glViewport(0, 0, window_width, window_height);
    }

    glClear(GL_COLOR_BUFFER_BIT);

    // Camera
    float cos_el = cosf(cam->elevation);
    float eye_x = cam->target_x + cam->distance * cos_el * cosf(cam->azimuth);
    float eye_y = cam->target_y + cam->distance * cos_el * sinf(cam->azimuth);
    float eye_z = cam->target_z + cam->distance * sinf(cam->elevation);

    float view[16], proj[16];
    build_look_at(view, eye_x, eye_y, eye_z,
                  cam->target_x, cam->target_y, cam->target_z);

    float aspect = (float)window_width / (float)window_height;
    build_perspective(proj, 45.0f * 3.14159265f / 180.0f, aspect, 0.1f, cam->distance * 10.0f);

    // Draw particles
    glUseProgram(particle_program);
    glUniformMatrix4fv(u_view_loc, 1, GL_FALSE, view);
    glUniformMatrix4fv(u_proj_loc, 1, GL_FALSE, proj);

    if (rcfg && u_smbh_pos_loc >= 0) {
        glUniform3f(u_smbh_pos_loc, rcfg->smbh_x, rcfg->smbh_y, rcfg->smbh_z);
    }
    if (rcfg && u_smbh_lum_loc >= 0) {
        glUniform1f(u_smbh_lum_loc, rcfg->smbh_luminosity);
    }

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, count * 8 * sizeof(float), upload_buf, GL_STREAM_DRAW);
    glDrawArrays(GL_POINTS, 0, count);
    glBindVertexArray(0);

    // Post-processing (HDR path)
    if (hdr_active && rcfg && bloom_extract_program && composite_program) {
        int iterations = rcfg->bloom_iterations > 0 ? rcfg->bloom_iterations : 2;

        // Bloom extract
        glBindFramebuffer(GL_FRAMEBUFFER, bloom_fbo[0]);
        glViewport(0, 0, bloom_width, bloom_height);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(bloom_extract_program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hdr_color_tex);
        glUniform1i(glGetUniformLocation(bloom_extract_program, "u_scene"), 0);
        glUniform1f(glGetUniformLocation(bloom_extract_program, "u_threshold"), 1.0f);
        draw_fullscreen_quad();

        // Gaussian blur ping-pong
        if (bloom_blur_program) {
            for (int i = 0; i < iterations; i++) {
                // Horizontal
                glBindFramebuffer(GL_FRAMEBUFFER, bloom_fbo[1]);
                glClear(GL_COLOR_BUFFER_BIT);
                glUseProgram(bloom_blur_program);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, bloom_tex[0]);
                glUniform1i(glGetUniformLocation(bloom_blur_program, "u_image"), 0);
                glUniform2f(glGetUniformLocation(bloom_blur_program, "u_direction"),
                            1.0f / bloom_width, 0.0f);
                draw_fullscreen_quad();

                // Vertical
                glBindFramebuffer(GL_FRAMEBUFFER, bloom_fbo[0]);
                glClear(GL_COLOR_BUFFER_BIT);
                glBindTexture(GL_TEXTURE_2D, bloom_tex[1]);
                glUniform2f(glGetUniformLocation(bloom_blur_program, "u_direction"),
                            0.0f, 1.0f / bloom_height);
                draw_fullscreen_quad();
            }
        }

        // Compute SMBH screen position for lensing
        const float mv[4] = {
            view[0]*rcfg->smbh_x + view[4]*rcfg->smbh_y + view[8]*rcfg->smbh_z + view[12],
            view[1]*rcfg->smbh_x + view[5]*rcfg->smbh_y + view[9]*rcfg->smbh_z + view[13],
            view[2]*rcfg->smbh_x + view[6]*rcfg->smbh_y + view[10]*rcfg->smbh_z + view[14],
            view[3]*rcfg->smbh_x + view[7]*rcfg->smbh_y + view[11]*rcfg->smbh_z + view[15]
        };
        float smbh_clip_x = proj[0]*mv[0] + proj[4]*mv[1] + proj[8]*mv[2] + proj[12]*mv[3];
        float smbh_clip_y = proj[1]*mv[0] + proj[5]*mv[1] + proj[9]*mv[2] + proj[13]*mv[3];
        float smbh_clip_w = proj[3]*mv[0] + proj[7]*mv[1] + proj[11]*mv[2] + proj[15]*mv[3];

        float smbh_ndc_x = 0.5f, smbh_ndc_y = 0.5f;
        if (smbh_clip_w > 0.001f) {
            smbh_ndc_x = (smbh_clip_x / smbh_clip_w) * 0.5f + 0.5f;
            smbh_ndc_y = (smbh_clip_y / smbh_clip_w) * 0.5f + 0.5f;
        }

        // Composite to screen
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, window_width, window_height);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(composite_program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, hdr_color_tex);
        glUniform1i(glGetUniformLocation(composite_program, "u_scene"), 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, bloom_tex[0]);
        glUniform1i(glGetUniformLocation(composite_program, "u_bloom"), 1);
        glUniform1f(glGetUniformLocation(composite_program, "u_bloom_intensity"), 0.5f);
        glUniform1f(glGetUniformLocation(composite_program, "u_exposure"),
                    rcfg->exposure);
        glUniform2f(glGetUniformLocation(composite_program, "u_smbh_screen"),
                    smbh_ndc_x, smbh_ndc_y);
        glUniform1f(glGetUniformLocation(composite_program, "u_smbh_mass"),
                    rcfg->smbh_mass);
        glUniform1f(glGetUniformLocation(composite_program, "u_lensing_strength"),
                    rcfg->smbh_mass * 0.0001f);
        draw_fullscreen_quad();
    } else if (hdr_active) {
        // Fallback blit
        glBindFramebuffer(GL_READ_FRAMEBUFFER, hdr_fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, hdr_width, hdr_height,
                          0, 0, window_width, window_height,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

void renderer_cleanup(void)
{
    glDeleteProgram(particle_program);
    if (bloom_extract_program) glDeleteProgram(bloom_extract_program);
    if (bloom_blur_program) glDeleteProgram(bloom_blur_program);
    if (composite_program) glDeleteProgram(composite_program);

    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &fullscreen_vao);

    if (hdr_fbo) {
        glDeleteFramebuffers(1, &hdr_fbo);
        glDeleteTextures(1, &hdr_color_tex);
        glDeleteRenderbuffers(1, &hdr_depth_rbo);
    }
    if (bloom_fbo[0]) {
        glDeleteFramebuffers(2, bloom_fbo);
        glDeleteTextures(2, bloom_tex);
    }

    free(upload_buf);
}
