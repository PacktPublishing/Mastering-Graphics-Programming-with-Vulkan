#pragma once

typedef int BOOL;
typedef char CHAR;
typedef unsigned long DWORD;
typedef DWORD* LPDWORD;
typedef unsigned long long DWORD64;
typedef unsigned long long ULONGLONG;
typedef const wchar_t* LPCWSTR;
typedef const char* LPCSTR;
typedef char* LPSTR;
typedef void* HANDLE;
typedef void* PVOID;
typedef void* LPVOID;

typedef struct _SECURITY_ATTRIBUTES SECURITY_ATTRIBUTES;

#define FORWARD_DECLARE_HANDLE(name) struct name##__; typedef struct name##__ *name

FORWARD_DECLARE_HANDLE( HINSTANCE );
FORWARD_DECLARE_HANDLE( HWND );
FORWARD_DECLARE_HANDLE( HMONITOR );

#define NULL 0
