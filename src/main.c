#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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
#define SOFTENING       0.5
#define THETA           0.5
#define DT              0.005
#define SUBSTEPS        2
#define WINDOW_WIDTH    1280
#define WINDOW_HEIGHT   800
#define DEFAULT_SMBH_MASS_FRAC     0.05
#define DEFAULT_ACCRETION_RADIUS   3.0
#define DEFAULT_JET_SPEED          20.0
#define DEFAULT_FEEDBACK_STRENGTH  1.0

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
                   "\nControls:\n"
                   "  Scroll        Zoom in/out\n"
                   "  Left-drag     Orbit camera\n"
                   "  Right-drag    Pan\n"
                   "  Space         Pause/resume\n"
                   "  R             Reset camera\n"
                   "  Q/Esc         Quit\n",
                   DEFAULT_N, DT, THETA,
                   DEFAULT_SMBH_MASS_FRAC, DEFAULT_ACCRETION_RADIUS,
                   DEFAULT_JET_SPEED, DEFAULT_FEEDBACK_STRENGTH);
            return 0;
        }
    }

    if (n < 2) n = 2;
    int substeps = high_fidelity ? 8 : SUBSTEPS;

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

    GLFWwindow *window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "cosmosim", NULL, NULL);
    if (!window) {
        fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // vsync

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
    rcfg.hdr_enabled = quasar;
    rcfg.bloom_iterations = high_fidelity ? 4 : 2;
    rcfg.lensing_samples = high_fidelity ? 4 : 1;

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

    printf("Simulation running. Press Space to pause, Q to quit, R to reset camera.\n");

    // FPS counter
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

        // FPS display
        fps_frames++;
        double now = glfwGetTime();
        if (now - fps_time >= 1.0) {
            char title[128];
            snprintf(title, sizeof(title), "cosmosim | %d bodies | %.0f FPS%s",
                     current_n, fps_frames / (now - fps_time), paused ? " [PAUSED]" : "");
            glfwSetWindowTitle(window, title);
            fps_frames = 0;
            fps_time = now;
        }
    }

    renderer_cleanup();
    free(bodies);
    free(pool);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
