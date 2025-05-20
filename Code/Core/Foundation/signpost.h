#pragma once
#include "Core/Foundation/memory.h"

// This needs to be called for each thread
void init_signpost_buffer(AllocHeap heap);

void signpost_log_packet(const u64* packet, u32 size_in_bytes);

enum SignpostPacketType : u8
{
  kSignpostPacketBytes,
  kSignpostPacketFmtString,
};

template <typename T>
consteval u32 signpost_log_id_for_packet()
{
  static u32 s_LogIds = 0;
  return ++s_LogIds;
}

static constexpr u32 kMaxSignpostStrFormats = 2048;
inline static const char* g_SignpostStrFormats[2048];
inline constinit u32 g_SignpostFmtIds;

template <char const* Str, typename... Args>
struct alignas(u64) SignpostLogFmtStringPacket
{
private:
  static u32 init() 
  {
    u32 ret = g_SignpostFmtIds++;
    g_SignpostStrFormats[ret] = Str;
    return ret;
  }

  template <typename T>
  static u64 bit_convert_u64(T val)
  {
    union
    {
      T   val;
      u64 u64_data;
    } data;

    data.val = val;
    return data.u64_data;
  }

public:
  static const inline u32 id = init();
  static constexpr u64 packet_size = sizeof...(Args) * sizeof(u64);

  u64 m_Args[sizeof...(Args)];

  SignpostLogFmtStringPacket(Args... args) : m_Args { bit_convert_u64(args)... }
  {
  }
};

template <char const* Str>
struct SignpostLogFmtStringPacket<Str>
{
private:
  static u32 init() 
  {
    u32 ret = g_SignpostFmtIds++;
    g_SignpostStrFormats[ret] = Str;
    return ret;
  }

public:
  static const inline u32 id = init();
  static constexpr u64 packet_size = 0;

  SignpostLogFmtStringPacket() = default;
};

template <char const* Str, typename... Args>
auto make_signpost_log_fmt_string_packet(Args... args)
{
  return SignpostLogFmtStringPacket<Str, decltype(args)...>(args...);
}


template <typename T>
void signpost_log_args(u16 log_class, u32 log_id, SignpostPacketType type, T args)
{
  static_assert(alignof(T) >= alignof(u64) || sizeof(T) == 0, "Signposts must be aligned to 8 byte boundaries.");

  struct alignas(u64) Packet
  {
    u32 log_id    = 0;
    u16 log_class = 0;
    u8  core_id   = 0;
    u8  type      = 0;

    u64 timestamp = 0;

    T   args      = 0;
  };

  LARGE_INTEGER counter;
  QueryPerformanceCounter(&counter);

  Packet packet;
  packet.type      = type;
  packet.timestamp = (u64)counter.QuadPart;
  packet.log_id    = log_id;
  packet.log_class = log_class;
  packet.core_id   = (u8)GetCurrentProcessorNumber();
  packet.args      = args;

  static_assert(sizeof(Packet) % alignof(u64) == 0, "Signpost packet invalid size!");

  signpost_log_packet((u64*)&packet, sizeof(Packet));
}

inline void signpost_log(u16 log_class, u32 log_id, SignpostPacketType type)
{
  struct alignas(u64) Packet
  {
    u32 log_id    = 0;
    u16 log_class = 0;
    u8  core_id   = 0;
    u8  type      = 0;

    u64 timestamp = 0;
  };

  LARGE_INTEGER counter;
  QueryPerformanceCounter(&counter);

  Packet packet;
  packet.type      = type;
  packet.timestamp = (u64)counter.QuadPart;
  packet.log_id    = log_id;
  packet.log_class = log_class;
  packet.core_id   = (u8)GetCurrentProcessorNumber();

  static_assert(sizeof(Packet) % alignof(u64) == 0, "Signpost packet invalid size!");

  signpost_log_packet((u64*)&packet, sizeof(Packet));
}

#define SIGNPOST_LOG_FMT(log_class, fmt, ...)  \
  do                                \
  {                                 \
    static constexpr char __fmt_str[] = fmt; \
    auto __packet = make_signpost_log_fmt_string_packet<__fmt_str>(__VA_ARGS__); \
    if constexpr (__packet.packet_size == 0) \
    { \
      signpost_log(log_class, __packet.id, kSignpostPacketFmtString); \
    } \
    else \
    { \
      signpost_log_args<decltype(__packet)>(log_class, __packet.id, kSignpostPacketFmtString, __packet); \
    } \
  } \
  while (0)
