using Microsoft.Extensions.Logging;

namespace remote_student_app;

public static class MauiProgram
{
    public static MauiAppBuilder CreateMauiAppBuilder()
    {
        var builder = MauiApp.CreateBuilder();
        builder
            .UseMauiApp<App>()
            .ConfigureFonts(fonts =>
            {
                fonts.AddFont("OpenSans-Regular.ttf", "OpenSansRegular");
                fonts.AddFont("OpenSans-Semibold.ttf", "OpenSansSemibold");
            });

#if DEBUG
    builder.Logging.AddDebug();
#endif

        return builder;
    }

    public static MauiApp CreateMauiApp()
    {
        return CreateMauiAppBuilder().Build();
    }
}
