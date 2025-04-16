#include <windows.h>
#include <stdio.h>
#include <memoryapi.h>
#include <sysinfoapi.h>
#include <synchapi.h>
#include <timeapi.h>

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef int            i32;
typedef LPCWSTR str;

#define ON_PIXEL  0XFFFFFFFF
#define OFF_PIXEL 0x00000000

#define internal static
#define global_variable static
#define local_persist static

typedef struct
{
  str path;
  u8 *buffer;
  i32 buffer_size;
  u32 size;
} Program;

typedef struct
{
  u8 b0;
  u8 b1;
} Instruction;

typedef struct
{
  u8*      memory;
  u16      program_counter;
  u16      idx_reg;
  u16*     registers;
  Program* current_program;
} Emu;

internal void load_program(Program *program, u8* memory);
internal inline void NextInstruction(Emu* emu, Instruction* inst);

// instructions looks like (A - just any char)
// ANNN
// AXNN
// AXYN
internal inline u16 ExtractNNN(Instruction* inst);
internal inline u8  ExtractNN(Instruction* inst);
internal inline u8  ExtractN(Instruction* inst);
internal inline u8  ExtractX(Instruction* inst);
internal inline u8  ExtractY(Instruction* inst);

// --- win32 ---
internal HWND open_window(str title, i32 width, i32 height);
LRESULT CALLBACK Win32WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
internal void Win32ResizeDIBSection(i32 width, i32 height);
internal void Win32UpdateWindow(HDC device_context, RECT* WindowRect);
internal void Win32FillBuffer(u32 color);
// -------------

global_variable bool       Running = false;
global_variable BITMAPINFO bitmap_info;
global_variable void*      bitmap_buffer;
global_variable HBITMAP    bitmap_handle;
global_variable HDC        device_context;
global_variable i32        BitmapWidth = 64;
global_variable i32        BitmapHeight = 32;
global_variable i32        WindowWidth;
global_variable i32        WindowHeight;

i32 main()
{
  Program ibm_logo = {
    .path = (str)L"./ibm_logo.ch8",
    .buffer = (u8*)VirtualAlloc(NULL, 512, MEM_COMMIT, PAGE_READWRITE),
    .buffer_size = 512
  };

  Emu emu = {
    .memory = (u8*)VirtualAlloc(NULL, 4096, MEM_COMMIT, PAGE_READWRITE),
    .program_counter = 0,
    .idx_reg = 0,
    .registers = (u16*)VirtualAlloc(NULL, sizeof(u16) * 0xF, MEM_COMMIT, PAGE_READWRITE),
    .current_program = &ibm_logo
  };

  load_program(&ibm_logo, emu.memory);

  Instruction curr_inst = {0};

  const i32 WH_FACTOR = 10;
  const i32 WIDTH     = BitmapWidth * WH_FACTOR;
  const i32 HEIGHT    = BitmapHeight * WH_FACTOR;

  HWND window = open_window((str)L"8emu", WIDTH, HEIGHT);
  MSG msg;
  Running = true;
  ULONGLONG program_start = GetTickCount64();
  ULONGLONG current_frame_timestamp;

  timeBeginPeriod(1);
  double MS_PER_FRAME = 1000.0/60.0;
  while(Running)
  {
    current_frame_timestamp = GetTickCount64();

    while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }

    NextInstruction(&emu, &curr_inst);

    u8 high_nibble = curr_inst.b0 >> 4;

    // 00E0
    if(curr_inst.b0 == 0x00 && curr_inst.b1 == 0xE0)
    {
      Win32FillBuffer(OFF_PIXEL);
    }
    // ANNN
    else if(high_nibble == 0xA)
    {
      emu.idx_reg = ExtractNNN(&curr_inst);
    }
    // 6XNN
    else if(high_nibble == 0x6)
    {
      *(emu.registers + ExtractX(&curr_inst)) = ExtractNN(&curr_inst);
    }
    // DXYN
    else if (high_nibble == 0xD)
    {
      u8  VX = ExtractX(&curr_inst);
      u8  VY = ExtractY(&curr_inst);
      u16 x  = emu.registers[VX] & (BitmapWidth - 1);
      u16 y  = emu.registers[VY] & (BitmapHeight - 1);
      u8  n  = ExtractN(&curr_inst);
      emu.registers[0xF] = 0;

      for (u8 row = 0; row < n; row++)
      {
        u8 sprite_byte = emu.memory[emu.idx_reg + row];
        u16 curr_y = (y + row) & (BitmapHeight - 1);
        for (u8 col = 0; col < 8; col++)
        {
          u8 sprite_pixel = sprite_byte & (0b10000000 >> col);
          if (sprite_pixel)
          {
            u16 curr_x = (x + col) & (BitmapWidth - 1);
            u32* pixel = (u32*)bitmap_buffer + (curr_y * BitmapWidth + curr_x);
            if (*pixel)
            {
              emu.registers[0xF] = 0xFFFF;
            }
            *pixel ^= 0xFFFFFFFF;
          }
        }
      }
    }
    // 7XNN
    else if (high_nibble == 0x7)
    {
      u8 NN = ExtractNN(&curr_inst);
      u8 VX = ExtractX(&curr_inst);
      emu.registers[VX] += (u16)NN;
    }
    // 1NNN
    else if (high_nibble == 0xF)
    {
      u16 NNN = ExtractNNN(&curr_inst);
      emu.program_counter = NNN;
    }

    InvalidateRect(window, NULL, 0);
    UpdateWindow(window);

    ULONGLONG elapsed_time = (GetTickCount64() - current_frame_timestamp);
    if(elapsed_time < MS_PER_FRAME)
    {
      Sleep(MS_PER_FRAME - elapsed_time);
    }
  }

  printf("End Time: %llu\n", GetTickCount64() - program_start);

  return 0;
}

