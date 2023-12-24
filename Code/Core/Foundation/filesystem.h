#pragma once
#include "Core/Foundation/types.h"

namespace fs
{
  struct FileStream
  {
    HANDLE handle = nullptr;
  };

  enum FileCreateFlags : u32
  {
    kCreateTruncateExisting = 1 << 0,
  };

  FOUNDATION_API FileStream create_file(const char* path, FileCreateFlags flags);
  FOUNDATION_API FileStream open_file(const char* path);
  FOUNDATION_API void close_file(FileStream* file_stream);

  FOUNDATION_API check_return bool write_file(FileStream file_stream, const void* src, size_t size);
  FOUNDATION_API check_return bool read_file(FileStream file_stream, void* dst, size_t size);
  FOUNDATION_API u64 get_file_size(FileStream file_stream);
}