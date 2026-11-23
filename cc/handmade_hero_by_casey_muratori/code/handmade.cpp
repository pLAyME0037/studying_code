#include <stdio.h>
#include <windows.h>

#define internal static
#define local_persist static
#define g_var static

// TODO: Temporary Global varible
g_var bool Running = true;

/* DIB Device Independent Bitmap is window GDI display that can be write in to
 * using bitmap */
g_var BITMAPINFO BitmapInfo;
g_var void *BitmapMemory;
g_var HBITMAP BitmapHandle;
g_var HDC BitmapDeviceContext;

internal void Win32ResizeDIBSection(int width, int height) {
    if (BitmapHandle) {
        DeleteObject(BitmapHandle);
    }
    if (!BitmapDeviceContext) {
        BitmapDeviceContext = CreateCompatibleDC(0);
    }

    BitmapInfo.bmiHeader.biSize          = sizeof(BitmapInfo.bmiHeader);
    BitmapInfo.bmiHeader.biWidth         = width;
    BitmapInfo.bmiHeader.biHeight        = height;
    BitmapInfo.bmiHeader.biPlanes        = 1;
    BitmapInfo.bmiHeader.biBitCount      = 32;
    BitmapInfo.bmiHeader.biCompression   = BI_RGB;
    BitmapInfo.bmiHeader.biSizeImage     = 0;
    BitmapInfo.bmiHeader.biXPelsPerMeter = 0;
    BitmapInfo.bmiHeader.biYPelsPerMeter = 0;
    BitmapInfo.bmiHeader.biClrUsed       = 0;
    BitmapInfo.bmiHeader.biClrImportant  = 0;

    HDC DeviceContext = CreateCompatibleDC(0);

    BitmapHandle = CreateDIBSection(DeviceContext,
                                    &BitmapInfo,
                                    DIB_RGB_COLORS,
                                    &BitmapMemory,
                                    NULL, NULL);
}

internal void Win32UpdateWindow(HDC device_ctx, int x, int y, int width, int height) {
    StretchDIBits(device_ctx,
                  x, y, width, height,
                  x, y, width, height,
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
        PAINTSTRUCT Paint;
        HDC DeviceContext = BeginPaint(window, &Paint);
        int X      = Paint.rcPaint.left;
        int Y      = Paint.rcPaint.top;
        int Height = Paint.rcPaint.bottom - Paint.rcPaint.top;
        int Width  = Paint.rcPaint.right - Paint.rcPaint.left;
        Win32UpdateWindow(DeviceContext, X, Y, Width, Height);
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
        HWND WindowHandle = CreateWindowEx(0, 
                                           window_class.lpszClassName, 
                                           "Handmade Hero", 
                                           WS_OVERLAPPEDWINDOW|WS_VISIBLE, 
                                           CW_USEDEFAULT, CW_USEDEFAULT,
                                           CW_USEDEFAULT, CW_USEDEFAULT,
                                           0, 0, instance, 0); 
        if (WindowHandle) {
            while (Running) {
                MSG Message;
                BOOL MessageResult = GetMessageA(&Message, 0, 0, 0); 
                if (MessageResult > 0) {
                    TranslateMessage(&Message);
                    DispatchMessageA(&Message);
                } else {
                    break;
                }
            }
        } else {
            // TODO: Logging System
        }
    } else {
        // TODO: Logging System
    }

    return 0;
}
