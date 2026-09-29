using System.ComponentModel;
using Microsoft.Maui.Platform.Linux.Hosting;

namespace user_info.Views;

public partial class ExHome : ContentPage
{
    public ExHome() {
        InitializeComponent();
    }

    public async void OnExAboutWuClicked(object sender, EventArgs e) {
#if WINDOW
        await Shell.Current.GoToAsync(nameof(ExAboutWu));
#else
        LinuxViewRenderer.PushPage(new ExAboutWu());
#endif
    }

    public async void OnExOfficesClicked(object sender, EventArgs e) {
#if WINDOW
        await Shell.Current.GoToAsync(nameof(ExOffices));
#else
        LinuxViewRenderer.PushPage(new ExOffices());
#endif
    }

    public async void OnExFacultyClicked(object sender, EventArgs e) {
#if WINDOW
        await Shell.Current.GoToAsync(nameof(ExFaculty));
#else
        LinuxViewRenderer.PushPage(new ExFaculty());
#endif
    }

}


