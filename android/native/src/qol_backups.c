/* Optional snapshots of completed GBA saves. No game/save format changes. */
#define _POSIX_C_SOURCE 200809L
#include "../include/android_qol_backups.h"
#include "ctr_host.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum { FLASH_BYTES = 128 * 1024, SECTOR_BYTES = 4096, SLOT_SECTORS = 14,
       KEEP_BACKUPS = 5, NAME_BYTES = 64 };
static const unsigned sDataSizes[SLOT_SECTORS] = {
    0xF2C, 0xF80, 0xF80, 0xF80, 0xF08,
    0xF80, 0xF80, 0xF80, 0xF80, 0xF80, 0xF80, 0xF80, 0xF80, 0x7D0
};

typedef struct {
    uint64_t millis;
    char name[NAME_BYTES];
} BackupName;

static unsigned Read16(const uint8_t *data)
{
    return data[0] | (unsigned)data[1] << 8;
}

static uint32_t Read32(const uint8_t *data)
{
    return Read16(data) | (uint32_t)Read16(data + 2) << 16;
}

static bool CompletedSlot(const uint8_t *flash, uint32_t counter)
{
    unsigned seen = 0;
    const uint8_t *slot = flash + (counter & 1u) * SLOT_SECTORS * SECTOR_BYTES;
    for (unsigned i = 0; i < SLOT_SECTORS; ++i) {
        const uint8_t *sector = slot + i * SECTOR_BYTES;
        unsigned id = Read16(sector + 0xFF4);
        if (id >= SLOT_SECTORS || (seen & (1u << id))
                || Read32(sector + 0xFF8) != 0x08012025u
                || Read32(sector + 0xFFC) != counter)
            return false;
        uint32_t sum = 0;
        for (unsigned byte = 0; byte < sDataSizes[id]; byte += 4)
            sum += Read32(sector + byte);
        if ((uint16_t)((sum & 0xFFFFu) + (sum >> 16)) != Read16(sector + 0xFF6))
            return false;
        seen |= 1u << id;
    }
    return seen == (1u << SLOT_SECTORS) - 1u;
}

static bool ReadSave(const char *path, uint8_t *flash, uint32_t counter)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return false;
    struct stat info;
    bool ok = fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_size == FLASH_BYTES;
    size_t done = 0;
    while (ok && done < FLASH_BYTES) {
        ssize_t bytes = read(fd, flash + done, FLASH_BYTES - done);
        if (bytes < 0 && errno == EINTR)
            continue;
        if (bytes <= 0) {
            ok = false;
            break;
        }
        done += (size_t)bytes;
    }
    if (close(fd) != 0)
        ok = false;
    return ok && CompletedSlot(flash, counter);
}

static bool ParseName(const char *name, uint64_t *millis)
{
    if (strncmp(name, "save-", 5) != 0 || name[5] < '0' || name[5] > '9')
        return false;
    errno = 0;
    char *end;
    uint64_t timestamp = strtoull(name + 5, &end, 10);
    if (errno || timestamp >= INT64_MAX || *end++ != '-' || *end < '0' || *end > '9')
        return false;
    unsigned long long counter = strtoull(end, &end, 10);
    if (errno || counter > UINT32_MAX || strcmp(end, ".sav") != 0 || strlen(name) >= NAME_BYTES)
        return false;
    *millis = timestamp;
    return true;
}

static DIR *OpenScan(int directory)
{
    /* A new description starts at offset zero; dup() would share readdir's
     * offset with the preceding scan of this directory. */
    int fd = openat(directory, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        return NULL;
    DIR *stream = fdopendir(fd);
    if (!stream)
        close(fd);
    return stream;
}

static bool ManagedFile(int directory, const char *name)
{
    struct stat info;
    return fstatat(directory, name, &info, AT_SYMLINK_NOFOLLOW) == 0
        && S_ISREG(info.st_mode) && info.st_size == FLASH_BYTES;
}

static bool ScanBackups(int directory, BackupName keep[KEEP_BACKUPS - 1],
                        unsigned *count, uint64_t *newest)
{
    DIR *stream = OpenScan(directory);
    if (!stream)
        return false;
    bool ok = true;
    *count = 0;
    *newest = 0;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(stream);
        if (!entry) {
            ok = errno == 0;
            break;
        }
        uint64_t millis;
        if (!ParseName(entry->d_name, &millis))
            continue;
        /* Even an incomplete/mismatched file reserves its name. Never replace
         * it, follow symlinks, or delete files outside our complete snapshots. */
        if (millis > *newest)
            *newest = millis;
        if (!ManagedFile(directory, entry->d_name))
            continue;
        unsigned at = 0;
        while (at < *count && (keep[at].millis > millis
               || (keep[at].millis == millis && strcmp(keep[at].name, entry->d_name) > 0)))
            ++at;
        if (at >= KEEP_BACKUPS - 1)
            continue;
        unsigned last = *count < KEEP_BACKUPS - 1 ? (*count)++ : KEEP_BACKUPS - 2;
        while (last > at) {
            keep[last] = keep[last - 1];
            --last;
        }
        keep[at].millis = millis;
        strcpy(keep[at].name, entry->d_name);
    }
    if (closedir(stream) != 0)
        ok = false;
    return ok;
}