internal void
load_program(Program* program, u8* memory)
{
  HANDLE hfile = CreateFile(
    program->path,
    GENERIC_READ,
    FILE_SHARE_READ,
    NULL,
    OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL,
    NULL
  );

  if(hfile == INVALID_HANDLE_VALUE)
  {
    printf("Failed to open file: %ws %lu\n", program->path, GetLastError());
    return;
  }

  u32 program_start_offset = 0x200;
  u8* address_to_load = memory + program_start_offset;
  u32 memory_size = 4096;
  u32 max_to_read_bytes = 4096 - program_start_offset;

  DWORD bytes_read;
  if(ReadFile(hfile, address_to_load, max_to_read_bytes, &bytes_read, NULL))
  {
    address_to_load[bytes_read] = '\0';
    printf("Read %lu bytes\n", bytes_read);
  }
  else
  {
    printf("Failed to read file: %lu\n", GetLastError());
    return;
  }

  program->size = bytes_read;

  CloseHandle(hfile);
}

HWND open_window(str title, i32 width, i32 height)
{
  LPCWSTR CLASS_NAME = L"Sample Window Class";

  WNDCLASS wc = { };

  HMODULE hInstance = GetModuleHandle(NULL);

  wc.lpfnWndProc   = Win32WindowProc;
  wc.hInstance     = hInstance;
  wc.lpszClassName = CLASS_NAME;

  RegisterClass(&wc);

  HWND hwnd = CreateWindowEx(
    0,                              // Optional window styles.
    CLASS_NAME,                     // Window class
    title,                          // Window text
    WS_OVERLAPPEDWINDOW,            // Window style

    // Size and position
    CW_USEDEFAULT, CW_USEDEFAULT, width, height,

    NULL,       // Parent window
    NULL,       // Menu
    hInstance,  // Instance handle
    NULL        // Additional application data
    );

  if (hwnd == NULL)
  {
    return 0;
  }

  ShowWindow(hwnd, SW_SHOW);

  return hwnd;
}

