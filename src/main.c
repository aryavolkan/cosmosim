#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "body.h"
#include "octree.h"
#include "integrator.h"
#include "renderer.h"
#include "initial_conditions.h"
#include "quasar.h"

#define DEFAULT_N       20000
#define G               1.0
#define SOFTENING       1.5
#define THETA           0.5
#define DT              0.005
#define SUBSTEPS        2
#define WINDOW_WIDTH    1280
#define WINDOW_HEIGHT   800
#define DEFAULT_SMBH_MASS_FRAC     0.05
#define DEFAULT_ACCRETION_RADIUS   3.0
#define DEFAULT_JET_SPEED          8.0
#define DEFAULT_FEEDBACK_STRENGTH  0.3
#define DEFAULT_RENDER_WIDTH       1920
#define DEFAULT_RENDER_HEIGHT      1080
#define DEFAULT_RENDER_FRAMES      1000
#define DEFAULT_RENDER_SUBSTEPS    8
#define DEFAULT_RENDER_ORBIT_SPEED 0.003f

/* ---- Offline rendering helpers ---- */

static GLuint render_fbo, render_color_tex, render_depth_rbo;

static int setup_render_fbo(int width, int height)
{
    glGenFramebuffers(1, &render_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, render_fbo);

    glGenTextures(1, &render_color_tex);
    glBindTexture(GL_TEXTURE_2D, render_color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, render_color_tex, 0);

    glGenRenderbuffers(1, &render_depth_rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, render_depth_rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, render_depth_rbo);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "Render FBO incomplete\n");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return -1;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return 0;
}

static int save_ppm(const char *path, int width, int height, const unsigned char *pixels)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    /* OpenGL gives bottom-up; write top-down */
    for (int y = height - 1; y >= 0; y--) {
        const unsigned char *row = pixels + y * width * 4;
        for (int x = 0; x < width; x++) {
            fputc(row[x * 4 + 0], f);
            fputc(row[x * 4 + 1], f);
            fputc(row[x * 4 + 2], f);
        }
    }
    fclose(f);
    return 0;
}

/* ---- Interactive mode globals ---- */

static Camera camera = {0.8f, 0.6f, 80.0f, 0.0f, 0.0f, 0.0f};
static int paused = 0;
static double last_cursor_x, last_cursor_y;
static int dragging_left = 0;
static int dragging_right = 0;

static void scroll_callback(GLFWwindow *window, double xoff, double yoff)
{
    (void)window;
    (void)xoff;
    camera.distance *= (float)(1.0 - 0.1 * yoff);
    if (camera.distance < 1.0f) camera.distance = 1.0f;
    if (camera.distance > 100000.0f) camera.distance = 100000.0f;
}

static void mouse_button_callback(GLFWwindow *window, int button, int action, int mods)
{
    (void)mods;
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            dragging_left = 1;
            glfwGetCursorPos(window, &last_cursor_x, &last_cursor_y);
        } else {
            dragging_left = 0;
        }
    }
    if (button == GLFW_MOUSE_BUTTON_RIGHT || button == GLFW_MOUSE_BUTTON_MIDDLE) {
        if (action == GLFW_PRESS) {
            dragging_right = 1;
            glfwGetCursorPos(window, &last_cursor_x, &last_cursor_y);
        } else {
            dragging_right = 0;
        }
    }
}

static void cursor_pos_callback(GLFWwindow *window, double xpos, double ypos)
{
    if (!dragging_left && !dragging_right) return;

    double dx = xpos - last_cursor_x;
    double dy = ypos - last_cursor_y;
    last_cursor_x = xpos;
    last_cursor_y = ypos;

    if (dragging_left) {
        // Orbit: left-drag rotates the camera
        camera.azimuth -= (float)(dx * 0.005);
        camera.elevation += (float)(dy * 0.005);
        // Clamp elevation to avoid gimbal lock
        float limit = 1.5f; // ~86 degrees
        if (camera.elevation > limit) camera.elevation = limit;
        if (camera.elevation < -limit) camera.elevation = -limit;
    }

    if (dragging_right) {
        // Pan: right/middle-drag moves the target
        int w, h;
        glfwGetWindowSize(window, &w, &h);
        float scale = camera.distance * 0.002f;

        // Pan in the camera's local right and up directions
        float cos_az = cosf(camera.azimuth);
        float sin_az = sinf(camera.azimuth);

        // Right vector (in XY plane)
        float rx = -sin_az;
        float ry =  cos_az;

        // Up vector (simplified, based on elevation)
        float cos_el = cosf(camera.elevation);
        float sin_el = sinf(camera.elevation);
        float ux = -sin_el * cos_az;
        float uy = -sin_el * sin_az;
        float uz =  cos_el;

        camera.target_x += (float)(-dx * scale) * rx + (float)(dy * scale) * ux;
        camera.target_y += (float)(-dx * scale) * ry + (float)(dy * scale) * uy;
        camera.target_z += (float)(dy * scale) * uz;
    }
}

