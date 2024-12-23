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

Result<FileStream, FileError>
open_file(const char* path)
{
  FileStream ret = {0};

  HANDLE handle = CreateFileA(
    path,
    GENERIC_WRITE | GENERIC_READ,
    0,
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

void
close_file(FileStream* file_stream)
{
  CloseHandle(file_stream->handle);
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
read_file(FileStream file_stream, void* dst, u64 size)
{
  ASSERT_MSG_FATAL(size <= U32_MAX, "Reading from file with size %lu not supported currently due to 32-bit limit.", size);
  return ReadFile(file_stream.handle, dst, (u32)size, NULL, NULL);
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