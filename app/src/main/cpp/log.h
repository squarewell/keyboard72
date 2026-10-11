#pragma once

#include <cstdarg>
#include <cstdio>

#ifdef __ANDROID__
#include <android/log.h>
#define NIN_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "nin-shim", __VA_ARGS__)
#define NIN_LOGW(...) __android_log_print(ANDROID_LOG_WARN, "nin-shim", __VA_ARGS__)
#define NIN_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "nin-shim", __VA_ARGS__)
#else
#define NIN_LOGI(...) (std::fprintf(stderr, "[nin-shim] " __VA_ARGS__), std::fputc('\n', stderr))
#define NIN_LOGW(...) (std::fprintf(stderr, "[nin-shim W] " __VA_ARGS__), std::fputc('\n', stderr))
#define NIN_LOGE(...) (std::fprintf(stderr, "[nin-shim E] " __VA_ARGS__), std::fputc('\n', stderr))
#endif
