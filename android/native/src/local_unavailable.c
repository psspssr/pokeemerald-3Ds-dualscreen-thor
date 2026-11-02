/* Nintendo UDS has no Android transport. Keep the game session interface
 * explicit about availability; never simulate a connected or searching peer. */
#include <3ds.h>
#include "3ds_local.h"
#include "constants/local_link.h"

static uint8_t session = LOCAL_SESSION_OFF, kind;
void LocalLink_Init(void) { session = LOCAL_SESSION_OFF; kind = 0; }
void LocalLink_Shutdown(void) { LocalLink_Init(); }
uint8_t LocalLink_Enabled(void) { return 0; }
uint16_t LocalLink_MyId(void) { return 0; }
uint32_t LocalLink_Generation(void) { return 0; }
void LocalLink_SetAdvert(uint8_t hosting, uint8_t slot, const uint8_t *data)
{ (void)hosting; (void)slot; (void)data; }
int LocalLink_GetPeers(struct LocalPeer *out, int max) { (void)out; (void)max; return 0; }
void LocalLink_Send(uint16_t dest, const void *data, uint32_t len) { (void)dest; (void)data; (void)len; }
uint8_t LocalLink_PollEvent(struct LocalEvent *ev) { (void)ev; return 0; }
uint32_t LocalLink_Milliseconds(void) { return (uint32_t)(svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000)); }
void LocalLink_StartSession(uint8_t requested) { kind = requested; session = LOCAL_SESSION_FAILED; }
void LocalLink_LeaveSession(void) { LocalLink_Init(); }
uint8_t LocalLink_GetSession(void) { return session; }
uint8_t LocalLink_GetSessionKind(void) { return kind; }
uint8_t LocalLink_InSession(void) { return 0; }
void LocalLink_LeaveBeforeSleep(void) { LocalLink_Init(); }
void LocalLink_AfterWake(void) { LocalLink_Init(); }
