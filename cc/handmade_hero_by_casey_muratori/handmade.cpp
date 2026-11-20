#include <stdio.h>
#include <windows.h>

int CALLBACK WinMain(HINSTANCE instance,
                     HINSTANCE prev_instance,
                     LPSTR     cmd_line,
                     int       show_cmd)
{
    (void)prev_instance;
    (void)cmd_line;
    (void)show_cmd;

    WNDCLASS window_class = {0};
    window_class.style = CS_CLASSDC|CS_OWNDC|CS_HREDRAW|CS_VREDRAW;
    window_class.lpfnWndProc;
    // cbClsExtra;
    // cbWndExtra;
    window_class.hInstance = instance;
    // hIcon;
    // hCursor;
    // hbrBackground;
    // lpszMenuName;
    window_class.lpszClassName = "handmade_hero_window_class";

    MessageBoxA(NULL, "hello, world", "caption", 0); 
    return 0;
}
