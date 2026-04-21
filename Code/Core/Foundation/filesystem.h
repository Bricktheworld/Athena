#pragma once
#include "Core/Foundation/types.h"
#include "Core/Foundation/Containers/option.h"
#include "Core/Foundation/Containers/error_or.h"

struct FileStream
{
  HANDLE handle = nullptr;
};

struct AsyncFileStream
{
  HANDLE file_handle        = nullptr;
  HANDLE io_completion_port = nullptr;
};

struct AsyncFilePromise
{
  // This pointer is not _owned_ by the promise, so it does not need to be freed.
  // This could technically lead to a dangling pointer bug, but I haven't found a better way to structure these
  HANDLE io_completion_port = nullptr;
};

static constexpr AsyncFilePromise kAsyncFileError = AsyncFilePromise{ .io_completion_port = nullptr };

enum FileCreateFlags : u32
{
  kFileCreateFlagsNone = 0,
  kCreateTruncateExisting = 1 << 0,
};

enum FileStreamFlags : u32
{
  kFileStreamRead      = 1 << 0,
  kFileStreamWrite     = 1 << 1,
  kFileStreamReadWrite = kFileStreamRead | kFileStreamWrite,
};

enum FileError
{
  kFileOk,
  kFileFailedToCreate,
  kFileDoesNotExist,
  kFileFailedToRead,
};

enum AwaitError
{
  kAwaitCompleted,
  kAwaitInFlight,  // This is only returned if you await non-blocking
  kAwaitFailed,
};

FOUNDATION_API const char* file_error_to_str(FileError err);


FOUNDATION_API Result<FileStream,      FileError> create_file(const char* path, FileCreateFlags create_flags);
FOUNDATION_API Result<FileStream,      FileError> open_file(const char* path, FileStreamFlags flags);
FOUNDATION_API Result<AsyncFileStream, FileError> open_file_async(const char* path, FileStreamFlags flags);
FOUNDATION_API void close_file(FileStream*      file_stream);
FOUNDATION_API void close_file(AsyncFileStream* file_stream);

FOUNDATION_API DONT_IGNORE_RETURN bool write_file(FileStream file_stream, const void* src, u64 size);
FOUNDATION_API DONT_IGNORE_RETURN bool write_file(FileStream file_stream, const void* src, u64 size);
FOUNDATION_API DONT_IGNORE_RETURN bool read_file(FileStream file_stream, void* dst, u64 size, u64 offset);
FOUNDATION_API DONT_IGNORE_RETURN Result<AsyncFilePromise, FileError> read_file(AsyncFileStream file_stream, void* dst, u64 size, u64 offset);

FOUNDATION_API DONT_IGNORE_RETURN AwaitError await_io(const AsyncFilePromise& promise, Option<u32> timeout_ms = None);

FOUNDATION_API DONT_IGNORE_RETURN bool file_exists(const char* path);
FOUNDATION_API u64 get_file_size(FileStream file_stream);
FOUNDATION_API u32 get_parent_dir(const char* path, u32 len);

// Gets the offset from path including the "." of the file extension
// so something like this: 
//   const char* extension = path + get_file_extension(path, strlen(path));
//   extension is ".dds" or something
FOUNDATION_API u32 get_file_extension(const char* path, u32 len);
