#include <stdio.h>
#include <windows.h>

LRESULT CALLBACK MainWindowCallback(HWND   window, 
                                    UINT   msg, 
                                    WPARAM w_param, 
                                    LPARAM l_param)
{
    LRESULT Result = 0;

    switch (msg) {
    case WM_SIZE: {
        OutputDebugStringA("WM_SIZE\n");
    } break;
    case WM_DESTROY: {
        OutputDebugStringA("WM_DESTORY\n");
    } break;
    case WM_CLOSE: {
        OutputDebugStringA("WM_CLOSE\n");
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
        static DWORD Operation = BLACKNESS;
        PatBlt(DeviceContext, X, Y, Width, Height, Operation);
        if (Operation == BLACKNESS) {
            Operation = WHITENESS;
        } else {
            Operation = BLACKNESS;
        }
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
    window_class.lpfnWndProc = MainWindowCallback;
    window_class.hInstance = instance;
    // hIcon;
    window_class.lpszClassName = "handmade_hero_window_class";

    if (RegisterClass(&window_class)) {
        HWND WindowHandle =
            CreateWindowEx(0, 
                           window_class.lpszClassName, 
                           "Handmade Hero", 
                           WS_OVERLAPPEDWINDOW|WS_VISIBLE, 
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           0, 
                           0, 
                           instance, 
                           0); 
        if (WindowHandle) {
            for (;;) {
                MSG Message;
                BOOL MessageResult = GetMessage(&Message, 0, 0, 0); 
                if (MessageResult > 0) {
                    TranslateMessage(&Message);
                    DispatchMessage(&Message);
                } else {
                    // TODO: Logging System
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
