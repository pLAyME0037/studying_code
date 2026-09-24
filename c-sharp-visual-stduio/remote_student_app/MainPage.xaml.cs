using remote_student_app.Services;

namespace remote_student_app;

public partial class MainPage : ContentPage
{
    private readonly StudentService studentService;
	public MainPage() {
		InitializeComponent();

        studentService = new StudentService();
	}

	private async void OnLoadStudentsClicked(object? sender, EventArgs e) {
        try {
            var student = await studentService.GetStudentsAsync();
            StudentCollectionView.ItemsSource = student;
        } catch (HttpRequestException) {
            await DisplayAlertAsync("Error", "Unable to connect to API", "OK");
        } catch (Exception ex) {
            await DisplayAlertAsync("Error", ex.Message, "OK");
        }
	}
}
