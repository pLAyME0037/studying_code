using System.ComponentModel;
using Microsoft.Maui.Platform.Linux.Hosting;

namespace user_info.Views;

public partial class ExFaculty : ContentPage
{
    public ExFaculty() {
        InitializeComponent();
    }

    public async void OnExAboutWuClicked(object sender, EventArgs e) {
#if WINDOW
        await Shell.Current.GoToAsync(nameof(ExAboutWu));
#else
        LinuxViewRenderer.PushPage(new ExAboutWu());
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

    public async void OnExFacultyOfEconomicClicked(object sender, EventArgs e) {
#if WINDOW
        await Shell.Current.GoToAsync(nameof(ExFacultyOfEconomic));
#else
        LinuxViewRenderer.PushPage(new ExFacultyOfEconomic());
#endif
    }

    public async void OnExFacultyOfScienceClicked(object sender, EventArgs e) {
#if WINDOW
        await Shell.Current.GoToAsync(nameof(ExFacultyOfScience));
#else
        LinuxViewRenderer.PushPage(new ExFacultyOfScience());
#endif
    }
}
