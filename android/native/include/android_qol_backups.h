#ifndef ANDROID_QOL_BACKUPS_H
#define ANDROID_QOL_BACKUPS_H

/* Game thread only, after a successful complete normal save. The counter
 * identifies the slot just committed; an older recovery slot is insufficient.
 * Disabled by default. Backup failures never change the game's save result. */
void CtrQol_BackupCompletedSave(unsigned counter);

#endif
