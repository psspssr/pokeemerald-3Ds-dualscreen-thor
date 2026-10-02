/*
 * JNI entry points for com.emerald3ds.android.NativeBridge and the two host
 * callbacks into Java (game exit, vibration).
 */
#include <android/native_window_jni.h>
#include <jni.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>

#include "ctr_host.h"
#include "ctr_host_internal.h"

#define BRIDGE_CLASS "com/emerald3ds/android/NativeBridge"

static JavaVM *sVm;
static jclass sBridgeClass;
static jmethodID sOnGameExit;
static jmethodID sVibrate;
static pthread_key_t sDetachKey;
static atomic_bool sExitNotified;

static void DetachThread(void *value)
{
    (void)value;
    if (sVm)
        (*sVm)->DetachCurrentThread(sVm);
}

/* Native threads stay attached until they end; the key destructor detaches. */
static JNIEnv *ThreadEnv(void)
{
    JNIEnv *env = NULL;
    JavaVMAttachArgs args = { JNI_VERSION_1_6, "emerald-native", NULL };

    if (!sVm)
        return NULL;
    if ((*sVm)->GetEnv(sVm, (void **)&env, JNI_VERSION_1_6) == JNI_OK)
        return env;
    if ((*sVm)->AttachCurrentThread(sVm, &env, &args) != JNI_OK)
        return NULL;
    pthread_setspecific(sDetachKey, (void *)1);
    return env;
}

JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved)
{
    JNIEnv *env;
    jclass local;

    (void)reserved;
    sVm = vm;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK)
        return JNI_ERR;
    pthread_key_create(&sDetachKey, DetachThread);
    /* FindClass only sees app classes here, not on the game thread. */
    local = (*env)->FindClass(env, BRIDGE_CLASS);
    if (!local)
    {
        (*env)->ExceptionClear(env);
        CtrHostJni_Log(ANDROID_LOG_ERROR, "class %s not found", BRIDGE_CLASS);
        return JNI_VERSION_1_6;
    }
    sBridgeClass = (*env)->NewGlobalRef(env, local);
    (*env)->DeleteLocalRef(env, local);
    sOnGameExit = (*env)->GetStaticMethodID(env, sBridgeClass, "onGameExit", "(I)V");
    if (!sOnGameExit)
        (*env)->ExceptionClear(env);
    sVibrate = (*env)->GetStaticMethodID(env, sBridgeClass, "vibrate", "(I)V");
    if (!sVibrate)
        (*env)->ExceptionClear(env);
    return JNI_VERSION_1_6;
}

static void CallStatic(jmethodID method, int arg)
{
    JNIEnv *env = ThreadEnv();

    if (!env || !sBridgeClass || !method)
        return;
    (*env)->CallStaticVoidMethod(env, sBridgeClass, method, (jint)arg);
    if ((*env)->ExceptionCheck(env))
    {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
    }
}

void CtrHost_NotifyGameExit(int status)
{
    if (atomic_exchange(&sExitNotified, true))
        return;
    CtrHostJni_Log(ANDROID_LOG_INFO, "game exited with status %d", status);
    CtrHost_SetState(CTR_HOST_EXITING);
    CallStatic(sOnGameExit, status);
}

void CtrHost_Vibrate(int milliseconds)
{
    if (milliseconds > 0)
        CallStatic(sVibrate, milliseconds);
}

static void ReadRect(JNIEnv *env, jintArray array, CtrHostRect *out)
{
    jint v[4] = { 0, 0, 0, 0 };

    if (array && (*env)->GetArrayLength(env, array) >= 4)
        (*env)->GetIntArrayRegion(env, array, 0, 4, v);
    out->x = v[0];
    out->y = v[1];
    out->w = v[2] > 0 ? v[2] : 0;
    out->h = v[3] > 0 ? v[3] : 0;
}

static int Clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

