#ifndef ANDROID_MYSTERY_H
#define ANDROID_MYSTERY_H

/* Append-only IDs shared with the Android catalogue. */
enum CtrMysteryEventId
{
    CTR_MYSTERY_EON = 0,
    CTR_MYSTERY_MYSTIC = 1,
    CTR_MYSTERY_AURORA = 2,
    CTR_MYSTERY_OLD_SEA_MAP = 3,
    CTR_MYSTERY_JIRACHI = 4,
    CTR_MYSTERY_CELEBI = 5,
    CTR_MYSTERY_REGI_DOLLS = 6,
    CTR_MYSTERY_EVENT_COUNT = 7,
};

/* The game owns this transient session marker. Nothing is added to a save. */
void CtrMystery_ResetSession(void);
void CtrMystery_ContinueSession(void);
void CtrMystery_SavedSession(void);

#endif
