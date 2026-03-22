#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "body.h"
#include "quadtree.h"
#include "integrator.h"
#include "renderer.h"
#include "initial_conditions.h"

#define DEFAULT_N       20000
#define G               1.0
#define SOFTENING       0.5
#define THETA           0.5
#define DT              0.005
#define SUBSTEPS        2
#define WINDOW_WIDTH    1280
#define WINDOW_HEIGHT   800

static Camera camera = {0.0f, 0.0f, 50.0f};
static int paused = 0;
static double last_cursor_x, last_cursor_y;
static int dragging = 0;

static void scroll_callback(GLFWwindow *window, double xoff, double yoff)
{
    (void)window;
    (void)xoff;
    camera.zoom *= (float)(1.0 - 0.1 * yoff);
    if (camera.zoom < 0.01f) camera.zoom = 0.01f;
    if (camera.zoom > 10000.0f) camera.zoom = 10000.0f;
}

static void mouse_button_callback(GLFWwindow *window, int button, int action, int mods)
{
    (void)mods;
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            dragging = 1;
            glfwGetCursorPos(window, &last_cursor_x, &last_cursor_y);
        } else {
            dragging = 0;
        }
    }
}

static void cursor_pos_callback(GLFWwindow *window, double xpos, double ypos)
{
    if (!dragging) return;

    int w, h;
    glfwGetWindowSize(window, &w, &h);

    double dx = xpos - last_cursor_x;
    double dy = ypos - last_cursor_y;
    last_cursor_x = xpos;
    last_cursor_y = ypos;

    float aspect = (float)w / (float)h;
    camera.offset_x -= (float)(dx / w * 2.0 * camera.zoom * aspect);
    camera.offset_y += (float)(dy / h * 2.0 * camera.zoom);
}

static void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    (void)scancode;
    (void)mods;
    if (action != GLFW_PRESS) return;

    if (key == GLFW_KEY_SPACE) paused = !paused;
    if (key == GLFW_KEY_Q || key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(window, 1);
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
    double dt = DT;
    double theta = THETA;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            n = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--merger") == 0) {
            merger = 1;
        } else if (strcmp(argv[i], "-dt") == 0 && i + 1 < argc) {
            dt = atof(argv[++i]);
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            theta = atof(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: cosmosim [options]\n"
                   "  -n <count>    Number of bodies (default %d)\n"
                   "  -m, --merger  Galaxy merger mode\n"
                   "  -dt <value>   Timestep (default %.4f)\n"
                   "  -t <theta>    Barnes-Hut opening angle (default %.1f)\n"
                   "\nControls:\n"
                   "  Scroll        Zoom in/out\n"
                   "  Left-drag     Pan\n"
                   "  Space         Pause/resume\n"
                   "  Q/Esc         Quit\n", DEFAULT_N, DT, THETA);
            return 0;
        }
    }

    if (n < 2) n = 2;

    printf("cosmosim: %d bodies, %s mode, dt=%.4f, theta=%.2f\n",
           n, merger ? "merger" : "galaxy", dt, theta);

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

    if (renderer_init() != 0) {
        fprintf(stderr, "Failed to initialize renderer\n");
        glfwTerminate();
        return 1;
    }

    // Allocate bodies and quadtree pool
    Body *bodies = calloc(n, sizeof(Body));
    QuadTreeNode *pool = malloc(8 * n * sizeof(QuadTreeNode)); // generous pool

    if (!bodies || !pool) {
        fprintf(stderr, "Failed to allocate memory\n");
        return 1;
    }

    // Generate initial conditions
    if (merger) {
        generate_merger(bodies, n, 60.0, 0.3);
    } else {
        generate_spiral_galaxy(bodies, n, 0.0, 0.0, (double)n * 2.0, 30.0, 0.0, 0.0);
    }

    // Compute initial accelerations
    integrator_init_accelerations(bodies, n, G, SOFTENING, theta, pool);

    printf("Simulation running. Press Space to pause, Q to quit.\n");

    // FPS counter
    double fps_time = glfwGetTime();
    int fps_frames = 0;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (!paused) {
            for (int sub = 0; sub < SUBSTEPS; sub++) {
                integrator_step(bodies, n, dt, G, SOFTENING, theta, pool);
            }
        }

        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        renderer_draw(bodies, n, &camera, w, h);
        glfwSwapBuffers(window);

        // FPS display
        fps_frames++;
        double now = glfwGetTime();
        if (now - fps_time >= 1.0) {
            char title[128];
            snprintf(title, sizeof(title), "cosmosim | %d bodies | %.0f FPS%s",
                     n, fps_frames / (now - fps_time), paused ? " [PAUSED]" : "");
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
