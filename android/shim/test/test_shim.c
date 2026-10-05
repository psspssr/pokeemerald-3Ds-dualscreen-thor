/* Behavioral tests for the real system shim, with only Android I/O replaced. */
#include <3ds/allocator/linear.h>
#include <3ds/allocator/vram.h>
#include <3ds/os.h>
#include <3ds/ndsp/channel.h>
#include <3ds/services/apt.h>
#include <3ds/services/hid.h>
#include <3ds/thread.h>
#include <3ds/romfs.h>
#include <ctr_host.h>
#include <ctrshim.h>
#include <ctrshim_mem.h>
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ndsp_backend.h"

static char romfs[512], sdmc[512];
static CtrHostInput input;
static bool failNextMemoryBacking;
static unsigned memoryBackingCalls;
int __real_posix_memalign(void **memory, size_t alignment, size_t size);
int __wrap_posix_memalign(void **memory, size_t alignment, size_t size)
{
    ++memoryBackingCalls;
    if (failNextMemoryBacking)
    {
        failNextMemoryBacking = false;
        return ENOMEM;
    }
    return __real_posix_memalign(memory, alignment, size);
}
static pthread_mutex_t hostLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t hostCond = PTHREAD_COND_INITIALIZER;
static CtrHostState hostState = CTR_HOST_RUNNING;
static atomic_int pauses, resumes, exits, exitCode, cleanup;
static atomic_uint gameSpeed = 1;

const char *CtrHost_RomfsDir(void) { return romfs; }
const char *CtrHost_SdmcDir(void) { return sdmc; }
void CtrHost_GetInput(CtrHostInput *out) { *out = input; }
unsigned CtrHost_GameSpeed(void) { return atomic_load(&gameSpeed); }
CtrHostState CtrHost_GetState(void)
{
    pthread_mutex_lock(&hostLock);
    CtrHostState state = hostState;
    pthread_mutex_unlock(&hostLock);
    return state;
}
void CtrHost_SetState(CtrHostState state)
{
    pthread_mutex_lock(&hostLock);
    hostState = state;
    pthread_cond_broadcast(&hostCond);
    pthread_mutex_unlock(&hostLock);
}
CtrHostState CtrHost_WaitWhilePaused(void)
{
    pthread_mutex_lock(&hostLock);
    while (hostState == CTR_HOST_PAUSED) pthread_cond_wait(&hostCond, &hostLock);
    CtrHostState state = hostState;
    pthread_mutex_unlock(&hostLock);
    return state;
}
void CtrHost_NotifyGameExit(int code) { atomic_store(&exitCode, code); atomic_fetch_add(&exits, 1); }
int __android_log_write(int priority, const char *tag, const char *text)
{
    (void)priority; (void)tag; (void)text;
    return 0;
}
static bool AudioOpen(void) { return true; }
static void AudioClose(void) {}
static void AudioPause(void) { atomic_fetch_add(&pauses, 1); }
static void AudioResume(void) { atomic_fetch_add(&resumes, 1); }
static const CtrNdspBackend backend = {AudioOpen, AudioClose, AudioPause, AudioResume};
const CtrNdspBackend *CtrNdsp_GetBackend(void) { return &backend; }

static void TestFilesystem(const char *root)
{
    char mapped[1024], text[16] = {0};
    snprintf(romfs, sizeof(romfs), "%s/romfs", root);
    snprintf(sdmc, sizeof(sdmc), "%s/sdmc", root);
    assert(CtrShim_MapPath("romfs:/data", mapped, sizeof(mapped)) == NULL && errno == ENODEV);
    assert(mkdir(romfs, 0700) == 0);
    assert(romfsInit() == 0);
    assert(CtrShim_MapPath("romfs:/data", mapped, 4) == NULL && errno == ENAMETOOLONG);
    assert(strcmp(CtrShim_MapPath("absolute", mapped, sizeof(mapped)), "absolute") == 0);
    FILE *save = fopen("sdmc:/save.tmp", "wb");
    assert(save != NULL && fwrite("save-data", 1, 9, save) == 9 && fclose(save) == 0);
    assert(rename("sdmc:/save.tmp", "sdmc:/game.sav") == 0);
    struct stat st;
    assert(stat("sdmc:/game.sav", &st) == 0 && st.st_size == 9);
    save = fopen("sdmc:/game.sav", "rb");
    assert(save != NULL && fread(text, 1, sizeof(text), save) == 9 && fclose(save) == 0);
    assert(strcmp(text, "save-data") == 0);
    assert(unlink("sdmc:/game.sav") == 0);
    romfsExit();
    assert(fopen("romfs:/data", "rb") == NULL && errno == ENODEV);
    assert(rmdir(romfs) == 0 && rmdir(sdmc) == 0);
}

