using user_info.Services;

namespace user_info.Pages;

public partial class HomePage : ContentPage
{
	public HomePage() {
		InitializeComponent();
	}

    private async void StudentListClicked(object sender, EventArgs e) {
        await Nav.PushAsync(new StudentList());
    }
}
