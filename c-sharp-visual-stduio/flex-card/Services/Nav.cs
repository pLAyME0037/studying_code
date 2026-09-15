using Microsoft.Maui.Controls;
#if !(ANDROID || IOS || MACCATALYST || WINDOWS)
using Microsoft.Maui.Platform.Linux.Hosting;
#endif

namespace user_info.Services;

public static class Nav
{
    public static async Task PushAsync(Page page) {
#if ANDROID || IOS || MACCATALYST || WINDOWS
        if (Shell.Current is not null) {
            await Shell.Current.GoToAsync(page.GetType().Name);
        }
#else
        LinuxViewRenderer.PushPage(page);
        await Task.CompletedTask;
#endif
    }

    public static async Task PopAsync() {
#if ANDROID || IOS || MACCATALYST || WINDOWS
        if (Shell.Current is not null) {
            await Shell.Current.GoToAsync("..");
        }
#else
        LinuxViewRenderer.PopPage();
        await Task.CompletedTask;
#endif
    }

    public static async Task GoHomeAsync() {
#if ANDROID || IOS || MACCATALYST || WINDOWS
        if (Shell.Current is not null) {
            await Shell.Current.GoToAsync("//HomePage");
        }
#else
        LinuxViewRenderer.CurrentSkiaShell?.NavigateToSection(0);
        await Task.CompletedTask;
#endif
    }

    public static async Task GoToPageAsync(string route) {
#if ANDROID || IOS || MACCATALYST || WINDOWS
        if (Shell.Current is not null) {
            await Shell.Current.GoToAsync(route);
        }
#else
        // Absolute routes "//Foo" -> "Foo", relative "Foo" -> "Foo"
        string cleanRoute = route.TrimStart('/');
        LinuxViewRenderer.NavigateToRoute(cleanRoute);
        await Task.CompletedTask;
#endif
    }
}
