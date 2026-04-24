#include "Core/Foundation/memory.h"
#include "Core/Foundation/filesystem.h"
#include <windows.h>

const char*
file_error_to_str(FileError err)
{
  switch (err)
  {
    case kFileOk:             return "Ok";
    case kFileFailedToCreate: return "Failed to create file";
    case kFileDoesNotExist:   return "File does not exist";
    default: UNREACHABLE;
  }
}

Result<FileStream, FileError>
create_file(const char* path, FileCreateFlags flags)
{
  FileStream ret = {0};

  DWORD creation_disposition = CREATE_NEW;
  if (flags & kCreateTruncateExisting)
  {
    creation_disposition = CREATE_ALWAYS;
  }
  HANDLE handle = CreateFileA(
    path,
    GENERIC_WRITE | GENERIC_READ,
    0,
    NULL,
    creation_disposition,
    FILE_ATTRIBUTE_NORMAL,
    NULL
  );

  if (handle == INVALID_HANDLE_VALUE)
  {
    return Err(kFileFailedToCreate);
  }
  ret.handle = handle;

  return Ok(ret);
}

static DWORD
to_win32_file_access_flags(FileStreamFlags flags)
{
  DWORD ret = 0;

  if (flags & kFileStreamRead)
  {
    ret |= GENERIC_READ;
  }

  if (flags & kFileStreamWrite)
  {
    ret |= GENERIC_WRITE;
  }

  // Sanity check to make sure we don't open the file in a silly state
  ASSERT_MSG_FATAL(ret != 0, "Specified neither read nor write access to file when opening! This is likely a programmer mistake.");

  return ret;
}


Result<FileStream, FileError>
open_file(const char* path, FileStreamFlags flags)
{
  FileStream ret = {0};

  DWORD access_flags = to_win32_file_access_flags(flags);

  HANDLE handle = CreateFileA(
    path,
    access_flags,
    FILE_SHARE_READ,
    NULL,
    OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL,
    NULL
  );

  if (handle == INVALID_HANDLE_VALUE)
  {
    return Err(kFileDoesNotExist);
  }

  ret.handle = handle;

  return Ok(ret);
}

Result<AsyncFileStream, FileError>
open_file_async(const char* path, FileStreamFlags flags)
{
  AsyncFileStream ret = {0};

  DWORD access_flags = to_win32_file_access_flags(flags);

  HANDLE handle = CreateFileA(
    path,
    access_flags,
    FILE_SHARE_READ,
    NULL,
    OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
    NULL
  );

  if (handle == INVALID_HANDLE_VALUE)
  {
    return Err(kFileDoesNotExist);
  }

  ret.file_handle        = handle;
  ret.io_completion_port = CreateIoCompletionPort(ret.file_handle, nullptr, 0, 0);

  return Ok(ret);
}

void
close_file(FileStream* file_stream)
{
  CloseHandle(file_stream->handle);
  zero_memory(file_stream, sizeof(FileStream));
}

void
close_file(AsyncFileStream* file_stream)
{
  CloseHandle(file_stream->io_completion_port);
  CloseHandle(file_stream->file_handle);
  zero_memory(file_stream, sizeof(FileStream));
}

// TODO(bshihabi): If you need to read more than 4 gigs this is not gonna work, need to support paging since apparently I can't write/read more than that from a file at a time on win32...
bool
write_file(FileStream file_stream, const void* src, u64 size)
{
  ASSERT_MSG_FATAL(size <= U32_MAX, "Writing to file with size %lu not supported currently due to 32-bit limit.", size);
  return WriteFile(file_stream.handle, src, (u32)size, NULL, NULL);
}

bool
read_file(FileStream file_stream, void* dst, u64 size, u64 offset)
{
  ASSERT_MSG_FATAL(size <= U32_MAX,   "Reading from file with size %lu not supported currently due to 32-bit limit.", size);
  ASSERT_MSG_FATAL(offset <= U32_MAX, "Seeking in files larger than U32_MAX (requested %lu) not implemented", offset);
  if (SetFilePointer(file_stream.handle, (LONG)offset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
  {
    return false;
  }

  return ReadFile(file_stream.handle, dst, (u32)size, NULL, NULL);
}

Result<void, FileError>
read_file(AsyncFileStream file_stream, AsyncFilePromise* out_promise, void* dst, u64 size, u64 offset)
{
  ASSERT_MSG_FATAL(size <= U32_MAX,   "Reading from file with size %lu not supported currently due to 32-bit limit.", size);

  out_promise->overlapped.Offset     = (DWORD)(offset & 0xFFFFFFFF);
  out_promise->overlapped.OffsetHigh = (DWORD)(offset >> 32);

  BOOL ok = ReadFile(file_stream.file_handle, dst, (u32)size, NULL, &out_promise->overlapped);

  DWORD err = GetLastError();
  if (!ok && err != ERROR_IO_PENDING)
  {
    char err_msg[512];
    FormatMessageA(
      FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr,
      err,
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      err_msg,
      sizeof(err_msg),
      nullptr
    );

    dbgln("Failed to read file: %s", err_msg);
    out_promise->io_completion_port = nullptr;
    return Err(kFileFailedToRead);
  }

  out_promise->io_completion_port = file_stream.io_completion_port;

  return Ok();
}

AwaitError
await_io(const AsyncFilePromise& promise, Option<u32> timeout_ms)
{
  // Either the struct wasn't filled correctly or it is a straight up error (kAsyncFileError)
  if (promise.io_completion_port == nullptr)
  {
    return kAwaitFailed;
  }

  DWORD       bytes_streamed = 0;
  ULONG_PTR   key            = 0;
  OVERLAPPED* overlapped     = nullptr;

  BOOL ok = GetQueuedCompletionStatus(promise.io_completion_port, &bytes_streamed, &key, &overlapped, unwrap_or(timeout_ms, INFINITE));

  if (!ok)
  {
    if (overlapped == nullptr && timeout_ms)
    {
      return kAwaitInFlight;
    }
    else
    {
      return kAwaitFailed;
    }
  }

  if (bytes_streamed == 0)
  {
    return kAwaitFailed;
  }

  return kAwaitCompleted;
}

bool
file_exists(const char* path)
{
  DWORD attributes = GetFileAttributesA(path);
  return (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY));
}

u64
get_file_size(FileStream file_stream)
{
  LARGE_INTEGER ret = {0};
  ret.QuadPart = 0;

  ASSERT(GetFileSizeEx(file_stream.handle, &ret));

  return ret.QuadPart;
}

u32
get_parent_dir(const char* path, u32 len)
{
  ASSERT_MSG_FATAL(len >= 1, "Path length must be greater than 0");
  for (u32 i = len - 1; i > 0; i--)
  {
    if (path[i] == '/' || path[i] == '\\')
    {
      return i + 1;
    }
  }

  return len;
}

u32
get_file_extension(const char* path, u32 len)
{
  ASSERT_MSG_FATAL(len >= 2, "Path length must be greater than 0");
  for (s64 i = len - 1; i >= 0; i--)
  {
    if (path[i] == '.')
    {
      return (u32)i;
    }
  }

  return len;
}