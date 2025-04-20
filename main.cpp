#include <stdio.h>
#include <windows.h>
#include <math.h>
#include <process.h>
#include <assert.h>
#include <stdlib.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int i32;
typedef LPCWSTR str;

#define internal static
#define global_variable static
#define local_persist static

typedef struct {
	str path;
	u8* buffer;
	i32 buffer_size;
	u32 size;
} Program;

typedef struct {
	u8 b0;
	u8 b1;
} Instruction;

typedef struct {
	u8* memory;
	u16 program_counter;
	u16 idx_reg;
	u8* registers;
	u8 delay_timer;
	u8 sound_timer;
	Program* current_program;
} Emu;

typedef struct {
	u16* memory;
	i32  size;
	i32 length;
} Stack;

internal void LoadProgram(Program* program, u8* memory);
internal inline void NextInstruction(Emu* emu, Instruction* inst);
internal inline Stack CreateStack(i32 size);
internal inline void PushStack(Stack* stack, u16 val);
internal inline u16 PopStack(Stack* stack);

// instructions looks like (A - just any char)
// ANNN
// AXNN
// AXYN
internal inline u16 ExtractNNN(Instruction* inst);
internal inline u8 ExtractNN(Instruction* inst);
internal inline u8 ExtractN(Instruction* inst);
internal inline u8 ExtractX(Instruction* inst);
internal inline u8 ExtractY(Instruction* inst);
internal void print_program(Emu* emu, Instruction* inst);

// --- win32 ---
internal HWND open_window(str title, i32 width, i32 height);
LRESULT CALLBACK Win32WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam,
	LPARAM lParam);
internal void Win32ResizeDIBSection(i32 width, i32 height);
internal void Win32UpdateWindow(HWND Window);
internal void Win32FillBuffer(u32 color);
// -------------

internal void Tick(Emu* emu, HWND Window);

global_variable bool Running = false;
global_variable bool Paused = false;
global_variable bool Debug = false;
global_variable BITMAPINFO bitmap_info;
global_variable void* bitmap_buffer;
global_variable HBITMAP bitmap_handle;
global_variable HDC device_context;
global_variable i32 BitmapWidth = 64;
global_variable i32 BitmapHeight = 32;
global_variable i32 WindowWidth;
global_variable i32 WindowHeight;

global_variable u16 FONT_CHARS[0x10 * 5] = {
0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
0x20, 0x60, 0x20, 0x20, 0x70, // 1
0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
0x90, 0x90, 0xF0, 0x10, 0x10, // 4
0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
0xF0, 0x10, 0x20, 0x40, 0x40, // 7
0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
0xF0, 0x90, 0xF0, 0x90, 0x90, // A
0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
0xF0, 0x80, 0x80, 0x80, 0xF0, // C
0xE0, 0x90, 0x90, 0x90, 0xE0, // D
0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};

i32 main() {
#ifdef STOP_EXEC
	Paused = true;
#endif

	Program program = {
		.path = (str)L"./roms/tetris.ch8",
		.buffer = (u8*)VirtualAlloc(NULL, 512, MEM_COMMIT, PAGE_READWRITE),
		.buffer_size = 512 };

	Emu emu = {
		.memory = (u8*)VirtualAlloc(NULL, 4096, MEM_COMMIT, PAGE_READWRITE),
		.program_counter = 0x200,
		.idx_reg = 0,
		.registers = (u8*)VirtualAlloc(NULL, sizeof(u8) * 0xF, MEM_COMMIT,
										 PAGE_READWRITE),
		.current_program = &program };

	for (i32 i = 0; i < 0x10; i++) {
		for (i32 j = 0; j < 0x5; j++) {
			i32 idx = i * 0x5 + j;
			emu.memory[idx] = FONT_CHARS[idx];
		}
	}

	Stack stack = CreateStack(1024);

	LoadProgram(&program, emu.memory);

	const i32 WH_FACTOR = 20;
	const i32 WIDTH = BitmapWidth * WH_FACTOR;
	const i32 HEIGHT = BitmapHeight * WH_FACTOR;

	HWND window = open_window((str)L"8emu", WIDTH, HEIGHT);
	MSG msg;
	Running = true;
	ULONGLONG program_start = GetTickCount64();
	ULONGLONG current_frame_timestamp;

	timeBeginPeriod(1);
	double MS_PER_FRAME = 1000.0 / 240.0;
	while (Running) {
		current_frame_timestamp = GetTickCount64();

		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

#ifdef _DEBUG
		if (GetAsyncKeyState(VK_F8) & 0x8000) {
			Tick(&emu, window);
		}
#endif

		if (Paused) {
			continue;
		}

		Tick(&emu, window);

		ULONGLONG elapsed_time = (GetTickCount64() - current_frame_timestamp);
		if (elapsed_time < MS_PER_FRAME) {
			Sleep(MS_PER_FRAME - elapsed_time);
		}
	}

	printf("End Time: %llu\n", GetTickCount64() - program_start);

	return 0;
}