JNIEXPORT void JNICALL
Java_com_emerald3ds_android_NativeBridge_nativeInit(JNIEnv *env, jclass cls, jstring romfs, jstring sdmc)
{
    const char *r = romfs ? (*env)->GetStringUTFChars(env, romfs, NULL) : NULL;
    const char *s = sdmc ? (*env)->GetStringUTFChars(env, sdmc, NULL) : NULL;

    (void)cls;
    CtrHost_SetPaths(r, s);
    CtrHostJni_Log(ANDROID_LOG_INFO, "romfs=%s sdmc=%s", CtrHost_RomfsDir(), CtrHost_SdmcDir());
    if (r)
        (*env)->ReleaseStringUTFChars(env, romfs, r);
    if (s)
        (*env)->ReleaseStringUTFChars(env, sdmc, s);
}

JNIEXPORT jboolean JNICALL
Java_com_emerald3ds_android_NativeBridge_nativeStart(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return CtrHost_StartGame() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_emerald3ds_android_NativeBridge_nativeIsStarted(JNIEnv *env, jclass cls)
{
    (void)env;
    (void)cls;
    return CtrHost_GameStarted() ? JNI_TRUE : JNI_FALSE;
}

static void SetSurface(JNIEnv *env, int index, jobject surface)
{
    ANativeWindow *window;

    if (!surface)
    {
        CtrHost_SetWindowAt(index, NULL);
        return;
    }
    window = ANativeWindow_fromSurface(env, surface);
    CtrHost_SetWindowAt(index, window);
    if (window)
        ANativeWindow_release(window);
}

JNIEXPORT void JNICALL
Java_com_emerald3ds_android_NativeBridge_nativeSetSurface(JNIEnv *env, jclass cls, jobject surface)
{
    (void)cls;
    SetSurface(env, 0, surface);
}

JNIEXPORT void JNICALL
Java_com_emerald3ds_android_NativeBridge_nativeSetSurfaceAt(JNIEnv *env, jclass cls, jint index, jobject surface)
{
    (void)cls;
    SetSurface(env, index, surface);
}

JNIEXPORT void JNICALL
Java_com_emerald3ds_android_NativeBridge_nativeSetLayout(JNIEnv *env, jclass cls, jintArray top,
                                                         jintArray bottom, jint filter, jint background,
                                                         jint topWindow, jint bottomWindow)
{
    CtrHostLayout layout;

    (void)cls;
    memset(&layout, 0, sizeof(layout));
    ReadRect(env, top, &layout.top);
    ReadRect(env, bottom, &layout.bottom);
    layout.filter = filter ? 1 : 0;
    layout.background = (uint32_t)background & 0xFFFFFFu;
    layout.topWindow = topWindow;
    layout.bottomWindow = bottomWindow;
    CtrHost_SetLayout(&layout);
}

JNIEXPORT void JNICALL
Java_com_emerald3ds_android_NativeBridge_nativeSetInput(JNIEnv *env, jclass cls, jint keys, jint circleX,
                                                        jint circleY, jint touchX, jint touchY)
{
    CtrHostInput input;

    (void)env;
    (void)cls;
    input.keys = (uint32_t)keys;
    input.circleX = (int16_t)Clamp(circleX, -156, 156);
    input.circleY = (int16_t)Clamp(circleY, -156, 156);
    input.touchX = (uint16_t)Clamp(touchX, 0, 319);
    input.touchY = (uint16_t)Clamp(touchY, 0, 239);
    CtrHost_SetInput(&input);
}

JNIEXPORT void JNICALL
Java_com_emerald3ds_android_NativeBridge_nativeSetState(JNIEnv *env, jclass cls, jint state)
{
    (void)env;
    (void)cls;
    switch (state)
    {
    case 0:
        CtrHost_SetState(CTR_HOST_RUNNING);
        break;
    case 1:
        CtrHost_SetState(CTR_HOST_PAUSED);
        break;
    default:
        CtrHost_SetState(CTR_HOST_EXITING);
        break;
    }
}
