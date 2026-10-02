/*
 * Force-included into src/music_player.c only.
 *
 * TrackStop calls ChnVolSetAsm before its definition, with no prototype in
 * scope. GCC accepts the later definition with a warning ("conflicting types",
 * the implicit declaration returned int); clang makes it a hard error that no
 * -Wno-error flag lowers. The prototype the definition has removes the
 * implicit declaration, so both compilers see the same program.
 */
#ifndef ANDROID_COMPAT_MUSIC_PLAYER_H
#define ANDROID_COMPAT_MUSIC_PLAYER_H

struct MixerSource;
struct MP2KTrack;
void ChnVolSetAsm(struct MixerSource *chan, struct MP2KTrack *track);

#endif