internal void Tick(Emu* emu, HWND Window) {
	local_persist Instruction curr_inst = {0}; // TODO: move to Emu
	local_persist Stack stack = CreateStack(1024); // TODO: move to Emu

	NextInstruction(emu, &curr_inst);

	if (emu->delay_timer > 0) {
		emu->delay_timer--;
	}

	if (emu->sound_timer > 0) {
		emu->sound_timer--;
	}

	u8 high_nibble = curr_inst.b0 >> 4;
	u8 last_nibble = ExtractN(&curr_inst);

	// 00E0
	if (curr_inst.b0 == 0x00 && curr_inst.b1 == 0xE0) {
		Win32FillBuffer(0x00000000);
		Win32UpdateWindow(Window);
	}
	// 00EE
	else if (curr_inst.b0 == 0x00 && curr_inst.b1 == 0xEE) {
		emu->program_counter = PopStack(&stack);
	}
	// ANNN
	else if (high_nibble == 0xA) {
		emu->idx_reg = ExtractNNN(&curr_inst);
	}
	// 6XNN
	else if (high_nibble == 0x6) {
		u8 X = ExtractX(&curr_inst);
		u8 NN = ExtractNN(&curr_inst);
		*(emu->registers + X) = NN;
	}
	// DXYN
	else if (high_nibble == 0xD) {
		u8 VX = ExtractX(&curr_inst);
		u8 VY = ExtractY(&curr_inst);
		u16 x = emu->registers[VX] & (BitmapWidth - 1);
		u16 y = emu->registers[VY] & (BitmapHeight - 1);
		u8 n = last_nibble;
		emu->registers[0xF] = 0;

		for (u8 row = 0; row < n; row++) {
			u8 sprite_byte = emu->memory[emu->idx_reg + row];
			u16 curr_y = (y + row) & (BitmapHeight - 1);
			for (u8 col = 0; col < 8; col++) {
				u8 sprite_pixel = sprite_byte & (0b10000000 >> col);
				if (sprite_pixel) {
					u16 curr_x = (x + col) & (BitmapWidth - 1);
					u32* pixel = (u32*)bitmap_buffer + (curr_y * BitmapWidth + curr_x);
					if (*pixel) {
						emu->registers[0xF] = 0xFFFF;
					}
					*pixel ^= 0xFFFFFFFF;
				}
			}
		}
		Win32UpdateWindow(Window);
	}
	// 7XNN
	else if (high_nibble == 0x7) {
		u8 NN = ExtractNN(&curr_inst);
		u8 VX = ExtractX(&curr_inst);
		emu->registers[VX] += (u16)NN;
	}
	// 1NNN
	else if (high_nibble == 0x1) {
		u16 NNN = ExtractNNN(&curr_inst);
		emu->program_counter = NNN;
	}
	// 2NNN
	else if (high_nibble == 0x2) {
		u16 NNN = ExtractNNN(&curr_inst);
		PushStack(&stack, emu->program_counter);
		emu->program_counter = NNN;
	}
	// 3XNN
	else if (high_nibble == 0x3) {
		u8 NN = ExtractNN(&curr_inst);
		u8 VX = ExtractX(&curr_inst);
		if (emu->registers[VX] == NN) {
			emu->program_counter += 2;
		}
	}
	// 4XNN
	else if (high_nibble == 0x4) {
		u8 NN = ExtractNN(&curr_inst);
		u8 VX = ExtractX(&curr_inst);
		if (emu->registers[VX] != NN) {
			emu->program_counter += 2;
		}
	}
	// 5XY0
	else if (high_nibble == 0x5) {
		u8 VX = ExtractX(&curr_inst);
		u8 VY = ExtractY(&curr_inst);
		if (emu->registers[VX] == emu->registers[VY]) {
			emu->program_counter += 2;
		}
	}
	// 9XYN
	else if (high_nibble == 0x9) {
		u8 VX = ExtractX(&curr_inst);
		u8 VY = ExtractY(&curr_inst);
		if (emu->registers[VX] != emu->registers[VY]) {
			emu->program_counter += 2;
		}
	}
	// 8XY0
	else if (high_nibble == 0x8 && last_nibble == 0x0) {
		u8 VX = ExtractX(&curr_inst);
		u8 VY = ExtractY(&curr_inst);
		emu->registers[VX] = emu->registers[VY];
	}
	// 8XY1
	else if (high_nibble == 0x8 && last_nibble == 0x1) {
		u8 VX = ExtractX(&curr_inst);
		u8 VY = ExtractY(&curr_inst);
		emu->registers[VX] |= emu->registers[VY];
	}
	// 8XY2
	else if (high_nibble == 0x8 && last_nibble == 0x2) {
		u8 VX = ExtractX(&curr_inst);
		u8 VY = ExtractY(&curr_inst);
		emu->registers[VX] &= emu->registers[VY];
	}
	// 8XY3
	else if (high_nibble == 0x8 && last_nibble == 0x3) {
		u8 VX = ExtractX(&curr_inst);
		u8 VY = ExtractY(&curr_inst);
		emu->registers[VX] ^= emu->registers[VY];
	}
	// 8XY4
	else if (high_nibble == 0x8 && last_nibble == 0x4) {
		u8 VX = ExtractX(&curr_inst);
		u8 VY = ExtractY(&curr_inst);
		emu->registers[VX] += emu->registers[VY];
	}
	// 8XY5
	else if (high_nibble == 0x8 && last_nibble == 0x5) {
		u8 VX = ExtractX(&curr_inst);
		u8 VY = ExtractY(&curr_inst);
		emu->registers[VX] -= emu->registers[VY];
	}
	// 8XY7
	else if (high_nibble == 0x8 && last_nibble == 0x7) {
		u8 VX = ExtractX(&curr_inst);
		u8 VY = ExtractY(&curr_inst);
		emu->registers[VY] -= emu->registers[VX];
	}
	// 8XY6
	else if (high_nibble == 0x8 && last_nibble == 0x6) {
		u8 VX = ExtractX(&curr_inst);
		u8 VY = ExtractY(&curr_inst);
		u16 y = emu->registers[VY];
		emu->registers[VX] = y >> 1;
		u8 shifted_bit = (u16)(y << 15) >> 15;
		emu->registers[0xF] = shifted_bit;
	}
	// 8XYE
	else if (high_nibble == 0x8 && last_nibble == 0xe) {
		u8 VX = ExtractX(&curr_inst);
		u8 VY = ExtractY(&curr_inst);
		u16 y = emu->registers[VY];
		emu->registers[VX] = y << 1;
		u8 shifted_bit = y >> 15;
		emu->registers[0xF] = shifted_bit;
	}
	// FX29
	else if (high_nibble == 0xF && curr_inst.b1 == 0x29) {
		u8 VX = ExtractX(&curr_inst);
		emu->idx_reg = emu->registers[VX] * 5;
	}
	// FX07
	else if (high_nibble == 0xF && curr_inst.b1 == 0x07) {
		u8 VX = ExtractX(&curr_inst);
		emu->registers[VX] = emu->delay_timer;
	}
	// FX15
	else if (high_nibble == 0xF && curr_inst.b1 == 0x15) {
		u8 VX = ExtractX(&curr_inst);
		emu->delay_timer = emu->registers[VX];
	}
	// FX18
	else if (high_nibble == 0xF && curr_inst.b1 == 0x18) {
		u8 VX = ExtractX(&curr_inst);
		emu->sound_timer = emu->registers[VX];
	}
	// EXA1 or EX9E
	else if (high_nibble == 0xE) {
		u8 VX = ExtractX(&curr_inst);
		u16 key = emu->registers[VX];
		local_persist u8 key_map[16] = {
			'X', // 0
			'1', // 1
			'2', // 2
			'3', // 3
			'Q', // 4
			'W', // 5
			'E', // 6
			'A', // 7
			'S', // 8
			'D', // 9
			'Z', // A
			'C', // B
			'4', // C
			'R', // D
			'F', // E
			'V'  // F
		};
		bool Pressed = GetKeyState(key_map[key]) & 0x8000;
		if ((curr_inst.b1 == 0xA1 && !Pressed) || (curr_inst.b1 == 0x9e && Pressed)) {
			emu->program_counter += 2;
		}
	}
	// CXNN
	else if (high_nibble == 0xC) {
		u8 VX = ExtractX(&curr_inst);
		u8 NN = ExtractNN(&curr_inst);
		emu->registers[VX] = (rand() & 255) & NN;
	}
	// FX1E
	else if (high_nibble == 0xF && curr_inst.b1 == 0x1E) {
		u8 VX = ExtractX(&curr_inst);
		emu->idx_reg += emu->registers[VX];
	}
	// FX55
	else if (high_nibble == 0xF && curr_inst.b1 == 0x55) {
		u8 VX = ExtractX(&curr_inst);
		for (u8 i = 0; i <= VX; i++) {
			emu->memory[emu->idx_reg + i] = emu->registers[i];
		}
	}
	// FX65
	else if (high_nibble == 0xF && curr_inst.b1 == 0x65) {
		u8 VX = ExtractX(&curr_inst);
		for (u8 i = 0; i <= VX; i++) {
			emu->registers[i] = emu->memory[emu->idx_reg + i];
		}
	}
	// FX33
	else if (high_nibble == 0xF && curr_inst.b1 == 0x33) {
		u8 VX = ExtractX(&curr_inst);
		u16 x = emu->registers[VX];
		emu->memory[emu->idx_reg] = x / 100;
		emu->memory[emu->idx_reg + 1] = (x / 10) % 10;
		emu->memory[emu->idx_reg + 2] = x % 10;
	}
	else {
		printf("Unhandled Instruction %x %x\n", curr_inst.b0, curr_inst.b1);
	}

#ifdef _DEBUG
	if (Debug) {
		system("cls");
		print_program(emu, &curr_inst);
	}
#endif
}

