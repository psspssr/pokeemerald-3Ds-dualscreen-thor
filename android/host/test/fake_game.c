/*
 * Stand-in for origin's main() to exercise the app and ctr_host.c without the
 * real game. Draws the layout's two screen rectangles, one indicator square
 * per KEY_* bit (lit while held) across the top screen, the circle pad
 * position as a dot on the top screen and the touch point on the bottom one.
 * Holding START+SELECT for a second returns from main, like origin.
 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/log.h>
#include <android/native_window.h>
#include <stdlib.h>
#include <time.h>

#include "ctr_host.h"

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "emerald-fake", __VA_ARGS__)

#define KEY_SELECT (1u << 2)
#define KEY_START (1u << 3)
#define KEY_TOUCH (1u << 20)

static EGLDisplay sDisplay = EGL_NO_DISPLAY;
static EGLConfig sConfig;
static EGLContext sContext = EGL_NO_CONTEXT;
typedef struct
{
    EGLSurface surface;
    struct ANativeWindow *window;
    uint32_t generation;
} FakeWindow;

static FakeWindow sWindows[CTR_HOST_MAX_WINDOWS];

static long long NowMs(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int InitEgl(void)
{
    static const EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE
    };
    static const EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLint count = 0;

    sDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(sDisplay, NULL, NULL))
        return 0;
    if (!eglChooseConfig(sDisplay, configAttribs, &sConfig, 1, &count) || count < 1)
        return 0;
    sContext = eglCreateContext(sDisplay, sConfig, EGL_NO_CONTEXT, contextAttribs);
    return sContext != EGL_NO_CONTEXT;
}

static void DropSurface(FakeWindow *w)
{
    if (w->surface != EGL_NO_SURFACE)
    {
        eglMakeCurrent(sDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(sDisplay, w->surface);
        w->surface = EGL_NO_SURFACE;
    }
    if (w->window)
    {
        ANativeWindow_release(w->window);
        w->window = NULL;
    }
}

/* Recreates the EGL surface when the host replaced the window. */
static int UpdateSurface(int index)
{
    FakeWindow *w = &sWindows[index];
    uint32_t generation;
    struct ANativeWindow *window = CtrHost_AcquireWindowAt(index, &generation);

    if (generation == w->generation)
    {
        if (window)
            ANativeWindow_release(window);
        return w->surface != EGL_NO_SURFACE;
    }
    w->generation = generation;
    DropSurface(w);
    if (!window)
    {
        LOG("window %d removed (generation %u)", index, generation);
        return 0;
    }
    w->window = window;
    w->surface = eglCreateWindowSurface(sDisplay, sConfig, window, NULL);
    if (w->surface == EGL_NO_SURFACE)
    {
        LOG("EGL surface failed: 0x%x", eglGetError());
        DropSurface(w);
        return 0;
    }
    LOG("window %d: %dx%d (generation %u)", index, ANativeWindow_getWidth(window),
        ANativeWindow_getHeight(window), generation);
    return 1;
}

