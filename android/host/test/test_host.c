/* Test the real ctr_host.c using real pthreads. Only Android windows/logging
 * and the JNI-to-UI callback are replaced; no production state is inspected. */
#include <android/native_window.h>
#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include "ctr_host.h"

struct ANativeWindow { int id; };

void ANativeWindow_acquire(ANativeWindow *window) { (void)window; }
void ANativeWindow_release(ANativeWindow *window) { (void)window; }
int __android_log_print(int priority, const char *tag, const char *format, ...)
{ (void)priority; (void)tag; (void)format; return 0; }
void CtrHost_NotifyGameExit(int status) { (void)status; assert(!"unexpected game launch"); }

static struct timespec Deadline(void)
{
    struct timespec deadline;
    assert(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
    deadline.tv_sec += 2;
    return deadline;
}

typedef enum { DEFERRED, MISSING, IMMEDIATE, ASYNC_IMMEDIATE, PAUSE_DURING_DISPATCH } UiMode;
static pthread_mutex_t uiLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t uiCond = PTHREAD_COND_INITIALIZER;
static UiMode uiMode;
static bool uiAllow;
static unsigned uiCalls;
static uint32_t uiRequest;

typedef struct { uint32_t request; bool allow; } Answer;
static void *AnswerImmediately(void *data)
{
    Answer *answer = data;
    assert(CtrHost_IsShinyFleePending(answer->request));
    CtrHost_AnswerShinyFlee(answer->request, answer->allow);
    return NULL;
}

bool CtrHost_ShowShinyFleePrompt(uint32_t request)
{
    /* Reentrant host queries here also prove the UI callback runs without
     * the host mutex held. All published request IDs must be live/nonzero. */
    assert(request && CtrHost_IsShinyFleePending(request));
    assert(pthread_mutex_lock(&uiLock) == 0);
    UiMode mode = uiMode;
    Answer answer = {request, uiAllow};
    uiRequest = request;
    ++uiCalls;
    assert(pthread_cond_broadcast(&uiCond) == 0);
    assert(pthread_mutex_unlock(&uiLock) == 0);
    if (mode == MISSING) return false;
    if (mode == IMMEDIATE) AnswerImmediately(&answer);
    else if (mode == ASYNC_IMMEDIATE)
    {
        pthread_t uiThread;
        assert(pthread_create(&uiThread, NULL, AnswerImmediately, &answer) == 0);
        /* Force the UI's reply to arrive before ConfirmShinyFlee starts
         * waiting. Losing this wake-up would hang the caller. */
        assert(pthread_join(uiThread, NULL) == 0);
    }
    else if (mode == PAUSE_DURING_DISPATCH)
        CtrHost_SetState(CTR_HOST_PAUSED);
    return true;
}

static unsigned ConfigureUi(UiMode mode, bool allow)
{
    assert(pthread_mutex_lock(&uiLock) == 0);
    uiMode = mode;
    uiAllow = allow;
    unsigned previous = uiCalls;
    assert(pthread_mutex_unlock(&uiLock) == 0);
    return previous;
}

static uint32_t WaitForPrompt(unsigned previous)
{
    struct timespec deadline = Deadline();
    assert(pthread_mutex_lock(&uiLock) == 0);
    while (uiCalls == previous)
        assert(pthread_cond_timedwait(&uiCond, &uiLock, &deadline) == 0);
    assert(uiCalls == previous + 1);
    uint32_t request = uiRequest;
    assert(pthread_mutex_unlock(&uiLock) == 0);
    return request;
}

typedef struct {
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool done, allow;
} PromptCall;

static void *Confirm(void *data)
{
    PromptCall *call = data;
    bool allow = CtrHost_ConfirmShinyFlee();
    assert(pthread_mutex_lock(&call->lock) == 0);
    call->allow = allow;
    call->done = true;
    assert(pthread_cond_signal(&call->cond) == 0);
    assert(pthread_mutex_unlock(&call->lock) == 0);
    return NULL;
}

static void StartPrompt(PromptCall *call)
{
    *call = (PromptCall){0};
    assert(pthread_mutex_init(&call->lock, NULL) == 0);
    assert(pthread_cond_init(&call->cond, NULL) == 0);
    assert(pthread_create(&call->thread, NULL, Confirm, call) == 0);
}

static bool FinishPrompt(PromptCall *call)
{
    struct timespec deadline = Deadline();
    assert(pthread_mutex_lock(&call->lock) == 0);
    while (!call->done)
        assert(pthread_cond_timedwait(&call->cond, &call->lock, &deadline) == 0);
    bool allow = call->allow;
    assert(pthread_mutex_unlock(&call->lock) == 0);
    assert(pthread_join(call->thread, NULL) == 0);
    assert(pthread_cond_destroy(&call->cond) == 0);
    assert(pthread_mutex_destroy(&call->lock) == 0);
    return allow;
}

static void TestDefaultsAndConfiguration(void)
{
    assert(CtrHost_GameSpeed() == 1 && CtrHost_ShinyMultiplier() == 1);
    assert(!CtrHost_SharedExperience() && !CtrHost_SaveBackups() && !CtrHost_ProtectShinies());
    assert(!CtrHost_IsShinyFleePending(0) && !CtrHost_IsShinyFleePending(1));
    for (unsigned speed = 1; speed <= 8; ++speed)
        for (unsigned shiny = 1; shiny <= 64; shiny *= 2)
        {
            CtrHost_SetGameplayOptions(speed, shiny, true, true, true);
            assert(CtrHost_GameSpeed() == speed && CtrHost_ShinyMultiplier() == shiny);
            assert(CtrHost_SharedExperience() && CtrHost_SaveBackups() && CtrHost_ProtectShinies());
        }
    const unsigned badSpeed[] = {0, 9, UINT_MAX};
    const unsigned badShiny[] = {0, 3, 6, 63, 65, UINT_MAX};
    for (unsigned i = 0; i < sizeof(badSpeed) / sizeof(*badSpeed); ++i)
    {
        CtrHost_SetGameplayOptions(badSpeed[i], 8, false, false, false);
        assert(CtrHost_GameSpeed() == 1 && CtrHost_ShinyMultiplier() == 8);
    }
    for (unsigned i = 0; i < sizeof(badShiny) / sizeof(*badShiny); ++i)
    {
        CtrHost_SetGameplayOptions(4, badShiny[i], true, false, true);
        assert(CtrHost_GameSpeed() == 4 && CtrHost_ShinyMultiplier() == 1);
        assert(CtrHost_SharedExperience() && !CtrHost_SaveBackups() && CtrHost_ProtectShinies());
    }
    CtrHost_SetGameplayOptions(8, 16, true, true, true);
    CtrHost_SetState(CTR_HOST_PAUSED);
    assert(CtrHost_GameSpeed() == 1 && CtrHost_ShinyMultiplier() == 16);
    assert(CtrHost_SharedExperience() && CtrHost_SaveBackups() && CtrHost_ProtectShinies());
    CtrHost_SetState(CTR_HOST_RUNNING);
    assert(CtrHost_GameSpeed() == 8);
    CtrHost_SetGameplayOptions(1, 1, false, false, false);
    assert(!CtrHost_SharedExperience() && !CtrHost_SaveBackups() && !CtrHost_ProtectShinies());
}

static void TestBottomMenuExpansion(void)
{
    struct ANativeWindow mainWindow = {0}, secondWindow = {1};
    CtrHostLayout observed;
    CtrHostLayout layout = {
        .top = {0, 0, 1920, 1080}, .bottom = {0, 0, 1240, 1080},
        .topWindow = 0, .bottomWindow = 1, .expandBottomMenus = true,
    };

    assert(!CtrHost_ExpandBottomMenus());
    assert(CtrHost_BottomMenuContent() == CTR_HOST_BOTTOM_ORIGINAL);
    assert(CtrHost_PresentedBottomMenuContent() == CTR_HOST_BOTTOM_ORIGINAL);
    CtrHost_SetBottomMenuContent(CTR_HOST_BOTTOM_FIELD);
    CtrHost_SetLayout(&layout);
    assert(!CtrHost_ExpandBottomMenus());
    assert(CtrHost_BottomMenuContent() == CTR_HOST_BOTTOM_ORIGINAL);
    CtrHost_SetWindowAt(0, &mainWindow);
    assert(!CtrHost_ExpandBottomMenus());
    CtrHost_SetWindowAt(1, &secondWindow);
    assert(CtrHost_ExpandBottomMenus());
    CtrHost_GetLayout(&observed);
    assert(observed.expandBottomMenus);
    assert(CtrHost_BottomMenuContent() == CTR_HOST_BOTTOM_FIELD);
    /* Pending content may change during skipped frames; touch must continue
     * using the old image until the GPU acknowledges an actual swap. */
    CtrHost_SetPresentedBottomMenuContent(CtrHost_BottomMenuContent());
    CtrHost_SetBottomMenuContent(CTR_HOST_BOTTOM_WHOLE);
    assert(CtrHost_BottomMenuContent() == CTR_HOST_BOTTOM_WHOLE);
    assert(CtrHost_PresentedBottomMenuContent() == CTR_HOST_BOTTOM_FIELD);
    CtrHost_SetPresentedBottomMenuContent(CtrHost_BottomMenuContent());
    assert(CtrHost_PresentedBottomMenuContent() == CTR_HOST_BOTTOM_WHOLE);

    layout.topWindow = 1;
    layout.bottomWindow = 0;
    CtrHost_SetLayout(&layout);
    assert(CtrHost_ExpandBottomMenus());
    /* A disappearing surface disables expansion before the UI relayout. */
    CtrHost_SetWindowAt(1, NULL);
    assert(!CtrHost_ExpandBottomMenus());
    CtrHost_GetLayout(&observed);
    assert(!observed.expandBottomMenus);
    assert(CtrHost_BottomMenuContent() == CTR_HOST_BOTTOM_ORIGINAL);
    assert(CtrHost_PresentedBottomMenuContent() == CTR_HOST_BOTTOM_WHOLE);
    CtrHost_SetWindowAt(1, &secondWindow);
    assert(CtrHost_ExpandBottomMenus());

    layout.expandBottomMenus = false; /* Fit, with both displays attached. */
    CtrHost_SetLayout(&layout);
    assert(!CtrHost_ExpandBottomMenus());
    assert(CtrHost_BottomMenuContent() == CTR_HOST_BOTTOM_ORIGINAL);
    CtrHost_SetPresentedBottomMenuContent(CtrHost_BottomMenuContent());
    assert(CtrHost_PresentedBottomMenuContent() == CTR_HOST_BOTTOM_ORIGINAL);
    layout.expandBottomMenus = true;
    layout.topWindow = layout.bottomWindow = 0;
    CtrHost_SetLayout(&layout);
    assert(!CtrHost_ExpandBottomMenus());
    layout.topWindow = -1;
    layout.bottomWindow = CTR_HOST_MAX_WINDOWS;
    CtrHost_SetLayout(&layout);
    assert(!CtrHost_ExpandBottomMenus());
    layout.topWindow = 0;
    layout.bottomWindow = 1;
    layout.bottom.h = 0;
    CtrHost_SetLayout(&layout);
    assert(!CtrHost_ExpandBottomMenus());

    /* An older zero-initialised layout never enables menu expansion. */
    layout = (CtrHostLayout){0};
    CtrHost_SetLayout(&layout);
    assert(!CtrHost_ExpandBottomMenus());
    CtrHost_SetWindowAt(0, NULL);
    CtrHost_SetWindowAt(1, NULL);
    CtrHost_SetBottomMenuContent((CtrHostBottomMenuContent)99);
    CtrHost_SetPresentedBottomMenuContent((CtrHostBottomMenuContent)-1);
    assert(CtrHost_BottomMenuContent() == CTR_HOST_BOTTOM_ORIGINAL);
    assert(CtrHost_PresentedBottomMenuContent() == CTR_HOST_BOTTOM_ORIGINAL);
}

static void TestVoxelAAConfiguration(void)
{
    CtrHostLayout layout = {0}, observed;
    assert(CtrHost_VoxelAACapabilities() == -1);
    CtrHost_GetLayout(&observed);
    assert(observed.voxelAASamples == 0);
    const int requests[] = {0, 2, 4, 1, 3, 8, -1, INT_MAX};
    for (unsigned i = 0; i < sizeof(requests) / sizeof(*requests); ++i)
    {
        layout.voxelAASamples = requests[i];
        CtrHost_SetLayout(&layout);
        CtrHost_GetLayout(&observed);
        assert(observed.voxelAASamples == (requests[i] == 2 || requests[i] == 4 ? requests[i] : 0));
    }
    layout.voxelAASamples = 4;
    CtrHost_SetLayout(&layout);
    CtrHost_SetVoxelAACapabilities(CTR_HOST_VOXEL_AA_2X | CTR_HOST_VOXEL_AA_4X);
    assert(CtrHost_VoxelAACapabilities() == 3);
    CtrHost_SetVoxelAACapabilities(CTR_HOST_VOXEL_AA_2X);
    assert(CtrHost_VoxelAACapabilities() == 1);
    CtrHost_GetLayout(&observed);
    assert(observed.voxelAASamples == 4); /* Capability fallback never rewrites the request. */
    CtrHost_SetVoxelAACapabilities(4);
    assert(CtrHost_VoxelAACapabilities() == 0);
    CtrHost_SetVoxelAACapabilities(-2);
    assert(CtrHost_VoxelAACapabilities() == -1);
    layout.voxelAASamples = 0;
    CtrHost_SetLayout(&layout);
}

static void TestAcceptRefuseAndStaleRequests(void)
{
    PromptCall call;
    unsigned previous = ConfigureUi(DEFERRED, false);
    StartPrompt(&call);
    uint32_t first = WaitForPrompt(previous);
    assert(CtrHost_IsShinyFleePending(first));
    CtrHost_AnswerShinyFlee(first, false);
    assert(!FinishPrompt(&call) && !CtrHost_IsShinyFleePending(first));

    previous = ConfigureUi(DEFERRED, false);
    StartPrompt(&call);
    uint32_t second = WaitForPrompt(previous);
    assert(second && second != first);
    CtrHost_AnswerShinyFlee(first, true);
    CtrHost_AnswerShinyFlee(0, true);
    assert(CtrHost_IsShinyFleePending(second));
    CtrHost_AnswerShinyFlee(second, true);
    assert(FinishPrompt(&call) && !CtrHost_IsShinyFleePending(second));
    // A repeated old answer after completion cannot resurrect a request.
    CtrHost_AnswerShinyFlee(second, true);
    assert(!CtrHost_IsShinyFleePending(second));
}

static void TestMissingUiAndImmediateAnswers(void)
{
    PromptCall call;
    unsigned previous = ConfigureUi(MISSING, false);
    StartPrompt(&call);
    uint32_t request = WaitForPrompt(previous);
    assert(!FinishPrompt(&call) && !CtrHost_IsShinyFleePending(request));
    for (unsigned i = 0; i < 32; ++i)
    {
        bool allow = (i & 1) != 0;
        previous = ConfigureUi(i & 2 ? IMMEDIATE : ASYNC_IMMEDIATE, allow);
        StartPrompt(&call);
        request = WaitForPrompt(previous);
        assert(FinishPrompt(&call) == allow);
        assert(!CtrHost_IsShinyFleePending(request));
    }
}

static void TestPauseCancelsPrompt(void)
{
    PromptCall call;
    unsigned previous = ConfigureUi(DEFERRED, false);
    StartPrompt(&call);
    uint32_t canceled = WaitForPrompt(previous);
    CtrHost_SetState(CTR_HOST_PAUSED);
    CtrHost_AnswerShinyFlee(canceled, true);
    assert(!FinishPrompt(&call) && !CtrHost_IsShinyFleePending(canceled));
    // Paused callers must fail closed without posting another UI prompt.
    previous = ConfigureUi(DEFERRED, false);
    StartPrompt(&call);
    assert(!FinishPrompt(&call));
    assert(ConfigureUi(DEFERRED, false) == previous);
    CtrHost_SetState(CTR_HOST_RUNNING);
    previous = ConfigureUi(DEFERRED, false);
    StartPrompt(&call);
    uint32_t current = WaitForPrompt(previous);
    assert(current != canceled);
    CtrHost_AnswerShinyFlee(canceled, true);
    assert(CtrHost_IsShinyFleePending(current));
    CtrHost_AnswerShinyFlee(current, true);
    assert(FinishPrompt(&call));

    // Also cancel before the game thread enters pthread_cond_wait.
    previous = ConfigureUi(PAUSE_DURING_DISPATCH, false);
    StartPrompt(&call);
    canceled = WaitForPrompt(previous);
    assert(!FinishPrompt(&call) && !CtrHost_IsShinyFleePending(canceled));
    CtrHost_SetState(CTR_HOST_RUNNING);
}

static void TestExitCancelsPrompt(void)
{
    PromptCall call;
    CtrHost_SetGameplayOptions(8, 1, false, false, true);
    unsigned previous = ConfigureUi(DEFERRED, false);
    StartPrompt(&call);
    uint32_t request = WaitForPrompt(previous);
    CtrHost_SetState(CTR_HOST_EXITING);
    CtrHost_AnswerShinyFlee(request, true);
    assert(!FinishPrompt(&call) && !CtrHost_IsShinyFleePending(request));
    assert(CtrHost_GameSpeed() == 1);
    CtrHost_SetState(CTR_HOST_RUNNING);
    assert(CtrHost_GetState() == CTR_HOST_EXITING);
    previous = ConfigureUi(DEFERRED, false);
    StartPrompt(&call);
    assert(!FinishPrompt(&call));
    assert(ConfigureUi(DEFERRED, false) == previous);
}

int main(void)
{
    TestBottomMenuExpansion();
    TestVoxelAAConfiguration();
    TestDefaultsAndConfiguration();
    TestAcceptRefuseAndStaleRequests();
    TestMissingUiAndImmediateAnswers();
    TestPauseCancelsPrompt();
    TestExitCancelsPrompt();
    puts("host: defaults, dual menu expansion, QoL normalization, paused speed, accept/refuse, stale IDs, missing UI, immediate/async replies, pause/exit cancellation passed");
    return 0;
}
