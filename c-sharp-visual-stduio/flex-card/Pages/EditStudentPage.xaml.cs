using user_info.Services;

namespace user_info.Pages;

public partial class EditStudentPage : ContentPage
{
	public EditStudentPage() {
		InitializeComponent();
	}

    private async void GoBack(object sender, EventArgs e) {
        await Nav.PopAsync();
    }

    private async void GoHome(object sender, EventArgs e) {
        await Nav.GoHomeAsync();
    }
}
