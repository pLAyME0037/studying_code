using user_info.Services.WUCompany;
using user_info.Models.WUCompany;
using user_info.Services;
using System.Diagnostics;

namespace user_info.Views;

public partial class WUCompanyRegister : ContentPage
{
    private readonly DBService _dBService;

    public WUCompanyRegister(DBService dBService) {
        InitializeComponent();
        this._dBService = dBService;
    }

    private async void RegisterButtonClick(object sender, EventArgs e) {
        try {
            string username        = UsernameEntry.Text;
            string email           = EmailEntry.Text;
            string password        = PasswordEntry.Text;
            string confirmPassword = ConfirmPasswordEntry.Text;

            if (string.IsNullOrWhiteSpace(username)
                || string.IsNullOrWhiteSpace(email)
                || string.IsNullOrWhiteSpace(password)) {
                await DisplayAlertAsync("Error",
                                        "Please enter all require information",
                                        "Ok");
                return;
            }

            if (password != confirmPassword) {
                await Dialog.ShowAlertAsync("Error",
                                            "Please recomfirm you password");
                return;
            }

            var existUser = await _dBService.GetUserByUserName(username);
            if (existUser != null) {
                await Dialog.ShowAlertAsync("Error",
                                            "User already exist");
                return;
            }

            User user = new() {
                Username = username,
                Email    = email,
                Password = password,
            };

            await _dBService.RegisterUserAsync(user);
            await Dialog.ShowAlertAsync("Success",
                                        "User create successfully");
            await Nav.GoToPageAsync("//WUCompanyLoginPage");
        } catch (Exception ex) {
            Debug.WriteLine($"RegisterButtonClick error: {ex}");
            await DisplayAlertAsync("Error", ex.Message, "OK");
        }
    }

    private async void GoToLoginButtonClick(object sender, EventArgs e) {
        try {
            await Nav.GoToPageAsync("//WUCompanyLoginPage");
        } catch (Exception ex) {
            Debug.WriteLine($"GoToLoginButtonClick error: {ex}");
            await DisplayAlertAsync("Error", ex.Message, "OK");
        }
    }
}


