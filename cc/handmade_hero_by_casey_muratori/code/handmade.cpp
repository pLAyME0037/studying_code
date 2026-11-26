#include <stdio.h>
#include <windows.h>
#include <stdint.h>
#include <xinput.h>

#define internal static
#define local_persist static
#define g_var static

struct win32_bitmap_buffer {
    BITMAPINFO Info;
    void *Memory;
    int Width;
    int Height;
    int BytePerPixel;
};

// TODO: Temporary Global varible
g_var bool G_Running = true;
g_var win32_bitmap_buffer G_BackBuffer;

struct win32_window_dimension {
    size_t Width;
    size_t Height;
};

#define X_Input_Get_State(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_STATE* pState)
typedef X_Input_Get_State(x_input_get_state);
X_Input_Get_State(XInputGetStateStub) { return 0; }
g_var x_input_get_state *XInputGetState_ = XInputGetStateStub;
#define XInputGetState XInputGetState_

#define X_Input_Set_State(name) DWORD WINAPI name(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration)
typedef X_Input_Set_State(x_input_set_state);
X_Input_Set_State(XInputSetStateStub) { return 0; }
g_var x_input_set_state *XInputSetState_ = XInputSetStateStub;
#define XInputSetState XInputSetState_

internal void Win32LoadInput(void) {
    HMODULE XInputLibrary = LoadLibrary("xinput1_3.dll");
    if (XInputLibrary) {
        XInputGetState = (x_input_get_state *)GetProcAddress(XInputLibrary, "XInputGetState");
        XInputSetState = (x_input_set_state *)GetProcAddress(XInputLibrary, "XInputSetState");
    }
}

internal win32_window_dimension Win32GetWindowDimension(HWND window) {
    win32_window_dimension Result;
    RECT ClientRect;
    GetClientRect(window, &ClientRect);
    Result.Width  = ClientRect.right - ClientRect.left;
    Result.Height = ClientRect.bottom - ClientRect.top;

    return Result;
}

/* DIB Device Independent Bitmap is window GDI display that can be write in to
 * using bitmap */
internal void RenderRectGradientBG(win32_bitmap_buffer *buffer,
                                   size_t red_offset,
                                   size_t green_offset,
                                   size_t blue_offset)
{
    // int Width = BitmapWidth;
    // int Height = BitmapHeight;
    int Pitch = buffer->Width*buffer->BytePerPixel;
    uint8_t *Row = (uint8_t *)buffer->Memory;
    for (size_t y = 0; y < (size_t)buffer->Height; ++y) {
        uint32_t *Pixel = (uint32_t *)Row;
        for (size_t x = 0; x < (size_t)buffer->Width; ++x) {
            uint8_t R = (x - red_offset);
            uint8_t B = (x + green_offset);
            uint8_t G = (y + blue_offset);
            *Pixel++ = ((R << (8 * 2))|(G << (8 * 1))|B);
        }
        Row += Pitch;
    }
}

internal void Win32ResizeDIBSection(win32_bitmap_buffer *buffer,
                                    int width, int height)
{
    if (buffer->Memory) {
        VirtualFree(buffer->Memory, 0, MEM_RELEASE);
    }
    buffer->Width  = width;
    buffer->Height = height;
    buffer->BytePerPixel = 4;

    buffer->Info.bmiHeader.biSize        = sizeof(buffer->Info.bmiHeader);
    buffer->Info.bmiHeader.biWidth       = buffer->Width;
    buffer->Info.bmiHeader.biHeight      = -buffer->Height;
    buffer->Info.bmiHeader.biPlanes      = 1;
    buffer->Info.bmiHeader.biBitCount    = 32;
    buffer->Info.bmiHeader.biCompression = BI_RGB;

    int BufferMemorySize = (buffer->Width*buffer->Height)*buffer->BytePerPixel;
    buffer->Memory = VirtualAlloc(0, BufferMemorySize, MEM_COMMIT, PAGE_READWRITE);

    // Pitch = width*buffer->BytePerPixel;
    // RenderRectGradientBG(255, 255); TODO: clear to black
}

internal void Win32DisplayToWindow(win32_bitmap_buffer *buffer,
                                   HDC device_ctx,
                                   int WindowWidth,
                                   int WindowHeight,
                                   int x, int y,
                                   int width, int height)
{
    (void)width;
    (void)height;
    StretchDIBits(device_ctx,
                  x, y, WindowWidth, WindowHeight,
                  x, y, buffer->Width, buffer->Height,
                  buffer->Memory,
                  &buffer->Info,
                  DIB_RGB_COLORS,
                  SRCCOPY);
}

