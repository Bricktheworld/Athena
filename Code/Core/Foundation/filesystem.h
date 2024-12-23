#pragma once
#include "Core/Foundation/types.h"
#include "Core/Foundation/Containers/error_or.h"

struct FileStream
{
  HANDLE handle = nullptr;
};

enum FileCreateFlags : u32
{
  kCreateTruncateExisting = 1 << 0,
};

enum FileError
{
  kFileOk,
  kFileFailedToCreate,
  kFileDoesNotExist,
};

FOUNDATION_API const char* file_error_to_str(FileError err);


FOUNDATION_API Result<FileStream, FileError> create_file(const char* path, FileCreateFlags flags);
FOUNDATION_API Result<FileStream, FileError> open_file(const char* path);
FOUNDATION_API void close_file(FileStream* file_stream);

FOUNDATION_API check_return bool write_file(FileStream file_stream, const void* src, u64 size);
FOUNDATION_API check_return bool read_file(FileStream file_stream, void* dst, u64 size);
FOUNDATION_API u64 get_file_size(FileStream file_stream);
FOUNDATION_API u32 get_parent_dir(const char* path, u32 len);
