using remote_product_app.Models;
using remote_product_app.Services;

namespace remote_product_app;

public partial class MainPage : ContentPage
{
    private readonly ProductService productService;

	public MainPage() {
		InitializeComponent();
        productService = new ProductService();
	}

    public async void OnLoadProductClicked(object sender, EventArgs e) {
        try {
            var products = await productService.ReadProductAsync();
            ProductCollectionView.ItemsSource = products;
        } catch (HttpRequestException) {
            await DisplayAlertAsync("Connection Error",
                                    "Lost Connection",
                                    "Ok");
        } catch (Exception ex) {
            await DisplayAlertAsync("Error", ex.Message, "Ok");
        }
    }
}