LRESULT CALLBACK Win32MainWindowCallback(HWND   window,
                                         UINT   msg,
                                         WPARAM w_param,
                                         LPARAM l_param)
{
    LRESULT Result = 0;

    switch (msg) {
    case WM_SIZE: {
    } break;
    case WM_DESTROY: {
        // handle error recreate window
        G_Running = false;
    } break;
    case WM_CLOSE: {
        // print message to user
        G_Running = false;
    } break;
    case WM_SYSKEYUP:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_KEYDOWN: {
        uint16_t VKCode = w_param;
        bool WasDown = ((l_param & (1 << 30)) != 0);
        bool IsDown  = ((l_param & (1 << 31)) == 0);
        if (WasDown != IsDown) {
            if (VKCode == 'W') { }
            if (VKCode == 'A') { }
            if (VKCode == 'S') { }
            if (VKCode == 'D') { }
            if (VKCode == 'Q') { }
            if (VKCode == 'E') { }
            if (VKCode == VK_UP) { }
            if (VKCode == VK_DOWN) { }
            if (VKCode == VK_LEFT) { }
            if (VKCode == VK_RIGHT) { }
            if (VKCode == VK_ESCAPE) { }
            if (VKCode == VK_SPACE) {
                OutputDebugStringA("Escape: ");
                if (IsDown) {
                    OutputDebugStringA("Is Down\n");
                }
                if (WasDown) {
                    OutputDebugStringA("Was Down\n");
                }
            }
        }
    } break;
    case WM_ACTIVATEAPP: {
        OutputDebugStringA("WM_ACTIVATEAPP\n");
    } break;
    case WM_PAINT: {
        PAINTSTRUCT Paint = {};
        int X      = Paint.rcPaint.left;
        int Y      = Paint.rcPaint.top;
        int Height = Paint.rcPaint.bottom - Paint.rcPaint.top;
        int Width  = Paint.rcPaint.right - Paint.rcPaint.left;

        HDC DeviceContext = BeginPaint(window, &Paint);
        win32_window_dimension Dimension = Win32GetWindowDimension(window);

        Win32DisplayToWindow(&G_BackBuffer,
                             DeviceContext,
                             Dimension.Width, Dimension.Height,
                             X, Y, Width, Height);
        EndPaint(window, &Paint);
    } break;
    default: {
        Result = DefWindowProc(window, msg, w_param, l_param);
    }
    }
    return Result;
}

int CALLBACK WinMain(HINSTANCE instance,
                     HINSTANCE prev_instance,
                     LPSTR     cmd_line,
                     int       show_code)
{
    (void)prev_instance;
    (void)cmd_line;
    (void)show_code;

    Win32LoadInput();

    WNDCLASS window_class = {};
    window_class.style       = CS_OWNDC|CS_HREDRAW|CS_VREDRAW;
    window_class.lpfnWndProc = Win32MainWindowCallback;
    window_class.hInstance   = instance;
    // hIcon;
    window_class.lpszClassName = "handmade_hero_window_class";

    Win32ResizeDIBSection(&G_BackBuffer, 1720, 722);

    if (RegisterClass(&window_class)) {
        HWND Window = CreateWindowEx(0,
                                     window_class.lpszClassName,
                                     "Handmade Hero",
                                     WS_OVERLAPPEDWINDOW|WS_VISIBLE,
                                     CW_USEDEFAULT, CW_USEDEFAULT,
                                     CW_USEDEFAULT, CW_USEDEFAULT,
                                     0, 0, instance, 0);
        if (Window) {
            size_t XOffset = 0;
            size_t YOffset = 0;
            MSG Message;
            while (G_Running) {
                while (PeekMessageA(&Message, 0, 0, 0, PM_REMOVE)) {
                    if (Message.message == WM_QUIT) G_Running = false;
                    TranslateMessage(&Message);
                    DispatchMessageA(&Message);
                }

                for (DWORD ctrl_idx = 0; ctrl_idx < XUSER_MAX_COUNT; ++ctrl_idx) {
                    XINPUT_STATE CtrlState;
                    if (XInputGetState(ctrl_idx, &CtrlState) == ERROR_SUCCESS) {
                        XINPUT_GAMEPAD *Pad = &CtrlState.Gamepad;
                        bool Up       = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_UP);
                        bool Down     = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN);
                        bool Left     = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT);
                        bool Right    = (Pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT);
                        bool Start    = (Pad->wButtons & XINPUT_GAMEPAD_START);
                        bool Back     = (Pad->wButtons & XINPUT_GAMEPAD_BACK);
                        bool LSholder = (Pad->wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER);
                        bool RSholder = (Pad->wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER);
                        bool AButton  = (Pad->wButtons & XINPUT_GAMEPAD_A);
                        bool BButton  = (Pad->wButtons & XINPUT_GAMEPAD_B);
                        bool XButton  = (Pad->wButtons & XINPUT_GAMEPAD_X);
                        bool YButton  = (Pad->wButtons & XINPUT_GAMEPAD_Y);

                        int16_t StickX = Pad->sThumbLX;
                        int16_t StickY = Pad->sThumbLY;
                    } else {
                        // Controller is Not available
                    }
                }
                XINPUT_VIBRATION Vibration;
                Vibration.wLeftMotorSpeed = 6000;
                Vibration.wRightMotorSpeed = 6000;
                XInputSetState(0, &Vibration);

                RenderRectGradientBG(&G_BackBuffer, XOffset, XOffset, YOffset);

                HDC DeviceCtx = GetDC(Window);
                win32_window_dimension Dimension = Win32GetWindowDimension(Window);

                Win32DisplayToWindow(&G_BackBuffer,
                                     DeviceCtx,
                                     Dimension.Width, Dimension.Height,
                                     0, 0,
                                     Dimension.Width, Dimension.Height);
                ReleaseDC(Window, DeviceCtx);

                ++XOffset;
                YOffset += 2;
            }
        } else {
            // TODO: Logging System
        }
    } else {
        // TODO: Logging System
    }

    return 0;
}
