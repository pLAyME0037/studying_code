#include <stdio.h>
#include <windows.h>
#include <stdint.h>

#define internal static
#define local_persist static
#define g_var static

// TODO: Temporary Global varible
g_var bool Running = true;
g_var int BitmapWidth;
g_var int BitmapHeight;
g_var int BytePerPixel = 4;

/* DIB Device Independent Bitmap is window GDI display that can be write in to
 * using bitmap */
g_var BITMAPINFO BitmapInfo;
g_var void *BitmapMemory;

internal void RenderRectGradientBG(size_t x_offset, size_t y_offset) {
    // int Width = BitmapWidth;
    // int Height = BitmapHeight;
    int Pitch = BitmapWidth*BytePerPixel;
    uint8_t *Row = (uint8_t *)BitmapMemory;
    for (size_t y = 0; y < (size_t)BitmapHeight; ++y) {
        uint32_t *Pixel = (uint32_t *)Row;
        for (size_t x = 0; x < (size_t)BitmapWidth; ++x) {
            uint8_t B = (x + x_offset);
            uint8_t G = (y + y_offset);
            *Pixel++ = ((G << 8)|B);
        }
        Row += Pitch;
    }
}

internal void Win32ResizeDIBSection(int width, int height) {
    if (BitmapMemory) {
        VirtualFree(BitmapMemory, 0, MEM_RELEASE);
    }
    BitmapWidth  = width;
    BitmapHeight = height;

    BitmapInfo.bmiHeader.biSize        = sizeof(BitmapInfo.bmiHeader);
    BitmapInfo.bmiHeader.biWidth       = BitmapWidth;
    BitmapInfo.bmiHeader.biHeight      = BitmapHeight;
    BitmapInfo.bmiHeader.biPlanes      = 1;
    BitmapInfo.bmiHeader.biBitCount    = 32;
    BitmapInfo.bmiHeader.biCompression = BI_RGB;

    int BitmapMemorySize = (BitmapWidth*BitmapHeight)*BytePerPixel;
    BitmapMemory = VirtualAlloc(0, BitmapMemorySize, MEM_COMMIT, PAGE_READWRITE);

    // RenderRectGradientBG(255, 255); TODO: clear to black
}

internal void Win32UpdateWindow(HDC device_ctx,
                                RECT *window_rect,
                                int x, int y,
                                int width, int height)
{
    (void)width;
    (void)height;
    int WindowWidth = window_rect->right - window_rect->left;
    int WindowHeight = window_rect->bottom - window_rect->top;
    StretchDIBits(device_ctx,
                  x, y, BitmapWidth, BitmapHeight,
                  x, y, WindowWidth, WindowHeight,
                  BitmapMemory,
                  &BitmapInfo,
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
        RECT ClientRect; 
        GetClientRect(window, &ClientRect);
        int Width  = ClientRect.right - ClientRect.left;
        int Height = ClientRect.bottom - ClientRect.top;
        Win32ResizeDIBSection(Width, Height);
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
        RECT ClientRect; 
        GetClientRect(window, &ClientRect);

        Win32UpdateWindow(DeviceContext, &ClientRect, X, Y, Width, Height);
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

                RenderRectGradientBG(XOffset, YOffset);

                HDC DeviceCtx = GetDC(Window);
                RECT ClientRect; 
                GetClientRect(Window, &ClientRect);
                int WindowWidth = ClientRect.right - ClientRect.left;
                int WindowHeight = ClientRect.bottom - ClientRect.top;
                Win32UpdateWindow(DeviceCtx,
                                  &ClientRect,
                                  0, 0,
                                  WindowWidth, WindowHeight);
                ReleaseDC(Window, DeviceCtx);

                ++XOffset;
            }
        } else {
            // TODO: Logging System
        }
    } else {
        // TODO: Logging System
    }

    return 0;
}