static void TestMemory(void)
{
    u32 before = linearSpaceFree();
    void *a = linearMemAlign(1001, 256), *b = linearAlloc(4096);
    assert(a && b && ((uintptr_t)a & 255) == 0);
    assert(linearGetSize(a) >= 1001 && linearSpaceFree() < before);
    memset(a, 0x7b, 1001);
    CtrMemBlock block;
    assert(CtrMem_Find((char *)a + 1000, &block) && block.kind == CTR_MEM_LINEAR);
    CtrMem_Register(CTR_MEM_FRAMEBUFFER, a, 1001, b);
    assert(CtrMem_Find(a, &block) && block.kind == CTR_MEM_FRAMEBUFFER && block.owner == b);
    CtrMem_Unregister(a);
    assert(CtrMem_Find(a, &block) && block.kind == CTR_MEM_LINEAR);
    assert(osConvertVirtToPhys(a) != 0);
    linearFree(a); linearFree(b);
    assert(linearSpaceFree() == before);
    before = vramSpaceFree();
    a = vramAllocAt(4096, VRAM_ALLOC_A); b = vramAllocAt(4096, VRAM_ALLOC_B);
    assert(a && b && vramSpaceFree() == before - 8192);
    vramFree(a); vramFree(b);
    assert(vramSpaceFree() == before);
    assert(linearMemAlign(1, 17) == NULL);
}

