#define _GNU_SOURCE
#include "android_qol_backups.h"
#include "ctr_host.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum { FLASH = 128 * 1024, SECTOR = 4096 };
static char sRoot[PATH_MAX], sSave[PATH_MAX], sBackups[PATH_MAX];
static bool sEnabled, sFailWrite, sFailRename, sFailSync;
static unsigned sNotifications, sWriteCalls;
static long long sMillis = 10000;
static uint8_t sFlash[FLASH], sRead[FLASH];

bool CtrHost_SaveBackups(void) { return sEnabled; }
const char *CtrHost_SdmcDir(void) { return sRoot; }
void CtrHost_NotifyBackupFailure(void) { ++sNotifications; }

ssize_t __real_write(int fd, const void *data, size_t size);
ssize_t __wrap_write(int fd, const void *data, size_t size)
{
    ++sWriteCalls;
    if (sFailWrite) {
        if (sWriteCalls == 1)
            return __real_write(fd, data, size < 1024 ? size : 1024);
        errno = ENOSPC;
        return -1;
    }
    return __real_write(fd, data, size);
}

int __real_renameat(int oldDir, const char *oldName, int newDir, const char *newName);
int __wrap_renameat(int oldDir, const char *oldName, int newDir, const char *newName)
{
    if (sFailRename) {
        errno = EIO;
        return -1;
    }
    return __real_renameat(oldDir, oldName, newDir, newName);
}

int __real_fsync(int fd);
int __wrap_fsync(int fd)
{
    if (sFailSync) {
        errno = EIO;
        return -1;
    }
    return __real_fsync(fd);
}

int __wrap_clock_gettime(clockid_t clock, struct timespec *now)
{
    assert(clock == CLOCK_REALTIME);
    now->tv_sec = sMillis / 1000;
    now->tv_nsec = (sMillis % 1000) * 1000000;
    return 0;
}

static void Put16(uint8_t *data, unsigned value)
{
    data[0] = value;
    data[1] = value >> 8;
}

static void Put32(uint8_t *data, uint32_t value)
{
    Put16(data, value);
    Put16(data + 2, value >> 16);
}

static void Slot(uint32_t counter)
{
    for (unsigned i = 0; i < 14; ++i) {
        uint8_t *sector = sFlash + ((counter & 1u) * 14 + i) * SECTOR;
        memset(sector, 0, SECTOR);
        /* Rotated sector ids and nonzero payload/checksum exercise format
         * validation rather than depending on physical section order. */
        Put32(sector + 0x10, 0xFFFFFFFFu);
        Put32(sector + 0x14, 0x00000002u);
        Put16(sector + 0xFF4, (i + counter % 14) % 14);
        Put16(sector + 0xFF6, 1);
        Put32(sector + 0xFF8, 0x08012025);
        Put32(sector + 0xFFC, counter);
    }
}

static void Save(uint32_t counter)
{
    memset(sFlash, 0xFF, sizeof(sFlash));
    Slot(counter);
    FILE *file = fopen(sSave, "wb");
    assert(file && fwrite(sFlash, 1, FLASH, file) == FLASH && fclose(file) == 0);
}

static void Rewrite(size_t bytes)
{
    FILE *file = fopen(sSave, "wb");
    assert(file && fwrite(sFlash, 1, bytes, file) == bytes && fclose(file) == 0);
}

static void SameBytes(const char *path, size_t size)
{
    FILE *file = fopen(path, "rb");
    assert(file && fread(sRead, 1, sizeof(sRead), file) == size && fgetc(file) == EOF);
    assert(fclose(file) == 0 && memcmp(sRead, sFlash, size) == 0);
}

static unsigned Files(void)
{
    DIR *directory = opendir(sBackups);
    if (!directory) {
        assert(errno == ENOENT);
        return 0;
    }
    unsigned count = 0;
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        if (entry->d_name[0] == '.') {
            assert(!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."));
            continue;  // No abandoned partial .tmp files.
        }
        if (!strncmp(entry->d_name, "save-", 5))
            ++count;
    }
    closedir(directory);
    return count;
}

static void BackupPath(char path[PATH_MAX], const char *name)
{
    assert(snprintf(path, PATH_MAX, "%s/%s", sBackups, name) < PATH_MAX);
}

static void SeedHistory(void)
{
    for (unsigned counter = 50; counter < 55; ++counter) {
        Save(counter);
        sMillis = counter * 1000;
        CtrQol_BackupCompletedSave(counter);
    }
    assert(Files() == 5 && sNotifications == 0);
}

static void Disabled(void)
{
    Save(1);
    CtrQol_BackupCompletedSave(1);
    assert(!sEnabled && !sNotifications && !sWriteCalls && Files() == 0);
    SameBytes(sSave, FLASH);
}