static void Fill(int winH, int x, int y, int w, int h, unsigned rgb)
{
    if (w <= 0 || h <= 0)
        return;
    glScissor(x, winH - y - h, w, h);
    glClearColor(((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

static void Draw(int index, const CtrHostLayout *layout, const CtrHostInput *input)
{
    EGLint winW = 0, winH = 0;
    EGLSurface surface = sWindows[index].surface;
    const CtrHostRect *t = &layout->top, *b = &layout->bottom;

    if (!eglMakeCurrent(sDisplay, surface, surface, sContext))
        return;
    /* Two displays at different refresh rates: never block on one's vsync. */
    eglSwapInterval(sDisplay, index == 0 ? 1 : 0);
    eglQuerySurface(sDisplay, surface, EGL_WIDTH, &winW);
    eglQuerySurface(sDisplay, surface, EGL_HEIGHT, &winH);
    glViewport(0, 0, winW, winH);
    glEnable(GL_SCISSOR_TEST);
    Fill(winH, 0, 0, winW, winH, layout->background);

    if (layout->topWindow == index && t->w > 0)
    {
        int cell = t->w / 34;

        Fill(winH, t->x, t->y, t->w, t->h, 0x1E4AA8);
        for (int bit = 0; bit < 32; bit++)
        {
            unsigned on = input->keys & (1u << bit);
            Fill(winH, t->x + cell + bit * cell, t->y + cell, cell - 2, cell - 2, on ? 0xFFE000 : 0x10306A);
        }
        /* Circle pad: centre of the top screen, +-156 maps to +-h/3. */
        int cx = t->x + t->w / 2 + input->circleX * (t->h / 3) / 156;
        int cy = t->y + t->h / 2 - input->circleY * (t->h / 3) / 156;
        Fill(winH, cx - cell / 2, cy - cell / 2, cell, cell, 0xFFFFFF);
    }
    if (layout->bottomWindow == index && b->w > 0)
    {
        Fill(winH, b->x, b->y, b->w, b->h, 0x2E8B57);
        if (input->keys & KEY_TOUCH)
        {
            int dot = b->w / 20 + 2;
            int px = b->x + (input->touchX * b->w + b->w / 2) / 320;
            int py = b->y + (input->touchY * b->h + b->h / 2) / 240;
            Fill(winH, px - dot / 2, py - dot / 2, dot, dot, 0xFF3030);
        }
    }
    eglSwapBuffers(sDisplay, surface);
}

int main(void)
{
    CtrHostInput last = { 0, 0, 0, 0, 0 };
    long long chordStart = 0;

    for (int i = 0; i < CTR_HOST_MAX_WINDOWS; i++)
    {
        sWindows[i].surface = EGL_NO_SURFACE;
        sWindows[i].generation = (uint32_t)-1;
    }
    LOG("fake game start: romfs=%s sdmc=%s", CtrHost_RomfsDir(), CtrHost_SdmcDir());
    if (!InitEgl())
    {
        LOG("EGL init failed");
        return 1;
    }
    for (;;)
    {
        CtrHostInput input;
        CtrHostLayout layout;
        CtrHostState state = CtrHost_GetState();

        if (state == CTR_HOST_PAUSED)
        {
            LOG("paused");
            state = CtrHost_WaitWhilePaused();
            LOG("resumed: state %d", state);
        }
        if (state == CTR_HOST_EXITING)
            break;
        CtrHost_GetInput(&input);
        if (input.keys != last.keys || input.circleX != last.circleX || input.circleY != last.circleY
            || ((input.keys & KEY_TOUCH) && (input.touchX != last.touchX || input.touchY != last.touchY)))
        {
            LOG("input keys=0x%08x circle=%d,%d touch=%u,%u", input.keys, input.circleX, input.circleY,
                input.touchX, input.touchY);
            last = input;
        }
        if ((input.keys & (KEY_START | KEY_SELECT)) == (KEY_START | KEY_SELECT))
        {
            if (!chordStart)
                chordStart = NowMs();
            else if (NowMs() - chordStart >= 1000)
            {
                LOG("START+SELECT: exit");
                break;
            }
        }
        else
            chordStart = 0;
        CtrHost_GetLayout(&layout);
        int drawn = 0;
        for (int i = 0; i < CTR_HOST_MAX_WINDOWS; i++)
            if (UpdateSurface(i))
            {
                Draw(i, &layout, &input);
                drawn = 1;
            }
        if (!drawn)
        {
            struct timespec ts = { 0, 16000000 };
            nanosleep(&ts, NULL);
        }
    }
    LOG("fake game exit");
    for (int i = 0; i < CTR_HOST_MAX_WINDOWS; i++)
        DropSurface(&sWindows[i]);
    if (sContext != EGL_NO_CONTEXT)
        eglDestroyContext(sDisplay, sContext);
    eglTerminate(sDisplay);
    return 0;
}