LRESULT CALLBACK
Win32WindowProc(HWND Window, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  LRESULT result = 0;

  switch(uMsg)
  {
    case WM_SIZE:
    {
      RECT client_rect;
      GetClientRect(Window, &client_rect);
      i32 width = client_rect.right - client_rect.left;
      i32 height = client_rect.bottom - client_rect.top;
      Win32ResizeDIBSection(width, height);
    } break;
    case WM_CLOSE:
    {
      Running = false;
    } break;
    case WM_DESTROY:
    {
      Running = false;
    } break;
    case WM_PAINT:
    {
      PAINTSTRUCT paint;
      HDC device_context = BeginPaint(Window, &paint);
      i32 x = paint.rcPaint.left;
      i32 y = paint.rcPaint.top;
      i32 width = paint.rcPaint.right - paint.rcPaint.left;
      i32 height = paint.rcPaint.top - paint.rcPaint.bottom;
      Win32UpdateWindow(device_context, &paint.rcPaint);
      EndPaint(Window, &paint);
    } break;
    default:
      result = DefWindowProc(Window, uMsg, wParam, lParam);
  }

  return result;
}

internal void
Win32ResizeDIBSection(i32 width, i32 height)
{
  if (bitmap_buffer)
  {
    VirtualFree(bitmap_buffer, NULL, MEM_RELEASE);
  }

  bitmap_info.bmiHeader.biSize = sizeof(bitmap_info.bmiHeader);
  bitmap_info.bmiHeader.biWidth = BitmapWidth;
  bitmap_info.bmiHeader.biHeight = -BitmapHeight;
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;
  bitmap_info.bmiHeader.biSizeImage = 0;
  bitmap_info.bmiHeader.biXPelsPerMeter = 0;
  bitmap_info.bmiHeader.biYPelsPerMeter = 0;
  bitmap_info.bmiHeader.biClrUsed = 0;
  bitmap_info.bmiHeader.biClrImportant = 0;

  i32 bytes_per_pixel = 4;
  i32 bitmap_size = (BitmapWidth * BitmapHeight) * bytes_per_pixel;
  bitmap_buffer = VirtualAlloc(NULL, bitmap_size, MEM_COMMIT, PAGE_READWRITE);
}

internal void
Win32UpdateWindow(HDC device_context, RECT* WindowRect)
{
  WindowWidth = WindowRect->right - WindowRect->left;
  WindowHeight = WindowRect->bottom - WindowRect->top;

  StretchDIBits(
    device_context,

    // destination
    // x,
    // y,
    // width,
    // height,
    0, 0, WindowWidth, WindowHeight,

    // source
    // x,
    // y,
    // width,
    // height,
    0, 0, BitmapWidth, BitmapHeight,

    bitmap_buffer,
    &bitmap_info,
    DIB_RGB_COLORS,
    SRCCOPY
  );
}

internal void
Win32FillBuffer(u32 color)
{
  i32 BytesPerPixel = 4;
  i32 Pitch = BitmapWidth * BytesPerPixel;
  u8* Row = (u8*)bitmap_buffer;
  for (i32 y = 0; y < BitmapHeight; y++)
  {
    u32* Pixel = (u32*)Row;
    for (i32 x = 0; x < BitmapWidth; x++)
    {
      *Pixel++ = color;
    }
    Row += Pitch;
  }
}

internal inline void
NextInstruction(Emu* emu, Instruction* inst)
{
  local_persist u8* start_address = emu->memory + 0x200;
  inst->b0 = start_address[emu->program_counter];
  inst->b1 = start_address[emu->program_counter + 1];
  emu->program_counter += 2;
}

internal inline u16
ExtractNNN(Instruction* inst)
{
 u8 b0 = (u8)(inst->b0 << 4);
 u8 b1 = inst->b1;
 return (u16)((b0 << 4) | b1);
}

internal inline u8
ExtractNN(Instruction* inst)
{
  return inst->b1;
}

internal inline u8
ExtractN(Instruction* inst)
{
  return ((u8)(inst->b1 << 4)) >> 4;
}

internal inline u8
ExtractX(Instruction* inst)
{
  return ((u8)(inst->b0 << 4)) >> 4;
}

internal inline u8
ExtractY(Instruction* inst)
{
  return inst->b1 >> 4;
}