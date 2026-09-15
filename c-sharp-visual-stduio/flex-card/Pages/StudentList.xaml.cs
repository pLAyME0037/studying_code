using user_info.Services;
using System.ComponentModel;

namespace user_info.Pages;

public partial class StudentList : ContentPage
{
    public StudentList() {
        InitializeComponent();
    }

    private async void StudentDetail1(object sender, EventArgs e) {
        await Nav.PushAsync(new StudentDetailPage());
    }

    private async void StudentDetail2(object sender, EventArgs e) {
        await Nav.PushAsync(new StudentDetailPage());
    }
}
