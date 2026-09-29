using System.ComponentModel;
using Microsoft.Maui.Platform.Linux.Hosting;

namespace user_info.Views;

public partial class ExOffices : ContentPage
{
    public ExOffices() {
        InitializeComponent();
    }

    public async void OnExFacultyClicked(object sender, EventArgs e) {
#if WINDOW
        await Shell.Current.GoToAsync(nameof(ExFaculty));
#else
        LinuxViewRenderer.PushPage(new ExFaculty());
#endif
    }

    public async void OnExHomeClicked(object sender, EventArgs e) {
#if WINDOW
        await Shell.Current.GoToAsync(nameof(ExHome));
#else
        LinuxViewRenderer.PushPage(new ExHome());
#endif
    }

    public async void OnExAboutWuClicked(object sender, EventArgs e) {
#if WINDOW
        await Shell.Current.GoToAsync(nameof(ExAboutWu));
#else
        LinuxViewRenderer.PushPage(new ExAboutWu());
#endif
    }
}


