#ifndef CTR_HOST_INTERNAL_H
#define CTR_HOST_INTERNAL_H

#include <android/log.h>
#include <stdbool.h>

#define CTR_HOST_LOG_TAG "emerald-host"
#define CtrHostJni_Log(prio, ...) __android_log_print((prio), CTR_HOST_LOG_TAG, __VA_ARGS__)

bool CtrHost_GameStarted(void);

#endif
