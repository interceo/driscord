/*
 * Stub implementations of VA-API symbols referenced by FFmpeg's libavutil
 * (hwcontext_vaapi.o, hwcontext_vulkan.o) when built from MSYS2.
 *
 * VA-API is Linux-only in driscord; these symbols are never called at
 * runtime on Windows. The stubs exist solely to satisfy the static linker
 * when cross-compiling with MinGW.
 */

#include <stddef.h>

typedef void* VADisplay;
typedef int   VAStatus;

VAStatus    vaSetInfoCallback(VADisplay d, void *cb, void *ctx)  { (void)d;(void)cb;(void)ctx; return -1; }
VAStatus    vaInitialize(VADisplay d, int *maj, int *min)        { (void)d;(void)maj;(void)min; return -1; }
VADisplay   vaGetDisplayWin32(const void *native)                { (void)native; return NULL; }
const char* vaErrorStr(VAStatus s)                               { (void)s; return ""; }
VAStatus    vaTerminate(VADisplay d)                              { (void)d; return -1; }
VAStatus    vaGetDisplayAttributes(VADisplay d, void *a, int n)  { (void)d;(void)a;(void)n; return -1; }
const char* vaQueryVendorString(VADisplay d)                     { (void)d; return ""; }
VAStatus    vaSetDriverName(VADisplay d, char *name)             { (void)d;(void)name; return -1; }