static void Complete(void)
{
    Save(1);
    CtrQol_BackupCompletedSave(1);
    assert(Files() == 1 && sNotifications == 0);
    char path[PATH_MAX];
    BackupPath(path, "save-10000-1.sav");
    SameBytes(path, FLASH);
    SameBytes(sSave, FLASH);
    /* Saving twice within one clock millisecond still creates two snapshots. */
    CtrQol_BackupCompletedSave(1);
    BackupPath(path, "save-10001-1.sav");
    SameBytes(path, FLASH);
    assert(Files() == 2);
}

static void Invalid(void)
{
    for (unsigned fault = 0; fault < 7; ++fault) {
        Save(3);
        Slot(2);  // A valid older recovery slot must not satisfy counter 3.
        uint8_t *current = sFlash + 14 * SECTOR;
        size_t bytes = FLASH;
        switch (fault) {
        case 0: current[0x10] ^= 1; break; // data checksum
        case 1: current[0xFF8] = 0; break; // uncommitted signature
        case 2: Put16(current + 0xFF4, 14); break; // out-of-range id
        case 3: Put16(current + 0xFF4, 4); break; // duplicate id
        case 4: Put32(current + 0xFFC, 1); break; // mixed/stale counters
        case 5: bytes = FLASH / 2; break; // recovery import, not completed save
        case 6: bytes = FLASH - 1; break;
        }
        Rewrite(bytes);
        CtrQol_BackupCompletedSave(3);
        assert(sNotifications == fault + 1 && Files() == 0);
        SameBytes(sSave, bytes);
    }
}

static void Retention(void)
{
    SeedHistory();
    char unknown[PATH_MAX];
    BackupPath(unknown, "notes.txt");
    FILE *note = fopen(unknown, "wb");
    assert(note && fputs("keep", note) >= 0 && fclose(note) == 0);
    for (unsigned counter = 55; counter < 58; ++counter) {
        Save(counter);
        sMillis = counter * 1000;
        CtrQol_BackupCompletedSave(counter);
    }
    assert(Files() == 5);
    char path[PATH_MAX];
    BackupPath(path, "save-53000-53.sav");
    assert(access(path, F_OK) == 0);
    BackupPath(path, "save-52000-52.sav");
    assert(access(path, F_OK) != 0);
    Save(1); // Imported earlier progress and a backwards device clock.
    sMillis = 1000;
    CtrQol_BackupCompletedSave(1);
    BackupPath(path, "save-57001-1.sav");
    SameBytes(path, FLASH);
    assert(Files() == 5 && sNotifications == 0 && access(unknown, F_OK) == 0);
    Save(UINT32_MAX);
    CtrQol_BackupCompletedSave(UINT32_MAX);
    Save(0); // Native save counter wraps without losing the new backup.
    CtrQol_BackupCompletedSave(0);
    BackupPath(path, "save-57003-0.sav");
    SameBytes(path, FLASH);
    assert(Files() == 5 && sNotifications == 0);
}

static void Failure(const char *kind)
{
    SeedHistory();
    Save(55);
    sMillis = 55000;
    sWriteCalls = 0;
    sFailWrite = !strcmp(kind, "write");
    sFailRename = !strcmp(kind, "rename");
    sFailSync = !strcmp(kind, "sync");
    CtrQol_BackupCompletedSave(55);
    assert(Files() == 5 && sNotifications == 1);
    SameBytes(sSave, FLASH);
    char path[PATH_MAX];
    BackupPath(path, "save-50000-50.sav");
    assert(access(path, F_OK) == 0); // Oldest was not deleted for a failed write.
    BackupPath(path, "save-55000-55.sav");
    assert(access(path, F_OK) != 0);
    sFailWrite = sFailRename = sFailSync = false;
    CtrQol_BackupCompletedSave(55);
    SameBytes(path, FLASH);
    assert(Files() == 5 && sNotifications == 1);
}

int main(int argc, char **argv)
{
    assert(argc == 3 && strlen(argv[2]) + 64 < PATH_MAX);
    strcpy(sRoot, argv[2]);
    char directory[PATH_MAX];
    assert(snprintf(directory, sizeof(directory), "%s/3ds", sRoot) < PATH_MAX);
    assert(mkdir(directory, 0700) == 0);
    assert(snprintf(directory, sizeof(directory), "%s/3ds/emerald3ds", sRoot) < PATH_MAX);
    assert(mkdir(directory, 0700) == 0);
    assert(snprintf(sSave, sizeof(sSave), "%s/emerald3ds.sav", directory) < PATH_MAX);
    assert(snprintf(sBackups, sizeof(sBackups), "%s/backups", directory) < PATH_MAX);
    sEnabled = strcmp(argv[1], "disabled") != 0;
    if (!strcmp(argv[1], "disabled")) Disabled();
    else if (!strcmp(argv[1], "complete")) Complete();
    else if (!strcmp(argv[1], "invalid")) Invalid();
    else if (!strcmp(argv[1], "retention")) Retention();
    else Failure(argv[1]);
    puts("save backup checks passed");
    return 0;
}
