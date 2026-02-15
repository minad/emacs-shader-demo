#define GL_GLEXT_PROTOTYPES
#include <emacs-module.h>
#include <stdbool.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <EGL/egl.h>
#include <string.h>
#include <malloc.h>

int plugin_is_GPL_compatible;
enum { WIDTH = 300, HEIGHT = 300 };
static EGLDisplay egl_display = EGL_NO_DISPLAY;
static EGLSurface egl_surface = EGL_NO_SURFACE;
static EGLContext egl_context = EGL_NO_CONTEXT;
static GLuint gl_prog = 0;
static GLuint gl_vbo = 0;
static GLint time_loc = -1;
static bool inited = false;

static const char *vs_src =
    "attribute vec2 pos;\
    varying vec2 vUV;\
    void main() {\
        vUV = pos * 0.5 + 0.5;\
        gl_Position = vec4(pos, 0.0, 1.0);\
    }";

static emacs_value make_string(emacs_env* env, const char* str) {
    return env->make_string(env, str, strlen(str));
}

static GLuint make_shader(emacs_env *env, GLenum type, const char *src, emacs_value* err) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    GLint ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        glDeleteShader(shader);
        *err = make_string(env, log);
        return 0;
    }
    return shader;
}

static GLuint make_prog(emacs_env *env, const char *fs_src, emacs_value* err) {
    GLuint vs = make_shader(env, GL_VERTEX_SHADER, vs_src, err);
    if (!vs)
        return 0;

    GLuint fs = make_shader(env, GL_FRAGMENT_SHADER, fs_src, err);
    if (!fs) {
        glDeleteShader(vs);
        return 0;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        glDeleteProgram(prog);
        *err = make_string(env, log);
        return 0;
    }

    return prog;
}

static bool init(void) {
    if (inited)
        return true;

    egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_display == EGL_NO_DISPLAY)
        return false;

    EGLint major, minor;
    if (!eglInitialize(egl_display, &major, &minor))
        return false;

    eglBindAPI(EGL_OPENGL_ES_API);

    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    EGLConfig config;
    EGLint nconfig;
    if (!eglChooseConfig(egl_display, config_attribs, &config, 1, &nconfig) || !nconfig)
        return false;

    EGLint pbuffer_attribs[] = {
        EGL_WIDTH,  WIDTH,
        EGL_HEIGHT, HEIGHT,
        EGL_NONE
    };
    egl_surface = eglCreatePbufferSurface(egl_display, config, pbuffer_attribs);
    if (egl_surface == EGL_NO_SURFACE)
        return false;

    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    egl_context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT, context_attribs);
    if (egl_context == EGL_NO_CONTEXT)
        return false;

    eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);

    static const float vertices[] = {
        -1, -1,
        1, -1,
        -1,  1,
        1,  1,
    };
    glGenBuffers(1, &gl_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, gl_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glViewport(0, 0, WIDTH, HEIGHT);

    return inited = true;
}

static emacs_value load_shader(emacs_env *env, ptrdiff_t nargs,
                               emacs_value args[], void *data) {
    if (!init())
        return make_string(env, "Failed to initialize EGL");

    ptrdiff_t len = 0;
    char *str = 0;
    if (!env->copy_string_contents(env, args[0], NULL, &len) ||
        !(str = malloc(len)) ||
        !env->copy_string_contents(env, args[0], str, &len))
        return make_string(env, "Copying string failed");

    emacs_value err;
    GLuint prog = make_prog(env, str, &err);
    free(str);

    if (!prog)
        return err;

    if (gl_prog)
        glDeleteProgram(gl_prog);

    gl_prog = prog;
    time_loc = glGetUniformLocation(gl_prog, "t");

    return make_string(env, "{SHADER}");
}

static emacs_value render(emacs_env *env, ptrdiff_t nargs,
                          emacs_value args[], void *data) {
    uint32_t *pixel = env->canvas_pixel(env, args[0]);
    float time = env->extract_float(env, args[1]);

    if (!pixel || !gl_prog)
        return env->intern(env, "nil");

    eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);

    glUseProgram(gl_prog);
    glUniform1f(time_loc, time);

    GLint pos = glGetAttribLocation(gl_prog, "pos");
    glBindBuffer(GL_ARRAY_BUFFER, gl_vbo);
    glEnableVertexAttribArray(pos);
    glVertexAttribPointer(pos, 2, GL_FLOAT, GL_FALSE, 0, 0);

    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glFinish();

    glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixel);

    env->canvas_refresh(env, args[0]);

    return env->intern(env, "nil");
}

int emacs_module_init(struct emacs_runtime *rt) {
    if ((size_t)rt->size < sizeof(*rt))
        return 1;
    emacs_env *env = rt->get_environment(rt);
    if ((size_t)env->size < sizeof(*env))
        return 2;
    env->funcall(env, env->intern(env, "defalias"), 2,
                 (emacs_value[]){
                     env->intern(env, "ob-shader-load"),
                     env->make_function(env, 1, 1, load_shader, NULL, NULL)
                 });
    env->funcall(env, env->intern(env, "defalias"), 2,
                 (emacs_value[]){
                     env->intern(env, "ob-shader-render"),
                     env->make_function(env, 2, 2, render, NULL, NULL)
                 });
    return 0;
}
