#include "windows.h"
#include "dbghelp.h"
#pragma comment(lib, "DbgHelp.lib")

#include "Core/Foundation/types.h"
#include "Core/Foundation/assert.h"

void
print_backtrace()
{
  constant u32 kMaxStackCount = 128;
  void* stack[kMaxStackCount];

  HANDLE process = GetCurrentProcess();

  SymSetOptions(SYMOPT_LOAD_LINES);
  SymInitialize(process, NULL, TRUE);

  u16 frame_count = CaptureStackBackTrace(0, 100, stack, NULL);

  struct Symbol
  {
    SYMBOL_INFO info;
    char buf[256];
    IMAGEHLP_LINE64 line;
  };

  Symbol symbol = {0};

  symbol.info.MaxNameLen   = 255;
  symbol.info.SizeOfStruct = sizeof(SYMBOL_INFO);

  symbol.line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

  dbgln("=======CALLSTACK=======");
  for (u32 i = 1; i < frame_count; i++)
  {
    SymFromAddr(process, (DWORD64)stack[i], 0, &symbol.info);
    DWORD displacement;
    SymGetLineFromAddr64(process, (DWORD64)stack[i], &displacement, &symbol.line);

    dbgln("  [%u] %s (0x%0llX)\n    %s(%u)", frame_count - i - 1, symbol.info.Name, symbol.info.Address, symbol.line.FileName, symbol.line.LineNumber);
  }
  dbgln("=======================");
}
