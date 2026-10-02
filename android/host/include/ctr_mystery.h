#ifndef CTR_MYSTERY_H
#define CTR_MYSTERY_H

/* Stable wire values shared by the Android menu, host queue and game adapter.
 * Event IDs are append-only; they index the returned status array. */
#define CTR_MYSTERY_MAX_EVENTS 32

enum CtrMysteryStatus
{
    CTR_MYSTERY_AVAILABLE = 0,
    CTR_MYSTERY_UNLOCKED = 1,
    CTR_MYSTERY_COMPLETED = 2,
    CTR_MYSTERY_NO_GAME = 3,
    CTR_MYSTERY_BUSY = 4,
    CTR_MYSTERY_BAG_FULL = 5,
    CTR_MYSTERY_PARTY_FULL = 6,
    CTR_MYSTERY_PREREQUISITE = 7,
    CTR_MYSTERY_DECOR_FULL = 8,
};

enum CtrMysteryResult
{
    CTR_MYSTERY_RESULT_ACTIVATED = 0,
    CTR_MYSTERY_RESULT_ALREADY = 1,
    CTR_MYSTERY_RESULT_NO_GAME = 2,
    CTR_MYSTERY_RESULT_BUSY = 3,
    CTR_MYSTERY_RESULT_NO_SPACE = 4,
    CTR_MYSTERY_RESULT_INVALID = 5,
    CTR_MYSTERY_RESULT_FAILED = 6,
    CTR_MYSTERY_RESULT_TIMEOUT = 7,
};

/* Game adapter: called only by the game thread at its pause point. These
 * bounded in-memory operations must not perform I/O, wait for UI or save.
 * Query returns the catalogue size, writing at most capacity entries. */
unsigned CtrMystery_Query(int *states, unsigned capacity);
int CtrMystery_Activate(unsigned eventId);

#endif
