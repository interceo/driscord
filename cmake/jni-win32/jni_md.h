/*
 * Windows-flavored jni_md.h for MinGW cross-compilation.
 *
 * The host Linux JDK ships only linux/jni_md.h, so when we cross-compile the
 * JNI DLL we supply this file instead. Binary-compatible with the x64 Windows
 * JDK that will load the resulting core.dll at runtime.
 *
 * Derived from OpenJDK's src/java.base/windows/native/include/jni_md.h
 * (GPLv2 with Classpath exception).
 */

#ifndef _JAVASOFT_JNI_MD_H_
#define _JAVASOFT_JNI_MD_H_

#define JNIEXPORT __declspec(dllexport)
#define JNIIMPORT __declspec(dllimport)
#define JNICALL   __stdcall

/* Windows x64 is LLP64: long is 32-bit, matches the Windows JDK ABI. */
typedef long          jint;
typedef long long     jlong;
typedef signed char   jbyte;

#endif /* !_JAVASOFT_JNI_MD_H_ */