internal void LoadProgram(Program* program, u8* memory) {
	HANDLE hfile = CreateFile(program->path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (hfile == INVALID_HANDLE_VALUE) {
		printf("Failed to open file: %ws %lu\n", program->path, GetLastError());
		return;
	}

	u8 temp_buffer[4096] = { 0 };

	DWORD bytes_read;
	if (ReadFile(hfile, temp_buffer, 4096, &bytes_read, NULL)) {
		temp_buffer[bytes_read] = '\0';
		printf("Read %lu bytes\n", bytes_read);
	}
	else {
		printf("Failed to read file: %lu\n", GetLastError());
		return;
	}

	program->size = bytes_read;

	for (u32 i = 0; i < program->size; i++) {
		memory[0x200 + i] = temp_buffer[i];
	}

	CloseHandle(hfile);
}

HWND open_window(str title, i32 width, i32 height) {
	LPCWSTR CLASS_NAME = L"Sample Window Class";

	WNDCLASS wc = {};

	HMODULE hInstance = GetModuleHandle(NULL);

	wc.lpfnWndProc = Win32WindowProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = CLASS_NAME;

	RegisterClass(&wc);

	HWND hwnd = CreateWindowEx(0,                    // Optional window styles.
		CLASS_NAME,           // Window class
		title,                // Window text
		WS_OVERLAPPEDWINDOW,  // Window style

		// Size and position
		CW_USEDEFAULT, CW_USEDEFAULT, width, height,

		NULL,       // Parent window
		NULL,       // Menu
		hInstance,  // Instance handle
		NULL        // Additional application data
	);

	if (hwnd == NULL) {
		return 0;
	}

	ShowWindow(hwnd, SW_SHOW);

	return hwnd;
}

LRESULT CALLBACK Win32WindowProc(HWND Window, UINT uMsg, WPARAM wParam,
	LPARAM lParam) {
	LRESULT result = 0;

	switch (uMsg) {
	case WM_SIZE: {
		RECT client_rect;
		GetClientRect(Window, &client_rect);
		i32 width = client_rect.right - client_rect.left;
		i32 height = client_rect.bottom - client_rect.top;
		Win32ResizeDIBSection(width, height);
	} break;
	case WM_CLOSE: {
		Running = false;
	} break;
	case WM_DESTROY: {
		Running = false;
	} break;
	case WM_PAINT: {
	    PAINTSTRUCT ps;
	    BeginPaint(Window, &ps);
	    EndPaint(Window, &ps);
	    return 0;
    } break;
	case WM_KEYDOWN: {
		// 0x75 - F6
		if (wParam == 0x75) {
			Paused = !Paused;
		}
		// 0x76 - F7
		if (wParam == 0x76) {
			Debug = !Debug;
		}
	} break;
	default:
		result = DefWindowProc(Window, uMsg, wParam, lParam);
	}

	return result;
}

internal void Win32ResizeDIBSection(i32 width, i32 height) {
	if (bitmap_buffer) {
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

internal void Win32UpdateWindow(HWND Window) {
	RECT WindowRect;
	GetClientRect(Window, &WindowRect);
	WindowWidth = WindowRect.right - WindowRect.left;
	WindowHeight = WindowRect.bottom - WindowRect.top;
	HDC DeviceContext = GetDC(Window);
	StretchDIBits(DeviceContext,

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

		bitmap_buffer, &bitmap_info, DIB_RGB_COLORS, SRCCOPY);
}

internal void Win32FillBuffer(u32 color) {
	i32 BytesPerPixel = 4;
	i32 Pitch = BitmapWidth * BytesPerPixel;
	u8* Row = (u8*)bitmap_buffer;
	for (i32 y = 0; y < BitmapHeight; y++) {
		u32* Pixel = (u32*)Row;
		for (i32 x = 0; x < BitmapWidth; x++) {
			*Pixel++ = color;
		}
		Row += Pitch;
	}
}

internal inline void NextInstruction(Emu* emu, Instruction* inst) {
	inst->b0 = emu->memory[emu->program_counter];
	inst->b1 = emu->memory[emu->program_counter + 1];
	emu->program_counter += 2;
}

internal inline u16 ExtractNNN(Instruction* inst) {
	u8 b0 = (u8)(inst->b0 << 4);
	u8 b1 = inst->b1;
	return (u16)((b0 << 4) | b1);
}

internal inline u8 ExtractNN(Instruction* inst) { return inst->b1; }

internal inline u8 ExtractN(Instruction* inst) {
	return inst->b1 & 0x0F;
}

internal inline u8 ExtractX(Instruction* inst) {
	return inst->b0 & 0x0F;
}

internal inline u8 ExtractY(Instruction* inst) {
	return inst->b1 >> 4;
}

internal void print_program(Emu* emu, Instruction* inst) {
	local_persist u16 start_address = 0x200;
	i32 rows = roundf((float)(emu->current_program->size / 0xF));
	for (i32 row = 0; row < rows; row++) {
		for (i32 col = 0; col < 0x10; col++) {
			i32 idx = row * 0x10 + col;
			if (emu->program_counter == idx + start_address) {
				printf("<");
			}
			printf("%02x", *((emu->memory + start_address) + idx));
			if (emu->program_counter == idx - 1 + start_address) {
				printf(">");
			}
			if (idx & (2 - 1)) {
				printf(" ");
			}
		}
		printf("\n");
	}
}

internal inline Stack CreateStack(i32 size) {
	Stack stack = {
		.memory = (u16*)VirtualAlloc(NULL, size, MEM_COMMIT, PAGE_READWRITE),
		.size = size,
		.length = 0
	};
	return stack;
}

internal inline void PushStack(Stack* stack, u16 val) {
	stack->memory[stack->length++] = val;
}

internal inline u16 PopStack(Stack* stack) {
	assert(stack->length > 0);
	stack->length--;
	return stack->memory[stack->length];
}