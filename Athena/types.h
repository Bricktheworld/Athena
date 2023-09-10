#pragma once
#include <stdint.h>
#include <stdio.h>
#include <windows.h>
#include <immintrin.h>
#include <initializer_list>

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

// SSE2 support is _required_ for this engine.
typedef __m128 f32x4;
typedef __m256 f32x8;
typedef __m128d f64x2;
typedef __m256d f64x4;

typedef __m128i s8x16;
typedef __m128i s16x8;
typedef __m128i s32x4;
typedef __m128i s64x2;

typedef __m128i u8x16;
typedef __m128i u16x8;
typedef __m128i u32x4;
typedef __m128i u64x2;

template <typename T>
using InitializerList = std::initializer_list<T>;

#define U64_MAX ((u64)0xFFFFFFFFFFFFFFFF)
#define U32_MAX ((u32)0xFFFFFFFF)
#define U16_MAX ((u16)0xFFFF)
#define U8_MAX ((u8)0xFF)
#define S64_MAX ((s64)0x7FFFFFFFFFFFFFFF)
#define S32_MAX ((s32)0x7FFFFFFF)
#define S16_MAX ((s16)0x7FFF)
#define S8_MAX ((s8)0x7F)
#define S64_MIN ((s64)0x8000000000000000)
#define S32_MIN ((s32)0x80000000)
#define S16_MIN ((s16)0x8000)
#define S8_MIN ((s8)0x80)


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

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

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
#define HASSERT(hres) hres
#endif

#define UNREACHABLE ASSERT(false); __assume(0)


