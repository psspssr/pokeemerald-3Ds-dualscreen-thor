/* Manual interoperability test driver for libmgba; no ROM or save is included.
 * Compile with the matching libmgba development headers and library, then:
 *   mgba_save_driver pristine.gba copy-of-android.sav
 * Commands on stdin: frames COUNT KEYS, read ADDRESS SIZE, screenshot PATH,
 * save PATH, quit. KEYS uses GBA key bits; reads are observational only.
 * This runs the actual GBA game and its normal save/load code without patches.
 */
#include <mgba/flags.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba-util/vfs.h>
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void logError(struct mLogger *logger, int category, enum mLogLevel level,
                     const char *format, va_list args)
{
    (void)logger;
    (void)category;
    if (level & (mLOG_FATAL | mLOG_ERROR)) {
        vfprintf(stderr, format, args);
        fputc('\n', stderr);
    }
}

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: %s ROM COPY_OF_SAVE\n", argv[0]); return 2; }
    struct mLogger logger = { .log = logError };
    mLogSetDefaultLogger(&logger);
    struct mCore *core = mCoreFind(argv[1]);
    assert(core && core->init(core));
    mCoreInitConfig(core, "emerald-save-test");
    mCoreConfigSetDefaultIntValue(&core->config, "audioSync", 0);
    mCoreConfigSetDefaultIntValue(&core->config, "videoSync", 0);
    mCoreConfigSetDefaultIntValue(&core->config, "skipBios", 1);
    mCoreLoadConfig(core);
    assert(mCoreLoadFile(core, argv[1]));
    assert(mCoreLoadSaveFile(core, argv[2], false));
    unsigned width, height;
    core->desiredVideoDimensions(core, &width, &height);
    color_t *pixels = calloc(width * height, sizeof(*pixels));
    assert(pixels);
    core->setVideoBuffer(core, pixels, width);
    core->reset(core);
    printf("READY %ux%u\n", width, height);
    fflush(stdout);
    char line[4096], command[32], path[4000];
    while (fgets(line, sizeof(line), stdin)) {
        unsigned count, keys, address, size;
        if (sscanf(line, "%31s", command) != 1) continue;
        if (!strcmp(command, "frames") && sscanf(line, "%*s %u %i", &count, (int *)&keys) == 2) {
            core->setKeys(core, keys);
            for (unsigned i = 0; i < count; ++i) core->runFrame(core);
            printf("FRAME %u keys=%u\n", core->frameCounter(core), keys);
        } else if (!strcmp(command, "read") && sscanf(line, "%*s %x %u", &address, &size) == 2) {
            printf("MEM %08x:", address);
            for (unsigned i = 0; i < size; ++i) printf(" %02x", core->busRead8(core, address + i));
            putchar('\n');
        } else if (!strcmp(command, "screenshot") && sscanf(line, "%*s %3999s", path) == 1) {
            struct VFile *file = VFileOpen(path, O_WRONLY | O_CREAT | O_TRUNC);
            assert(file && mCoreTakeScreenshotVF(core, file));
            file->close(file);
            printf("SCREENSHOT %s\n", path);
        } else if (!strcmp(command, "save") && sscanf(line, "%*s %3999s", path) == 1) {
            void *data = NULL;
            size_t bytes = core->savedataClone(core, &data);
            assert(bytes == 128 * 1024 && data);
            FILE *file = fopen(path, "wb");
            assert(file && fwrite(data, 1, bytes, file) == bytes && fclose(file) == 0);
            free(data);
            printf("SAVE %s %zu\n", path, bytes);
        } else if (!strcmp(command, "quit")) break;
        else { fprintf(stderr, "invalid command: %s", line); return 2; }
        fflush(stdout);
    }
    core->deinit(core);
    free(pixels);
    return 0;
}
