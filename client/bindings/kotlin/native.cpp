#include "driscord.h"
#include <jni.h>

static driscord_app g_app;

extern "C"
JNIEXPORT void JNICALL
Java_com_driscord_core_Native_create(JNIEnv*, jobject)
{
    g_app = driscord_create();
}