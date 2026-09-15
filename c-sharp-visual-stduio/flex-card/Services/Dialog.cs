using Microsoft.Maui.Controls;
using Microsoft.Maui.Platform.Linux.Hosting;
using user_info.Views;

namespace user_info.Services;

public static class Dialog
{
    public static async Task ShowAlertAsync(string title,
                                            string message)
    {
#if ANDROID || IOS || MACCATALYST || WINDOWS
        var mainPage = Application.Current?.MainPage;
        if (mainPage != null) {
            await mainPage.Navigation.PushModalAsync(new SimpleDialogPage(title, message));
        }
#else
        LinuxViewRenderer.PushPage(new SimpleDialogPage(title, message));
        await Task.CompletedTask;
#endif    
    }
}


