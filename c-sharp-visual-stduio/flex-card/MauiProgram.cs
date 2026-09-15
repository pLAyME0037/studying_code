using CommunityToolkit.Maui;
using Microsoft.Extensions.Logging;
using Microsoft.Maui.Platform.Linux.Hosting;
using Microsoft.Maui.Platform.Linux.MediaElement.Hosting;
using user_info.Services.WUCompany;
using user_info.Views;

namespace user_info;

public static class MauiProgram
{
    public static MauiAppBuilder CreateMauiAppBuilder() {
        var builder = MauiApp.CreateBuilder();
        builder
            .UseMauiApp<App>()
#if ANDROID || IOS || MACCATALYST || WINDOWS
            .UseMauiCommunityToolkit()
            .UseMauiCommunityToolkitMediaElement(isAndroidForegroundServiceEnabled: false)
#endif
            .ConfigureFonts(fonts => {
                fonts.AddFont("OpenSans-Regular.ttf", "OpenSansRegular");
                fonts.AddFont("OpenSans-Semibold.ttf", "OpenSansSemibold");
            })
            .UseLinux()
            .UseX11()
            .UseLinuxMediaElement();
        builder.Services.AddTransient<DBService>();
        builder.Services.AddTransient<WUCompanyLogin>();
        builder.Services.AddTransient<WUCompanyRegister>();

#if DEBUG
    builder.Logging.AddDebug();
#endif

        return builder;
    }

    public static MauiApp CreateMauiApp() {
        return CreateMauiAppBuilder().Build();
    }
}
