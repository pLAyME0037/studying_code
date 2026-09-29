using System.Net.Http.Json;
using remote_product_app.Models;

namespace remote_product_app.Services;

public class ProductService
{
    private readonly HttpClient httpClient;

    public ProductService() {
        httpClient = new HttpClient();
    }

    public async Task<List<Product>> ReadProductAsync() {
        string url = "https://dummyjson.com/products";
        ProductResponse? response = await httpClient.GetFromJsonAsync<ProductResponse>(url);
        return response?.Products ?? new List<Product>();
    }
}