static void TestVramBudget(void)
{
    /* Model the observed 513024-byte free budget split between the original
     * console banks: a new 512x256 layer cannot coexist with those live
     * allocations. Android must retain room for that layer and battle copy. */
    const size_t live = (6u * 1024u * 1024u - 513024u) / 2;
    const u32 total = vramSpaceFree();
    unsigned calls = memoryBackingCalls;
    assert(vramSpaceFree() == total && memoryBackingCalls == calls);
    assert(CtrMem_Used(CTR_MEM_VRAM) == 0); /* Budget is not eager backing. */
    for (unsigned cycle = 0; cycle < 16; ++cycle)
    {
        void *a = vramAllocAt(live, VRAM_ALLOC_A);
        void *b = vramAllocAt(live, VRAM_ALLOC_B);
        void *layer = vramAlloc(512u * 256u * 2u);
        void *battle = vramAlloc(1024u * 256u * 2u);
        if (!layer || !battle)
            fprintf(stderr, "VRAM graphics coexistence failed: layer=%d battle=%d free=%u\n",
                    layer != NULL, battle != NULL, vramSpaceFree());
        assert(a && b && layer && battle);
        assert(((u8 *)layer)[0] == 0 && ((u8 *)layer)[512u * 256u * 2u - 1] == 0);
        memset(layer, 0x71, 512u * 256u * 2u);
        memset(battle, 0x29, 1024u * 256u * 2u);
        vramFree(b); vramFree(layer); vramFree(a); vramFree(battle);
        assert(vramSpaceFree() == total && CtrMem_Used(CTR_MEM_VRAM) == 0);
    }
    assert(total == 16u * 1024u * 1024u && total == OS_VRAM_SIZE && total == CTR_MEM_VRAM_POOL);
    assert(OS_VRAM_VADDR >= OS_OLD_FCRAM_VADDR + OS_OLD_FCRAM_SIZE);
    assert(OS_VRAM_VADDR + OS_VRAM_SIZE <= OS_DSPRAM_VADDR);
    assert(OS_VRAM_PADDR + OS_VRAM_SIZE <= OS_FCRAM_PADDR);
    const size_t bank = total / 2;
    void *a = vramAllocAt(bank, VRAM_ALLOC_A), *b = vramAllocAt(bank, VRAM_ALLOC_B);
    assert(a && b && vramSpaceFree() == 0);
    assert(((uintptr_t)a & 0x7f) == 0 && ((uintptr_t)b & 0x7f) == 0);
    assert(vramGetSize(a) == bank && vramGetSize(b) == bank);
    assert(osConvertVirtToPhys(a) == OS_VRAM_PADDR);
    assert(osConvertVirtToPhys(b) == OS_VRAM_PADDR + bank);
    assert(osConvertVirtToPhys((u8 *)a + bank - 1) == OS_VRAM_PADDR + bank - 1);
    assert(osConvertVirtToPhys((u8 *)b + bank - 1) == OS_VRAM_PADDR + total - 1);
    CtrMemBlock block;
    assert(CtrMem_Find((u8 *)a + bank - 1, &block) && block.kind == CTR_MEM_VRAM && block.size == bank);
    ((u8 *)a)[bank - 1] = 0x42; ((u8 *)b)[bank - 1] = 0x24;
    assert(!vramAlloc(128) && !vramAllocAt(bank + 128, VRAM_ALLOC_A));
    vramFree(a);
    assert(!vramAllocAt(128, VRAM_ALLOC_B)); /* Free A never spills a B-only request. */
    a = vramAllocAt(bank, VRAM_ALLOC_ANY);
    assert(a && osConvertVirtToPhys(a) == OS_VRAM_PADDR);
    vramFree(a); vramFree(b);
    assert(vramSpaceFree() == total);

    /* Real heap failure must roll the synthetic reservation back, including
     * its physical offset; a larger quota never turns ENOMEM into success. */
    failNextMemoryBacking = true;
    assert(!vramAllocAt(512u * 1024u, VRAM_ALLOC_A));
    assert(!failNextMemoryBacking && vramSpaceFree() == total && CtrMem_Used(CTR_MEM_VRAM) == 0);
    a = vramAllocAt(512u * 1024u, VRAM_ALLOC_A);
    assert(a && osConvertVirtToPhys(a) == OS_VRAM_PADDR);
    vramFree(a);

    void *pieces[4];
    for (unsigned i = 0; i < 4; ++i) { pieces[i] = vramAllocAt(bank / 4, VRAM_ALLOC_A); assert(pieces[i]); }
    vramFree(pieces[0]); vramFree(pieces[2]);
    assert(!vramAllocAt(bank / 4 + 128, VRAM_ALLOC_A)); /* Fragmentation still matters. */
    b = vramAllocAt(bank / 4 + 128, VRAM_ALLOC_ANY);
    assert(b && osConvertVirtToPhys(b) == OS_VRAM_PADDR + bank);
    vramFree(pieces[3]); vramFree(pieces[1]); vramFree(b);
    a = vramAllocAt(bank, VRAM_ALLOC_A); assert(a); vramFree(a);
    assert(vramSpaceFree() == total && CtrMem_Used(CTR_MEM_VRAM) == 0);
    puts("shim: bounded 16 MiB VRAM, bank/physical offsets, repeated scene allocations, fragmentation and real backing-failure rollback passed");
}

static void TestInput(void)
{
    input = (CtrHostInput){.keys = KEY_A | KEY_TOUCH, .circleX = 120, .circleY = 100,
                           .touchX = 319, .touchY = 239};
    hidScanInput();
    assert((hidKeysDown() & (KEY_A | KEY_TOUCH | KEY_CPAD_RIGHT | KEY_CPAD_UP)) ==
           (KEY_A | KEY_TOUCH | KEY_CPAD_RIGHT | KEY_CPAD_UP));
    hidScanInput();
    assert(hidKeysDown() == 0);
    touchPosition touch;
    hidTouchRead(&touch);
    assert(touch.px == 319 && touch.py == 239);
    input = (CtrHostInput){0}; hidScanInput();
    assert(hidKeysHeld() == 0 && (hidKeysUp() & KEY_A));
    hidTouchRead(&touch);
    assert(touch.px == 0 && touch.py == 0);
}