static void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    (void)scancode;
    (void)mods;
    if (action != GLFW_PRESS) return;

    if (key == GLFW_KEY_SPACE) paused = !paused;
    if (key == GLFW_KEY_Q || key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(window, 1);
    // Reset camera
    if (key == GLFW_KEY_R) {
        camera.azimuth = 0.8f;
        camera.elevation = 0.6f;
        camera.distance = 80.0f;
        camera.target_x = 0.0f;
        camera.target_y = 0.0f;
        camera.target_z = 0.0f;
    }
}

static void framebuffer_size_callback(GLFWwindow *window, int width, int height)
{
    (void)window;
    glViewport(0, 0, width, height);
}

int main(int argc, char **argv)
{
    int n = DEFAULT_N;
    int merger = 0;
    int quasar = 0;
    int high_fidelity = 0;
    double dt = DT;
    double theta = THETA;
    double smbh_mass_frac = DEFAULT_SMBH_MASS_FRAC;
    double accretion_radius = DEFAULT_ACCRETION_RADIUS;
    double jet_speed = DEFAULT_JET_SPEED;
    double feedback_strength = DEFAULT_FEEDBACK_STRENGTH;

    /* Offline render options */
    const char *render_dir = NULL;
    int render_width = DEFAULT_RENDER_WIDTH;
    int render_height = DEFAULT_RENDER_HEIGHT;
    int render_frames = DEFAULT_RENDER_FRAMES;
    int render_substeps = DEFAULT_RENDER_SUBSTEPS;
    float orbit_speed = DEFAULT_RENDER_ORBIT_SPEED;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            n = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--merger") == 0) {
            merger = 1;
        } else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quasar") == 0) {
            quasar = 1;
        } else if (strcmp(argv[i], "--high-fidelity") == 0) {
            high_fidelity = 1;
        } else if (strcmp(argv[i], "-dt") == 0 && i + 1 < argc) {
            dt = atof(argv[++i]);
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            theta = atof(argv[++i]);
        } else if (strcmp(argv[i], "--smbh-mass") == 0 && i + 1 < argc) {
            smbh_mass_frac = atof(argv[++i]);
        } else if (strcmp(argv[i], "--accretion-radius") == 0 && i + 1 < argc) {
            accretion_radius = atof(argv[++i]);
        } else if (strcmp(argv[i], "--jet-speed") == 0 && i + 1 < argc) {
            jet_speed = atof(argv[++i]);
        } else if (strcmp(argv[i], "--feedback-strength") == 0 && i + 1 < argc) {
            feedback_strength = atof(argv[++i]);
        } else if (strcmp(argv[i], "--render") == 0 && i + 1 < argc) {
            render_dir = argv[++i];
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            render_frames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            render_width = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            render_height = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--render-substeps") == 0 && i + 1 < argc) {
            render_substeps = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--orbit-speed") == 0 && i + 1 < argc) {
            orbit_speed = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: cosmosim [options]\n"
                   "  -n <count>              Number of bodies (default %d)\n"
                   "  -m, --merger            Galaxy merger mode\n"
                   "  -q, --quasar            Enable quasar physics (SMBH + accretion + jets)\n"
                   "  --high-fidelity         Higher substeps and jet density\n"
                   "  -dt <value>             Timestep (default %.4f)\n"
                   "  -t <theta>              Barnes-Hut opening angle (default %.1f)\n"
                   "  --smbh-mass <frac>      SMBH mass fraction (default %.2f)\n"
                   "  --accretion-radius <r>  Accretion radius (default %.1f)\n"
                   "  --jet-speed <v>         Jet particle speed (default %.1f)\n"
                   "  --feedback-strength <s> Feedback multiplier (default %.1f)\n"
                   "\nOffline rendering:\n"
                   "  --render <dir>          Render frames to directory (PPM format)\n"
                   "  --frames <n>            Number of frames (default %d)\n"
                   "  --width <w>             Output width (default %d)\n"
                   "  --height <h>            Output height (default %d)\n"
                   "  --render-substeps <n>   Physics substeps per frame (default %d)\n"
                   "  --orbit-speed <f>       Camera orbit speed in rad/frame (default %.3f)\n"
                   "\nControls (interactive mode):\n"
                   "  Scroll        Zoom in/out\n"
                   "  Left-drag     Orbit camera\n"
                   "  Right-drag    Pan\n"
                   "  Space         Pause/resume\n"
                   "  R             Reset camera\n"
                   "  Q/Esc         Quit\n"
                   "\nCombine frames into video:\n"
                   "  ffmpeg -framerate 60 -i <dir>/frame_%%06d.ppm -c:v libx264 -pix_fmt yuv420p out.mp4\n",
                   DEFAULT_N, DT, THETA,
                   DEFAULT_SMBH_MASS_FRAC, DEFAULT_ACCRETION_RADIUS,
                   DEFAULT_JET_SPEED, DEFAULT_FEEDBACK_STRENGTH,
                   DEFAULT_RENDER_FRAMES, DEFAULT_RENDER_WIDTH, DEFAULT_RENDER_HEIGHT,
                   DEFAULT_RENDER_SUBSTEPS, (double)DEFAULT_RENDER_ORBIT_SPEED);
            return 0;
        }
    }

    if (n < 2) n = 2;
    int substeps = render_dir ? render_substeps : (high_fidelity ? 8 : SUBSTEPS);

    printf("cosmosim: %d bodies, %s%s mode, dt=%.4f, theta=%.2f\n",
           n, merger ? "merger" : "galaxy", quasar ? " quasar" : "", dt, theta);

    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    if (render_dir) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    GLFWwindow *window = glfwCreateWindow(
        render_dir ? render_width : WINDOW_WIDTH,
        render_dir ? render_height : WINDOW_HEIGHT,
        "cosmosim", NULL, NULL);
    if (!window) {
        fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(render_dir ? 0 : 1);

    int version = gladLoadGL(glfwGetProcAddress);
    if (!version) {
        fprintf(stderr, "Failed to initialize GLAD\n");
        glfwTerminate();
        return 1;
    }
    printf("OpenGL %d.%d\n", GLAD_VERSION_MAJOR(version), GLAD_VERSION_MINOR(version));

    glfwSetScrollCallback(window, scroll_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_pos_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    RendererConfig rcfg;
    memset(&rcfg, 0, sizeof(rcfg));
    rcfg.hdr_enabled = quasar || render_dir;
    rcfg.bloom_iterations = (render_dir || high_fidelity) ? 4 : 2;
    rcfg.lensing_samples = (render_dir || high_fidelity) ? 4 : 1;
    rcfg.exposure = 1.0f;

    if (renderer_init(&rcfg) != 0) {
        fprintf(stderr, "Failed to initialize renderer\n");
        glfwTerminate();
        return 1;
    }

    // Allocate bodies with headroom for jet particles
    int n_alloc = quasar ? n + n / 4 : n;
    Body *bodies = calloc(n_alloc, sizeof(Body));
    OctreeNode *pool = malloc(8 * n_alloc * sizeof(OctreeNode));

    if (!bodies || !pool) {
        fprintf(stderr, "Failed to allocate memory\n");
        return 1;
    }

    // Generate initial conditions
    if (quasar) {
        if (merger) {
            generate_quasar_merger(bodies, n, 60.0, 0.3, smbh_mass_frac);
        } else {
            generate_quasar_galaxy(bodies, n, 0.0, 0.0, (double)n * 2.0, 30.0,
                                   0.0, 0.0, smbh_mass_frac);
        }
    } else {
        if (merger) {
            generate_merger(bodies, n, 60.0, 0.3);
        } else {
            generate_spiral_galaxy(bodies, n, 0.0, 0.0, (double)n * 2.0, 30.0, 0.0, 0.0);
        }
    }

    // Compute initial accelerations
    integrator_init_accelerations(bodies, n, G, SOFTENING, theta, pool);

    QuasarConfig qcfg = quasar_default_config();
    if (quasar) {
        qcfg.accretion_radius = accretion_radius;
        qcfg.jet_speed = jet_speed;
        qcfg.feedback_strength = feedback_strength;
        qcfg.jet_cap = high_fidelity ? n / 4 : n / 10;
        qcfg.max_bodies = n_alloc;
    }
    int current_n = n;
    int compact_counter = 0;
    int compact_interval = high_fidelity ? 60 : 120;

    /* ---- Offline render mode ---- */
    if (render_dir) {
        mkdir(render_dir, 0755);

        if (setup_render_fbo(render_width, render_height) != 0) {
            fprintf(stderr, "Failed to create render FBO\n");
            return 1;
        }

        unsigned char *pixels = malloc(render_width * render_height * 4);
        if (!pixels) {
            fprintf(stderr, "Failed to allocate pixel buffer\n");
            return 1;
        }

        Camera render_cam = {0.8f, 0.4f, 20.0f, 0.0f, 0.0f, 0.0f};

        // Initialize camera target to midpoint of all SMBHs
        if (quasar) {
            float mid_x = 0, mid_y = 0, mid_z = 0;
            int smbh_count = 0;
            for (int i = 0; i < current_n; i++) {
                if (bodies[i].type == BODY_SMBH && bodies[i].mass > 0.0) {
                    mid_x += (float)bodies[i].x;
                    mid_y += (float)bodies[i].y;
                    mid_z += (float)bodies[i].z;
                    smbh_count++;
                }
            }
            if (smbh_count > 0) {
                render_cam.target_x = mid_x / smbh_count;
                render_cam.target_y = mid_y / smbh_count;
                render_cam.target_z = mid_z / smbh_count;
            }
            // Start zoomed out for merger
            if (merger && smbh_count > 1) render_cam.distance = 60.0f;
            renderer_update_smbh(&rcfg, bodies, current_n);
        }

        float smooth_smbh_x = render_cam.target_x;
        float smooth_smbh_y = render_cam.target_y;
        float smooth_smbh_z = render_cam.target_z;

        printf("Rendering %d frames at %dx%d to %s/\n",
               render_frames, render_width, render_height, render_dir);
        printf("Substeps per frame: %d, dt: %.4f, orbit speed: %.4f rad/frame\n",
               substeps, dt, (double)orbit_speed);

        double t_start = glfwGetTime();

        for (int frame = 0; frame < render_frames; frame++) {
            /* Physics */
            for (int sub = 0; sub < substeps; sub++) {
                integrator_step(bodies, current_n, dt, G, SOFTENING, theta, pool);
                if (quasar) {
                    quasar_step(bodies, &current_n, n_alloc, &qcfg, dt);
                }
            }
            if (quasar) {
                compact_counter++;
                if (compact_counter >= compact_interval) {
                    current_n = quasar_compact(bodies, current_n);
                    compact_counter = 0;
                }
            }

            /* Camera: track SMBH(s) with dynamic distance */
            render_cam.azimuth += orbit_speed;

            // Find all SMBHs and compute midpoint + separation
            {
                float smbh_pos[2][3] = {{0}};
                int smbh_count = 0;
                for (int i = 0; i < current_n && smbh_count < 2; i++) {
                    if (bodies[i].type == BODY_SMBH && bodies[i].mass > 0.0) {
                        smbh_pos[smbh_count][0] = (float)bodies[i].x;
                        smbh_pos[smbh_count][1] = (float)bodies[i].y;
                        smbh_pos[smbh_count][2] = (float)bodies[i].z;
                        smbh_count++;
                    }
                }

                // Camera target: midpoint of all SMBHs
                float mid_x = smbh_pos[0][0], mid_y = smbh_pos[0][1], mid_z = smbh_pos[0][2];
                float smbh_sep = 0.0f;
                if (smbh_count == 2) {
                    mid_x = (smbh_pos[0][0] + smbh_pos[1][0]) * 0.5f;
                    mid_y = (smbh_pos[0][1] + smbh_pos[1][1]) * 0.5f;
                    mid_z = (smbh_pos[0][2] + smbh_pos[1][2]) * 0.5f;
                    float dx = smbh_pos[1][0] - smbh_pos[0][0];
                    float dy = smbh_pos[1][1] - smbh_pos[0][1];
                    float dz = smbh_pos[1][2] - smbh_pos[0][2];
                    smbh_sep = sqrtf(dx*dx + dy*dy + dz*dz);
                }

                // Smooth camera target
                render_cam.target_x = 0.95f * render_cam.target_x + 0.05f * mid_x;
                render_cam.target_y = 0.95f * render_cam.target_y + 0.05f * mid_y;
                render_cam.target_z = 0.95f * render_cam.target_z + 0.05f * mid_z;

                // Camera distance: based on SMBH separation + neighborhood size
                // When far apart: zoom out to see both galaxies
                // When merged: zoom in to accretion disk
                float target_dist;
                if (smbh_sep > 5.0f) {
                    // Show both galaxies: distance = separation * 1.2 + some padding
                    target_dist = smbh_sep * 1.2f + 15.0f;
                } else {
                    // Merged or close: zoom to accretion neighborhood
                    target_dist = 20.0f;
                }

                // Clamp
                if (target_dist < 8.0f) target_dist = 8.0f;
                if (target_dist > 120.0f) target_dist = 120.0f;

                // Smooth camera distance
                render_cam.distance = 0.97f * render_cam.distance + 0.03f * target_dist;
            }

            /* Render to FBO */
            if (quasar) {
                renderer_update_smbh(&rcfg, bodies, current_n);
                // Smooth SMBH render position to eliminate event horizon jitter
                if (frame == 0) {
                    smooth_smbh_x = rcfg.smbh_x;
                    smooth_smbh_y = rcfg.smbh_y;
                    smooth_smbh_z = rcfg.smbh_z;
                } else {
                    smooth_smbh_x = 0.9f * smooth_smbh_x + 0.1f * rcfg.smbh_x;
                    smooth_smbh_y = 0.9f * smooth_smbh_y + 0.1f * rcfg.smbh_y;
                    smooth_smbh_z = 0.9f * smooth_smbh_z + 0.1f * rcfg.smbh_z;
                }
                rcfg.smbh_x = smooth_smbh_x;
                rcfg.smbh_y = smooth_smbh_y;
                rcfg.smbh_z = smooth_smbh_z;
            }
            renderer_draw(bodies, current_n, &render_cam,
                          render_width, render_height, &rcfg);

            /* If HDR pipeline wrote to default FB (composite pass), read from it.
               Otherwise the scene is in the default FB already. */
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glReadPixels(0, 0, render_width, render_height,
                         GL_RGBA, GL_UNSIGNED_BYTE, pixels);

            /* Save frame */
            char path[1024];
            snprintf(path, sizeof(path), "%s/frame_%06d.ppm", render_dir, frame);
            save_ppm(path, render_width, render_height, pixels);

            /* Progress */
            if ((frame + 1) % 10 == 0 || frame == render_frames - 1) {
                double elapsed = glfwGetTime() - t_start;
                double fps = (frame + 1) / elapsed;
                double eta = (render_frames - frame - 1) / fps;
                printf("\r  frame %d/%d  (%.1f fps, ETA %.0fs)   ",
                       frame + 1, render_frames, fps, eta);
                fflush(stdout);
            }

            glfwPollEvents();
            if (glfwWindowShouldClose(window)) break;
        }

        double total = glfwGetTime() - t_start;
        printf("\nDone. %d frames in %.1fs (%.1f fps avg)\n",
               render_frames, total, render_frames / total);
        printf("Convert to video:\n  ffmpeg -framerate 60 -i %s/frame_%%06d.ppm "
               "-c:v libx264 -pix_fmt yuv420p output.mp4\n", render_dir);

        free(pixels);
        glDeleteFramebuffers(1, &render_fbo);
        glDeleteTextures(1, &render_color_tex);
        glDeleteRenderbuffers(1, &render_depth_rbo);
    } else {
        /* ---- Interactive mode ---- */
        printf("Simulation running. Press Space to pause, Q to quit, R to reset camera.\n");

        double fps_time = glfwGetTime();
        int fps_frames = 0;

        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();

            if (!paused) {
                for (int sub = 0; sub < substeps; sub++) {
                    integrator_step(bodies, current_n, dt, G, SOFTENING, theta, pool);
                    if (quasar) {
                        quasar_step(bodies, &current_n, n_alloc, &qcfg, dt);
                    }
                }
                if (quasar) {
                    compact_counter++;
                    if (compact_counter >= compact_interval) {
                        current_n = quasar_compact(bodies, current_n);
                        compact_counter = 0;
                    }
                }
            }

            int w, h;
            glfwGetFramebufferSize(window, &w, &h);
            if (quasar) {
                renderer_update_smbh(&rcfg, bodies, current_n);
            }
            renderer_draw(bodies, current_n, &camera, w, h, &rcfg);
            glfwSwapBuffers(window);

            fps_frames++;
            double now = glfwGetTime();
            if (now - fps_time >= 1.0) {
                char title[128];
                snprintf(title, sizeof(title), "cosmosim | %d bodies | %.0f FPS%s",
                         current_n, fps_frames / (now - fps_time),
                         paused ? " [PAUSED]" : "");
                glfwSetWindowTitle(window, title);
                fps_frames = 0;
                fps_time = now;
            }
        }
    }

    renderer_cleanup();
    free(bodies);
    free(pool);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
