#include "Core/Foundation/memory.h"
#include "Core/Foundation/filesystem.h"
#include <windows.h>

using namespace fs;

FileStream
fs::create_file(const char* path, FileCreateFlags flags)
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

  ASSERT(handle != INVALID_HANDLE_VALUE);
  ret.handle = handle;

  return ret;
}

FileStream
fs::open_file(const char* path)
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

  ASSERT(handle != INVALID_HANDLE_VALUE);

  ret.handle = handle;

  return ret;
}

void
fs::close_file(FileStream* file_stream)
{
  CloseHandle(file_stream->handle);
  zero_memory(file_stream, sizeof(FileStream));
}

bool
fs::write_file(FileStream file_stream, const void* src, size_t size)
{
  return WriteFile(file_stream.handle, src, size, NULL, NULL);
}

bool
fs::read_file(FileStream file_stream, void* dst, size_t size)
{
  return ReadFile(file_stream.handle, dst, size, NULL, NULL);
}

u64
fs::get_file_size(FileStream file_stream)
{
  LARGE_INTEGER ret = {0};
  ret.QuadPart = 0;

  ASSERT(GetFileSizeEx(file_stream.handle, &ret));

  return ret.QuadPart;
}
