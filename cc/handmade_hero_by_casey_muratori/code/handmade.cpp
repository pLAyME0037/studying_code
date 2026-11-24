#include <stdio.h>
#include <windows.h>
#include <stdint.h>

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
g_var bool Running = true;
g_var win32_bitmap_buffer GlobalBackBuffer;

struct win32_window_dimension {
    size_t Width;
    size_t Height;
};

win32_window_dimension Win32GetWindowDimension(HWND window) {
    win32_window_dimension Result;
    RECT ClientRect; 
    GetClientRect(window, &ClientRect);
    Result.Width  = ClientRect.right - ClientRect.left;
    Result.Height = ClientRect.bottom - ClientRect.top;

    return Result;
}

/* DIB Device Independent Bitmap is window GDI display that can be write in to
 * using bitmap */
internal void RenderRectGradientBG(win32_bitmap_buffer buffer, size_t x_offset, size_t y_offset) {
    // int Width = BitmapWidth;
    // int Height = BitmapHeight;
    int Pitch = buffer.Width*buffer.BytePerPixel;
    uint8_t *Row = (uint8_t *)buffer.Memory;
    for (size_t y = 0; y < (size_t)buffer.Height; ++y) {
        uint32_t *Pixel = (uint32_t *)Row;
        for (size_t x = 0; x < (size_t)buffer.Width; ++x) {
            uint8_t B = (x + x_offset);
            uint8_t G = (y + y_offset);
            *Pixel++ = ((G << 8)|B);
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
    buffer->Info.bmiHeader.biHeight      = buffer->Height;
    buffer->Info.bmiHeader.biPlanes      = 1;
    buffer->Info.bmiHeader.biBitCount    = 32;
    buffer->Info.bmiHeader.biCompression = BI_RGB;

    int BufferMemorySize = (buffer->Width*buffer->Height)*buffer->BytePerPixel;
    buffer->Memory = VirtualAlloc(0, BufferMemorySize, MEM_COMMIT, PAGE_READWRITE);

    // Pitch = width*buffer->BytePerPixel;
    // RenderRectGradientBG(255, 255); TODO: clear to black
}

internal void Win32DisplayToWindow(HDC device_ctx,
                                   int WindowWidth,
                                   int WindowHeight,
                                   win32_bitmap_buffer buffer,
                                   int x, int y,
                                   int width, int height)
{
    (void)width;
    (void)height;
    StretchDIBits(device_ctx,
                  x, y, WindowWidth, WindowHeight,
                  x, y, buffer.Width, buffer.Height,
                  buffer.Memory,
                  &buffer.Info,
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
        Running = false;
    } break;
    case WM_CLOSE: {
        // print message to user
        Running = false;
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

        Win32DisplayToWindow(DeviceContext,
                             Dimension.Width, Dimension.Height,
                             GlobalBackBuffer,
                             X, Y, Width, Height);
        EndPaint(window, &Paint); 
    } break;
    default: {
        OutputDebugStringA("Window Default\n");
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

    WNDCLASS window_class = {};
    window_class.style = CS_OWNDC|CS_HREDRAW|CS_VREDRAW;
    window_class.lpfnWndProc = Win32MainWindowCallback;
    window_class.hInstance = instance;
    // hIcon;
    window_class.lpszClassName = "handmade_hero_window_class";

    Win32ResizeDIBSection(&GlobalBackBuffer, 1720, 722);

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
            while (Running) {
                while (PeekMessageA(&Message, 0, 0, 0, PM_REMOVE)) {
                    if (Message.message == WM_QUIT) Running = false;
                    TranslateMessage(&Message);
                    DispatchMessageA(&Message);
                }

                RenderRectGradientBG(GlobalBackBuffer, XOffset, YOffset);

                HDC DeviceCtx = GetDC(Window);
                win32_window_dimension Dimension = Win32GetWindowDimension(Window);

                Win32DisplayToWindow(DeviceCtx,
                                     Dimension.Width, Dimension.Height,
                                     GlobalBackBuffer,
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
