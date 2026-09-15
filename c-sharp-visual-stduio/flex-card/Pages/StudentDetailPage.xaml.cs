using user_info.Services;

namespace user_info.Pages;

public partial class StudentDetailPage : ContentPage
{
	public StudentDetailPage() {
		InitializeComponent();
	}

    private async void EditStudent(object sender, EventArgs e) {
        await Nav.PushAsync(new EditStudentPage());
    }
}