static bool SyncDirectory(int directory)
{
    if (fsync(directory) == 0)
        return true;
    /* Some Android shared-storage filesystems do not support directory
     * fsync. The file was fsynced before its same-directory atomic rename. */
    return errno == EINVAL || errno == ENOTSUP;
}

static bool WriteSnapshot(int directory, const char *name, const uint8_t *flash)
{
    char temporary[NAME_BYTES + 8];
    snprintf(temporary, sizeof(temporary), ".%s.tmp", name);
    int fd = openat(directory, temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return false;
    bool ok = true;
    size_t done = 0;
    while (done < FLASH_BYTES) {
        ssize_t bytes = write(fd, flash + done, FLASH_BYTES - done);
        if (bytes < 0 && errno == EINTR)
            continue;
        if (bytes <= 0) {
            ok = false;
            break;
        }
        done += (size_t)bytes;
    }
    if (ok && fsync(fd) != 0)
        ok = false;
    if (close(fd) != 0)
        ok = false;
    struct stat info;
    if (ok && (fstatat(directory, name, &info, AT_SYMLINK_NOFOLLOW) == 0 || errno != ENOENT))
        ok = false;
    if (ok && renameat(directory, temporary, directory, name) != 0)
        ok = false;
    if (!ok)
        unlinkat(directory, temporary, 0);
    return ok && SyncDirectory(directory);
}

static bool RotateBackups(int directory, const BackupName keep[KEEP_BACKUPS - 1],
                          unsigned count, const char *latest)
{
    DIR *stream = OpenScan(directory);
    if (!stream)
        return false;
    bool ok = true;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(stream);
        if (!entry) {
            if (errno)
                ok = false;
            break;
        }
        uint64_t ignored;
        if (!ParseName(entry->d_name, &ignored) || !strcmp(entry->d_name, latest)
                || !ManagedFile(directory, entry->d_name))
            continue;
        bool retain = false;
        for (unsigned i = 0; i < count; ++i)
            retain |= !strcmp(entry->d_name, keep[i].name);
        if (!retain && unlinkat(directory, entry->d_name, 0) != 0)
            ok = false;
    }
    if (closedir(stream) != 0)
        ok = false;
    return ok && SyncDirectory(directory);
}

static bool Backup(uint32_t counter)
{
    const char *sdmc = CtrHost_SdmcDir();
    if (!sdmc || sdmc[0] != '/')
        return false;
    char save[PATH_MAX], folder[PATH_MAX];
    int n = snprintf(save, sizeof(save), "%s/3ds/emerald3ds/emerald3ds.sav", sdmc);
    int m = snprintf(folder, sizeof(folder), "%s/3ds/emerald3ds/backups", sdmc);
    if (n < 0 || n >= (int)sizeof(save) || m < 0 || m >= (int)sizeof(folder))
        return false;
    uint8_t *flash = malloc(FLASH_BYTES);
    if (!flash)
        return false;
    bool ok = ReadSave(save, flash, counter);
    int directory = -1;
    if (ok && mkdir(folder, 0700) != 0 && errno != EEXIST)
        ok = false;
    if (ok) {
        directory = open(folder, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        ok = directory >= 0;
    }
    BackupName keep[KEEP_BACKUPS - 1];
    unsigned count;
    uint64_t newest;
    struct timespec now;
    if (ok)
        ok = ScanBackups(directory, keep, &count, &newest)
            && clock_gettime(CLOCK_REALTIME, &now) == 0 && now.tv_sec >= 0;
    if (ok) {
        uint64_t millis = (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
        /* Imported games and wrapping save counters may go backwards. Clock
         * rollback likewise must not make a just-completed save look oldest. */
        if (millis <= newest)
            millis = newest + 1;
        char name[NAME_BYTES];
        snprintf(name, sizeof(name), "save-%" PRIu64 "-%" PRIu32 ".sav", millis, counter);
        ok = millis < INT64_MAX && WriteSnapshot(directory, name, flash);
        if (ok)
            ok = RotateBackups(directory, keep, count, name);
    }
    if (directory >= 0 && close(directory) != 0)
        ok = false;
    free(flash);
    return ok;
}

void CtrQol_BackupCompletedSave(unsigned counter)
{
    if (CtrHost_SaveBackups() && !Backup(counter))
        CtrHost_NotifyBackupFailure();
}
