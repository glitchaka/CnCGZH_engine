#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOGDI
#define NOGDI
#endif
#ifndef NOUSER
#define NOUSER
#endif
#include <windows.h>

#ifndef MB_ICONERROR
#define MB_ICONERROR 0x00000010L
#endif

extern "C" __declspec(dllimport) int __stdcall MessageBoxW(HWND, const wchar_t*, const wchar_t*, unsigned int);
#endif
