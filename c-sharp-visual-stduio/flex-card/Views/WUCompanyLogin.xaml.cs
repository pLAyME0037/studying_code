using Microsoft.Maui.Platform.Linux.Hosting;
using user_info.Services;
using user_info.Services.WUCompany;
using System.Diagnostics;

namespace user_info.Views;

public partial class WUCompanyLogin : ContentPage
{
    private readonly DBService _dBService;

    public WUCompanyLogin(DBService service) {
        InitializeComponent();
        this._dBService = service;
    }

    protected async void LoginButtonClick(object sender, EventArgs e) {
        try {
            string username = UsernameEntry.Text;
            string password = PasswordEntry.Text;

            if (string.IsNullOrWhiteSpace(username)
                || string.IsNullOrWhiteSpace(password)) {
                await Dialog.ShowAlertAsync("Error",
                                            "Please enter username and password");
                return;
            }

            var user = await _dBService.LoginAsync(username, password);

            if (user != null) {
                await Dialog.ShowAlertAsync("Success",
                                            $"Welcome, {user.Username}");
            } else {
                await Dialog.ShowAlertAsync("Login Failed",
                                            $"Invalid username or password");
            }
        } catch (Exception ex) {
            Debug.WriteLine($"LoginButtonClick error: {ex}");
            await DisplayAlertAsync("Error", ex.Message, "OK");
        }
    }

    protected async void RegisterButtonClick(object sender, EventArgs e) {
        try {
            await Nav.GoToPageAsync("//WUCompanyRegisterPage");
        } catch (Exception ex) {
            Debug.WriteLine($"RegisterButtonClick error: {ex}");
            await DisplayAlertAsync("Error", ex.Message, "OK");
        }
    }
}


