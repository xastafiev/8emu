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
	u16* memory;
	i32  size;
	i32 length;
} Stack;

typedef struct {
	u8* memory;
	u16 program_counter;
	u16 idx_reg;
	u8* registers;
	u8 delay_timer;
	u8 sound_timer;
	Program* current_program;
	Instruction current_instruction;
	Stack stack;

	bool running;
	bool paused;
	bool debug;
} Emu;

internal void LoadProgram(Program* program, u8* memory);
internal inline void NextInstruction(Emu* emu);
internal inline Stack CreateStack(i32 size);
internal inline void PushStack(Stack* stack, u16 val);
internal inline u16 PopStack(Stack* stack);

internal inline u16 ExtractNNN(Instruction* inst);
internal inline u8 ExtractNN(Instruction* inst);
internal inline u8 ExtractN(Instruction* inst);
internal inline u8 ExtractX(Instruction* inst);
internal inline u8 ExtractY(Instruction* inst);
internal void PrintProgram(Emu* emu, Instruction* inst);

internal HWND Win32OpenWindow(str title, i32 width, i32 height);
LRESULT CALLBACK Win32WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam,
	LPARAM lParam);
internal void Win32ResizeDIBSection(i32 width, i32 height);
internal void Win32UpdateWindow(HWND Window);
internal void Win32FillBuffer(u32 color);

internal void Tick(Emu* emu, HWND Window);

global_variable BITMAPINFO bitmap_info;
global_variable void* bitmap_buffer;
global_variable i32 BitmapWidth = 64;
global_variable i32 BitmapHeight = 32;
global_variable const u8 FONT_CHARS[0x10 * 5] = {
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
	Program program = {
		.path = (str)L"./roms/tetris.ch8",
		.buffer = (u8*)VirtualAlloc(NULL, 512, MEM_COMMIT, PAGE_READWRITE),
		.buffer_size = 512
	};

	Emu emu = {
		.memory = (u8*)VirtualAlloc(NULL, 4096, MEM_COMMIT, PAGE_READWRITE),
		.program_counter = 0x200,
		.idx_reg = 0,
		.registers = (u8*)VirtualAlloc(NULL, sizeof(u8) * 16, MEM_COMMIT, PAGE_READWRITE),
		.current_program = &program,
		.stack = CreateStack(1024)
	};

	memcpy(emu.memory, FONT_CHARS, sizeof(FONT_CHARS));

	LoadProgram(&program, emu.memory);

	HWND window = Win32OpenWindow((str)L"8emu", BitmapWidth * 20, BitmapHeight * 20);
	SetWindowLongPtr(window, GWLP_USERDATA, (LONG_PTR)&emu);
	ULONGLONG program_start = GetTickCount64();
	timeBeginPeriod(1);

	const double MS_PER_FRAME = 1000.0 / 1000.0; // Target: 1000 FPS
	MSG msg = {0};

	emu.running = true;
	while (emu.running) {
		ULONGLONG frame_start = GetTickCount64();

		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

#ifdef _DEBUG
		local_persist SHORT prev_state = 0;
		SHORT curr_state = GetAsyncKeyState(VK_F8) & 0x8000;
		if (!prev_state && curr_state) {
			Tick(&emu, window);
		}
		prev_state = curr_state;
#endif

		if (!emu.paused) {
			Tick(&emu, window);
		}

		ULONGLONG elapsed = GetTickCount64() - frame_start;
		if (elapsed < MS_PER_FRAME) {
			Sleep(MS_PER_FRAME - elapsed);
		}

		static u32 frame_counter = 0;
		static u8 prev_sound_timer = 0;
		frame_counter++;

		if (frame_counter >= 1000.0 / 60.0) {
			frame_counter = 0;
			if (emu.delay_timer > 0) emu.delay_timer--;
			if (emu.sound_timer > 0) {
				if (prev_sound_timer == 0) {
					Beep(0x400, emu.sound_timer * 20);
				}
				emu.sound_timer--;
			}
			prev_sound_timer = emu.sound_timer;
		}
	}

	printf("End Time: %llu ms\n", GetTickCount64() - program_start);

	return 0;
}


