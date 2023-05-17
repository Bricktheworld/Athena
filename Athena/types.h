#pragma once
#include <stdint.h>
#include <stdio.h>
#include <windows.h>

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef float f32;
typedef double f64;

typedef unsigned char byte;


inline s32
v_dbgln(const char* fmt, va_list args)
{
	static constexpr u32 MAX_CHARS = 1024;
	char buf[MAX_CHARS];

	s32 written = vsnprintf(buf, MAX_CHARS - 2, fmt, args);
	buf[written] = '\n';
	buf[written + 1] = 0;

	OutputDebugStringA(buf);

	return written;
}

inline int
dbgln(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	s32 res = v_dbgln(fmt, args);

	va_end(args);

	return res;
}

template <typename T>
inline void
swap(T* a, T* b)
{
	T tmp = a;
	a = b;
	b = tmp;
}

#define KiB(val) (val * 1024LL)
#define MiB(val) (KiB(val) * 1024LL)
#define GiB(val) (MiB(val) * 1024LL)

#define ARRAY_LENGTH(arr) (sizeof(arr) / sizeof((arr)[0]))

#define pass_by_register __vectorcall

#define check_return [[nodiscard]]

#define constant static constexpr

// Credit for this beautiful macro: https://stackoverflow.com/a/42060129
struct __DeferDummy__ {};
template <class F> struct __Deferrer__ { F f; ~__Deferrer__() { f(); } };
template <class F> __Deferrer__<F> operator*(__DeferDummy__, F f) { return {f}; }
#define __DEFER_(LINE) zz_defer##LINE
#define __DEFER(LINE) __DEFER_(LINE)
#define defer auto __DEFER(__LINE__) = __DeferDummy__{} *[&]()

#define COM_RELEASE(com) \
	do \
	{ \
		if (com == nullptr) {} \
		else \
		{ \
			com->Release(); \
			com = nullptr; \
		} \
	} while (0)

#ifdef _DEBUG
#define DEBUG
#endif

//#ifdef DEBUG
#if 1
#define DEBUG_BREAK() __debugbreak()

#define ASSERT(expr) \
	do \
	{ \
		if (expr) { } \
		else \
		{ \
			dbgln("Assertion failed! %s, %d", __FILE__, __LINE__); \
			DEBUG_BREAK();  \
		} \
	} while(0)

#include <comdef.h>
#define HASSERT(hres) \
	do \
	{ \
		if (SUCCEEDED(hres)) { } \
		else \
		{ \
			_com_error err(hres); \
			const wchar_t* err_msg = err.ErrorMessage(); \
			dbgln("HRESULT failed (%s, %d): %ls", __FILE__, __LINE__, err_msg); \
			DEBUG_BREAK();  \
		} \
	} while(0)
#else
#define DEBUG_BREAK() do { } while(0)
#define ASSERT(expr) do { } while(0)
#define HASSERT(hres) do { } while(0)
#endif

#define UNREACHABLE ASSERT(false)


