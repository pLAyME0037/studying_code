using user_info.Services;

namespace user_info.Views;

public partial class SimpleDialogPage : ContentPage
{
    public SimpleDialogPage(string title, string message) {
        InitializeComponent();
        TitleLabel.Text = title;
        MessageLabel.Text = message;
    }

    private async void OnOkClicked(object sender, EventArgs e) {
        await Nav.PopAsync();
    }
}