static LightEvent gate;
static LightLock counterLock;
static unsigned counter;
static void Worker(void *unused)
{
    (void)unused;
    LightEvent_Wait(&gate);
    for (int i = 0; i < 10000; ++i) {
        LightLock_Lock(&counterLock); ++counter; LightLock_Unlock(&counterLock);
    }
    threadExit(37);
}
static void TestThreads(void)
{
    Result unsupported = MAKERESULT(RL_PERMANENT, RS_NOTSUPPORTED, RM_KERNEL, RD_NOT_IMPLEMENTED);
    assert(R_FAILED(unsupported));
    assert(R_FAILED(MAKERESULT(RL_USAGE, RS_INVALIDARG, RM_APT, RD_INVALID_POINTER)));
    assert(R_LEVEL(unsupported) == RL_PERMANENT && R_SUMMARY(unsupported) == RS_NOTSUPPORTED);
    assert(R_MODULE(unsupported) == RM_KERNEL && R_DESCRIPTION(unsupported) == RD_NOT_IMPLEMENTED);
    /* The voxel stream's new system-core hint must still allow workers. */
    u32 percent = 123;
    assert(APT_GetAppCpuTimeLimit(&percent) == 0 && percent == 0);
    assert(APT_SetAppCpuTimeLimit(50) == 0);
    assert(APT_GetAppCpuTimeLimit(&percent) == 0 && percent == 50);
    assert(R_FAILED(APT_SetAppCpuTimeLimit(101)));
    assert(APT_GetAppCpuTimeLimit(&percent) == 0 && percent == 50);
    assert(R_FAILED(APT_GetAppCpuTimeLimit(NULL)));
    assert(APT_SetAppCpuTimeLimit(30) == 0);
    assert(APT_GetAppCpuTimeLimit(&percent) == 0 && percent == 30);
    s64 luma = 123;
    assert(R_FAILED(svcGetSystemInfo(&luma, 0x10000, 0)) && luma == 0);
    assert(R_FAILED(svcGetSystemInfo(NULL, 0x10000, 0)));
    extern int __system_argc;
    extern char **__system_argv;
    assert(__system_argc == 0 && __system_argv == NULL);
    LightEvent_Init(&gate, RESET_STICKY);
    LightLock_Init(&counterLock);
    Thread a = threadCreate(Worker, NULL, 8192, 0x30, -1, false);
    Thread b = threadCreate(Worker, NULL, 8192, 0x30, -1, false);
    assert(a && b && threadJoin(a, 0) == CTR_RESULT_TIMEOUT);
    LightEvent_Signal(&gate);
    assert(threadJoin(a, 2000000000ull) == 0 && threadJoin(b, 2000000000ull) == 0);
    assert(counter == 20000 && threadGetExitCode(a) == 37 && threadGetExitCode(b) == 37);
    threadFree(a); threadFree(b);
    LightEvent_Init(&gate, RESET_ONESHOT);
    assert(LightEvent_WaitTimeout(&gate, 1000000) != 0);
    LightEvent_Signal(&gate);
    assert(LightEvent_TryWait(&gate) && !LightEvent_TryWait(&gate));
    RecursiveLock recursive;
    RecursiveLock_Init(&recursive);
    RecursiveLock_Lock(&recursive); assert(RecursiveLock_TryLock(&recursive) == 0);
    RecursiveLock_Unlock(&recursive); RecursiveLock_Unlock(&recursive);
}

