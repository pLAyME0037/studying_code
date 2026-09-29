using System.ComponentModel;
using Microsoft.Maui.Platform.Linux.Hosting;

namespace user_info.Views;

public partial class ExAboutWu : ContentPage
{
    public ExAboutWu() {
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

    public async void OnExOfficesClicked(object sender, EventArgs e) {
#if WINDOW
        await Shell.Current.GoToAsync(nameof(ExOffices));
#else
        LinuxViewRenderer.PushPage(new ExOffices());
#endif
    }

}
