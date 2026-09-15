using Microsoft.Maui.Platform.Linux.Hosting;
using user_info.Views;

namespace user_info;

public partial class AppShell : Shell
{
	public AppShell() {
		InitializeComponent();
        Routing.RegisterRoute("WUCompanyLoginPage", typeof(WUCompanyLogin));
        Routing.RegisterRoute("WUCompanyRegisterPage", typeof(WUCompanyRegister));
	}
}