internal void Tick(Emu* emu, HWND Window) {
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

	NextInstruction(emu);

	u8 high_nibble = emu->current_instruction.b0 >> 4;
	u8 last_nibble = ExtractN(&emu->current_instruction);

	// 00E0
	if (emu->current_instruction.b0 == 0x00 && emu->current_instruction.b1 == 0xE0) {
		Win32FillBuffer(0x00000000);
		Win32UpdateWindow(Window);
	}
	// 00EE
	else if (emu->current_instruction.b0 == 0x00 && emu->current_instruction.b1 == 0xEE) {
		emu->program_counter = PopStack(&emu->stack);
	}
	// ANNN
	else if (high_nibble == 0xA) {
		emu->idx_reg = ExtractNNN(&emu->current_instruction);
	}
	// 6XNN
	else if (high_nibble == 0x6) {
		u8 X = ExtractX(&emu->current_instruction);
		u8 NN = ExtractNN(&emu->current_instruction);
		emu->registers[X] = NN;
	}
	// DXYN
	else if (high_nibble == 0xD) {
		u8 VX = ExtractX(&emu->current_instruction);
		u8 VY = ExtractY(&emu->current_instruction);
		u16 x = emu->registers[VX] & (BitmapWidth - 1);
		u16 y = emu->registers[VY] & (BitmapHeight - 1);
		emu->registers[0xF] = 0x0;

		for (u8 row = 0; row < last_nibble; row++) {
			u8 sprite_byte = emu->memory[emu->idx_reg + row];
			u16 curr_y = (y + row) & (BitmapHeight - 1);
			for (u8 col = 0; col < 8; col++) {
				u8 sprite_pixel = sprite_byte & (0b10000000 >> col);
				if (sprite_pixel) {
					u16 curr_x = (x + col) & (BitmapWidth - 1);
					u32* pixel = (u32*)bitmap_buffer + (curr_y * BitmapWidth + curr_x);
					if (*pixel != 0x00000000) {
						emu->registers[0xF] = 0x1;
					}
					*pixel ^= 0xFFFFFFFF;
				}
			}
		}
		Win32UpdateWindow(Window);
	}
	// BNNN
	else if (high_nibble == 0xB) {
		emu->program_counter = emu->registers[0] + ExtractNNN(&emu->current_instruction);
	}
	// 7XNN
	else if (high_nibble == 0x7) {
		emu->registers[ExtractX(&emu->current_instruction)] += ExtractNN(&emu->current_instruction);
	}
	// 1NNN
	else if (high_nibble == 0x1) {
		emu->program_counter = ExtractNNN(&emu->current_instruction);
	}
	// 2NNN
	else if (high_nibble == 0x2) {
		PushStack(&emu->stack, emu->program_counter);
		emu->program_counter = ExtractNNN(&emu->current_instruction);
	}
	// 3XNN
	else if (high_nibble == 0x3) {
		u8 NN = ExtractNN(&emu->current_instruction);
		u8 VX = ExtractX(&emu->current_instruction);
		if (emu->registers[VX] == NN) {
			emu->program_counter += 2;
		}
	}
	// 4XNN
	else if (high_nibble == 0x4) {
		u8 NN = ExtractNN(&emu->current_instruction);
		u8 VX = ExtractX(&emu->current_instruction);
		if (emu->registers[VX] != NN) {
			emu->program_counter += 2;
		}
	}
	// 5XY0
	else if (high_nibble == 0x5) {
		u8 VX = ExtractX(&emu->current_instruction);
		u8 VY = ExtractY(&emu->current_instruction);
		if (emu->registers[VX] == emu->registers[VY]) {
			emu->program_counter += 2;
		}
	}
	// 9XYN
	else if (high_nibble == 0x9) {
		u8 VX = ExtractX(&emu->current_instruction);
		u8 VY = ExtractY(&emu->current_instruction);
		if (emu->registers[VX] != emu->registers[VY]) {
			emu->program_counter += 2;
		}
	}
	// 8XY0
	else if (high_nibble == 0x8 && last_nibble == 0x0) {
		emu->registers[ExtractX(&emu->current_instruction)] = emu->registers[ExtractY(&emu->current_instruction)];
	}
	// 8XY1
	else if (high_nibble == 0x8 && last_nibble == 0x1) {
		emu->registers[ExtractX(&emu->current_instruction)] |= emu->registers[ExtractY(&emu->current_instruction)];
	}
	// 8XY2
	else if (high_nibble == 0x8 && last_nibble == 0x2) {
		emu->registers[ExtractX(&emu->current_instruction)] &= emu->registers[ExtractY(&emu->current_instruction)];
	}
	// 8XY3
	else if (high_nibble == 0x8 && last_nibble == 0x3) {
		u8 VX = ExtractX(&emu->current_instruction);
		u8 VY = ExtractY(&emu->current_instruction);
		emu->registers[ExtractX(&emu->current_instruction)] ^= emu->registers[ExtractY(&emu->current_instruction)];
	}
	// 8XY4
	else if (high_nibble == 0x8 && last_nibble == 0x4) {
		u8 VX = ExtractX(&emu->current_instruction);
		u8 VY = ExtractY(&emu->current_instruction);
		u32 result = (u32)emu->registers[VX] + (u32)emu->registers[VY];
		if (result > 255) {
			emu->registers[0xF] = 0x1;
		} else {
			emu->registers[0xF] = 0x0;
		}
		emu->registers[VX] = (u8)result;
	}
	// 8XY5
	else if (high_nibble == 0x8 && last_nibble == 0x5) {
		u8 VX = ExtractX(&emu->current_instruction);
		u8 VY = ExtractY(&emu->current_instruction);
		if (emu->registers[VX] > emu->registers[VY]) {
			emu->registers[0xF] = 0x1;
		} else {
			emu->registers[0xF] = 0x0;
		}
		emu->registers[VX] -= emu->registers[VY];
	}
	// 8XY7
	else if (high_nibble == 0x8 && last_nibble == 0x7) {
		u8 VX = ExtractX(&emu->current_instruction);
		u8 VY = ExtractY(&emu->current_instruction);
		if (emu->registers[VY] > emu->registers[VX]) {
			emu->registers[0xF] = 0x1;
		} else {
			emu->registers[0xF] = 0x0;
		}
		emu->registers[VX] = emu->registers[VY] - emu->registers[VX];
	}
	// 8XY6
	else if (high_nibble == 0x8 && last_nibble == 0x6) {
		u8 VX = ExtractX(&emu->current_instruction);
		u8 VY = ExtractY(&emu->current_instruction);
		u16 y = emu->registers[VY];
		emu->registers[0xF] = emu->registers[VX] & 0x1;
		emu->registers[VX] = y >> 1;
	}
	// 8XYE
	else if (high_nibble == 0x8 && last_nibble == 0xe) {
		u8 VX = ExtractX(&emu->current_instruction);
		u8 VY = ExtractY(&emu->current_instruction);
		u16 y = emu->registers[VY];
		emu->registers[0xF] = y >> 15;
		emu->registers[VX] = y << 1;
	}
	// FX29
	else if (high_nibble == 0xF && emu->current_instruction.b1 == 0x29) {
		emu->idx_reg = emu->registers[ExtractX(&emu->current_instruction)] * 5;
	}
	// FX07
	else if (high_nibble == 0xF && emu->current_instruction.b1 == 0x07) {
		emu->registers[ExtractX(&emu->current_instruction)] = emu->delay_timer;
	}
	// FX15
	else if (high_nibble == 0xF && emu->current_instruction.b1 == 0x15) {
		emu->delay_timer = emu->registers[ExtractX(&emu->current_instruction)];
	}
	// FX18
	else if (high_nibble == 0xF && emu->current_instruction.b1 == 0x18) {
		emu->sound_timer = emu->registers[ExtractX(&emu->current_instruction)];
	}
	// FX0A
	else if (high_nibble == 0xF && emu->current_instruction.b1 == 0x0A) {
		u8 VX = ExtractX(&emu->current_instruction);
		for (u8 i = 0; i < 16; i++) {
			if (GetAsyncKeyState(key_map[i]) & 0x8000) {
				emu->registers[VX] = i;
				break;
			}
		}
		emu->program_counter -= 2;
	}
	// EXA1 or EX9E
	else if (high_nibble == 0xE) {
		u8 VX = ExtractX(&emu->current_instruction);
		u16 key = emu->registers[VX];
		bool Pressed = GetKeyState(key_map[key]) & 0x8000;
		if ((emu->current_instruction.b1 == 0xA1 && !Pressed) || (emu->current_instruction.b1 == 0x9e && Pressed)) {
			emu->program_counter += 2;
		}
	}
	// CXNN
	else if (high_nibble == 0xC) {
		u8 VX = ExtractX(&emu->current_instruction);
		u8 NN = ExtractNN(&emu->current_instruction);
		double r = (double)rand() / RAND_MAX;
		emu->registers[VX] = (u8)(r * 256) & NN;
	}
	// FX1E
	else if (high_nibble == 0xF && emu->current_instruction.b1 == 0x1E) {
		emu->idx_reg += emu->registers[ExtractX(&emu->current_instruction)];
	}
	// FX55
	else if (high_nibble == 0xF && emu->current_instruction.b1 == 0x55) {
		u8 VX = ExtractX(&emu->current_instruction);
		for (u8 i = 0; i <= VX; i++) {
			emu->memory[emu->idx_reg + i] = emu->registers[i];
		}
	}
	// FX65
	else if (high_nibble == 0xF && emu->current_instruction.b1 == 0x65) {
		u8 VX = ExtractX(&emu->current_instruction);
		for (u8 i = 0; i <= VX; i++) {
			emu->registers[i] = emu->memory[emu->idx_reg + i];
		}
	}
	// FX33
	else if (high_nibble == 0xF && emu->current_instruction.b1 == 0x33) {
		u16 x = emu->registers[ExtractX(&emu->current_instruction)];
		emu->memory[emu->idx_reg + 0] = x / 100;
		emu->memory[emu->idx_reg + 1] = (x / 10) % 10;
		emu->memory[emu->idx_reg + 2] = x % 10;
	}
	else {
		printf("Unhandled Instruction %x %x\n", emu->current_instruction.b0, emu->current_instruction.b1);
	}

#ifdef _DEBUG
	if (emu->debug) {
		system("cls");
		PrintProgram(emu, &emu->current_instruction);
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

HWND Win32OpenWindow(str title, i32 width, i32 height) {
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

	Emu* emu = (Emu*)GetWindowLongPtr(Window, GWLP_USERDATA);

	switch (uMsg) {
	case WM_SIZE: {
		RECT client_rect;
		GetClientRect(Window, &client_rect);
		i32 width = client_rect.right - client_rect.left;
		i32 height = client_rect.bottom - client_rect.top;
		Win32ResizeDIBSection(width, height);
	} break;
	case WM_CLOSE: {
		emu->running = false;
	} break;
	case WM_DESTROY: {
		emu->running = false;
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
			emu->paused = !emu->paused;
		}
		// 0x76 - F7
		if (wParam == 0x76) {
			emu->debug = !emu->debug;
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
	i32 WindowWidth = WindowRect.right - WindowRect.left;
	i32 WindowHeight = WindowRect.bottom - WindowRect.top;
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

internal inline void NextInstruction(Emu* emu) {
	emu->current_instruction.b0 = emu->memory[emu->program_counter];
	emu->current_instruction.b1 = emu->memory[emu->program_counter + 1];
	emu->program_counter += 2;
}

internal inline u16 ExtractNNN(Instruction* inst) {
    return (u16)(((inst->b0 & 0x0F) << 8) | inst->b1);
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

internal void PrintProgram(Emu* emu, Instruction* inst) {
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
			if (idx & 1) {
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
	assert(stack->length < stack->size);
	stack->memory[stack->length++] = val;
}

internal inline u16 PopStack(Stack* stack) {
	assert(stack->length > 0);
	return stack->memory[--stack->length];
}
