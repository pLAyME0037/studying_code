using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using user_info.Models;
using user_info.Services;

namespace user_info.ViewModels;

public partial class ExProductViewModel : ObservableObject
{
    private readonly ExProductService? _service;
    private ObservableCollection<ExProduct> products { get; }

    [ObservableProperty] private int    id          = 0;
    [ObservableProperty] private string productName = string.Empty;
    [ObservableProperty] private int    unit        = 0;
    [ObservableProperty] private int    unitPrice   = 0;

    [ObservableProperty] private ExProduct? selectProduct;
 
    public ExProductViewModel(ExProductService service) {
        _service = service;
        products = new ObservableCollection<ExProduct>();
    }

    public async Task LoadProduct() {
        var read_products = await _service!.ReadProductList();
        foreach (var product in read_products) {
            products.Add(product);
        }
    }

    [RelayCommand]
    public async Task ReadProduct() {
        await LoadProduct();
    }

    [RelayCommand]
    public async Task AddProduct() {
        ExProduct product = new() {
            ProductName = ProductName,
            Unit        = Unit,
            UnitPrice   = UnitPrice,
        };
        await _service!.CreateProduct(product);
        products.Add(product);
    }

    [RelayCommand]
    public async Task GetUpdateProduct(ExProduct product) {
        selectProduct = product;
        ProductName = product.ProductName;
        Unit        = product.Unit;
        UnitPrice   = product.UnitPrice;
    }

    [RelayCommand]
    public async Task UpdateProduct() {
        if (selectProduct is not {} product) return;
        product.ProductName = ProductName;
        product.Unit        = Unit;
        product.UnitPrice   = UnitPrice;
        await _service!.UpdateProduct(product);
        selectProduct = null;
        await _service.ReadProductList();
    }

    [RelayCommand]
    public async Task DeleteProduct(ExProduct product) {
        await _service!.DeleteProduct(product);
        products.Remove(product);
    }
}