static void *PauseLoop(void *unused) { (void)unused; assert(aptMainLoop()); return NULL; }
static void CountAudioFrame(void *data) { ++*(unsigned *)data; }
static void TestAudioAndLifecycle(void)
{
    assert(ndspInit() == 0);
    int16_t pcm[] = {32767, -32768, 16384, -16384};
    ndspWaveBuf wave = {.data_pcm16 = pcm, .nsamples = 2};
    float out[8];
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    ndspChnSetRate(0, 48000);
    ndspChnSetInterp(0, NDSP_INTERP_NONE);
    ndspChnWaveBufAdd(0, &wave);
    assert(wave.status == NDSP_WBUF_QUEUED);
    CtrNdsp_Render(out, 4, 48000);
    assert(fabsf(out[2] - 32767.0f / 32768.0f) < 0.0001f && out[3] == -1.0f);
    assert(out[4] == 0.5f && out[5] == -0.5f && wave.status == NDSP_WBUF_DONE);
    float guard = 123;
    CtrNdsp_Render(&guard, -1, 48000);
    assert(guard == 123);
    /* A pending normal-speed loop must be discarded on entering fast-forward.
     * Producers complete buffers immediately, rather than filling their ring
     * or accumulating delayed playback while the game keeps mixing. */
    wave = (ndspWaveBuf){.data_pcm16 = pcm, .nsamples = 2, .looping = true};
    ndspChnWaveBufAdd(0, &wave);
    assert(ndspChnIsPlaying(0));
    atomic_store(&gameSpeed, 4);
    unsigned audioFrames = 0;
    ndspSetCallback(CountAudioFrame, &audioFrames);
    float muted[400];
    CtrNdsp_Render(muted, 200, NDSP_SAMPLE_RATE);
    for (unsigned i = 0; i < 400; ++i) assert(muted[i] == 0.0f);
    assert(wave.status == NDSP_WBUF_DONE && !ndspChnIsPlaying(0) && audioFrames > 0);
    for (unsigned speed = 2; speed <= 8; ++speed)
    {
        atomic_store(&gameSpeed, speed);
        for (unsigned tick = 0; tick < 32; ++tick)
        {
            wave = (ndspWaveBuf){.data_pcm16 = pcm, .nsamples = 2};
            ndspChnWaveBufAdd(0, &wave);
            assert(wave.status == NDSP_WBUF_DONE && !ndspChnIsPlaying(0));
        }
    }
    /* Resumption emits only newly submitted audio, including the usual
     * initial silent sample; old loop and interpolator samples cannot leak. */
    atomic_store(&gameSpeed, 1);
    int16_t fresh[] = {8192, 8192, 8192, 8192};
    wave = (ndspWaveBuf){.data_pcm16 = fresh, .nsamples = 2};
    ndspChnWaveBufAdd(0, &wave);
    CtrNdsp_Render(out, 4, 48000);
    assert(out[0] == 0.0f && out[1] == 0.0f);
    assert(out[2] == 0.25f && out[3] == 0.25f && out[4] == 0.25f && out[5] == 0.25f);
    assert(wave.status == NDSP_WBUF_DONE);
    ndspSetCallback(NULL, NULL);
    /* Entering acceleration between output callbacks must also release the
     * old queue as soon as the game submits another frame. */
    wave.looping = true;
    ndspChnWaveBufAdd(0, &wave);
    atomic_store(&gameSpeed, 4);
    ndspWaveBuf incoming = {.data_pcm16 = pcm, .nsamples = 2};
    ndspChnWaveBufAdd(0, &incoming);
    assert(wave.status == NDSP_WBUF_DONE && incoming.status == NDSP_WBUF_DONE);
    atomic_store(&gameSpeed, 1);
    CtrNdsp_Render(out, 4, 48000);
    for (unsigned i = 0; i < 8; ++i) assert(out[i] == 0.0f);
    CtrHost_SetState(CTR_HOST_PAUSED);
    pthread_t thread;
    assert(pthread_create(&thread, NULL, PauseLoop, NULL) == 0);
    for (int i = 0; i < 1000 && atomic_load(&pauses) == 0; ++i) usleep(1000);
    assert(atomic_load(&pauses) == 1);
    CtrHost_SetState(CTR_HOST_RUNNING);
    pthread_join(thread, NULL);
    assert(atomic_load(&resumes) == 1);
    ndspExit();
    CtrHost_SetState(CTR_HOST_EXITING);
    assert(!aptMainLoop());
}

extern int __wrap_atexit(void (*function)(void));
static void Cleanup(void) { atomic_fetch_add(&cleanup, 1); }
static void *ExitThread(void *unused) { (void)unused; CtrShim_Exit(17); }
static void TestExit(void)
{
    assert(__wrap_atexit(Cleanup) == 0);
    pthread_t thread;
    assert(pthread_create(&thread, NULL, ExitThread, NULL) == 0);
    pthread_join(thread, NULL);
    assert(atomic_load(&cleanup) == 1 && atomic_load(&exits) == 1 && atomic_load(&exitCode) == 17);
}

int main(void)
{
    char root[] = "/tmp/emerald-shim-XXXXXX";
    assert(mkdtemp(root));
    TestFilesystem(root); TestMemory(); TestVramBudget(); TestInput(); TestThreads(); TestAudioAndLifecycle(); TestExit();
    assert(rmdir(root) == 0);
    puts("shim: filesystem, memory, input, synchronization, threads, PCM/fast-forward mute/recovery, lifecycle and exit passed");
    return 0;
}
