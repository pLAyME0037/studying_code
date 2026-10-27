using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Products.Models;
using Products.Services;

namespace Products.ViewModels;

public partial class MainViewModel : ObservableObject
{
    private readonly DatabaseService _db;
    private readonly PdfExportService _pdf;

    public ObservableCollection<Product> ProductsList { get; } = new();
    public ObservableCollection<Customer> CustomersList { get; } = new();
    public ObservableCollection<CartItem> CartItems { get; } = new();
    public ObservableCollection<IncomeReportItem> ReportItems { get; } = new();

    [ObservableProperty] private Customer? _selectedCustomer;
    [ObservableProperty] private Product? _selectedProduct;
    [ObservableProperty] private string _itemQuantityText = "1";
    [ObservableProperty] private decimal _cartTotal;

    // KPI Metrics
    [ObservableProperty] private decimal _totalIncome;
    [ObservableProperty] private int _totalTransactions;
    [ObservableProperty] private int _totalProducts;
    [ObservableProperty] private int _totalCustomers;

    // Master Data Inputs
    [ObservableProperty] private string _newProductName = string.Empty;
    [ObservableProperty] private string _newProductPriceText = string.Empty;
    [ObservableProperty] private string _newCustomerName = string.Empty;
    [ObservableProperty] private string _newCustomerPhone = string.Empty;

    public MainViewModel(DatabaseService db, PdfExportService pdf)
    {
        _db = db;
        _pdf = pdf;
    }

    [RelayCommand]
    public async Task LoadDataAsync()
    {
        await _db.InitializeAsync();
        await RefreshMasterDataAsync();
        await RefreshReportAsync();
    }

    private async Task RefreshMasterDataAsync()
    {
        var products = await _db.GetProductsAsync();
        ProductsList.Clear();
        foreach (var p in products) ProductsList.Add(p);
        TotalProducts = products.Count;

        var customers = await _db.GetCustomersAsync();
        CustomersList.Clear();
        foreach (var c in customers) CustomersList.Add(c);
        TotalCustomers = customers.Count;
    }

    [RelayCommand]
    public async Task AddProductAsync()
    {
        if (string.IsNullOrWhiteSpace(NewProductName) || !decimal.TryParse(NewProductPriceText, out var price) || price <= 0)
            return;

        await _db.SaveProductAsync(new Product { Name = NewProductName.Trim(), Price = price });
        NewProductName = string.Empty;
        NewProductPriceText = string.Empty;
        await RefreshMasterDataAsync();
    }

    [RelayCommand]
    public async Task AddCustomerAsync()
    {
        if (string.IsNullOrWhiteSpace(NewCustomerName)) return;

        await _db.SaveCustomerAsync(new Customer { Name = NewCustomerName.Trim(), Phone = NewCustomerPhone.Trim() });
        NewCustomerName = string.Empty;
        NewCustomerPhone = string.Empty;
        await RefreshMasterDataAsync();
    }

    [RelayCommand]
    public void AddToCart()
    {
        if (SelectedProduct == null || !int.TryParse(ItemQuantityText, out var qty) || qty <= 0)
            return;

        var existing = CartItems.FirstOrDefault(x => x.ProductId == SelectedProduct.Id);
        if (existing != null)
        {
            var newQty = existing.Quantity + qty;
            CartItems.Remove(existing);
            CartItems.Add(new CartItem
            {
                ProductId = SelectedProduct.Id,
                ProductName = SelectedProduct.Name,
                UnitPrice = SelectedProduct.Price,
                Quantity = newQty
            });
        }
        else
        {
            CartItems.Add(new CartItem
            {
                ProductId = SelectedProduct.Id,
                ProductName = SelectedProduct.Name,
                UnitPrice = SelectedProduct.Price,
                Quantity = qty
            });
        }

        CartTotal = CartItems.Sum(x => x.LineTotal);
        ItemQuantityText = "1";
    }

    [RelayCommand]
    public void RemoveFromCart(CartItem item)
    {
        CartItems.Remove(item);
        CartTotal = CartItems.Sum(x => x.LineTotal);
    }

    [RelayCommand]
    public async Task CheckoutOrderAsync()
    {
        if (SelectedCustomer == null || !CartItems.Any()) return;

        await _db.CheckoutAsync(SelectedCustomer.Id, CartItems);
        CartItems.Clear();
        CartTotal = 0;
        await RefreshReportAsync();
    }

    [RelayCommand]
    public async Task PrintPdfAsync()
    {
        if (!ReportItems.Any()) return;
        await _pdf.GenerateIncomeReportPdfAsync(ReportItems.ToList());
    }

    private async Task RefreshReportAsync()
    {
        var items = await _db.GetIncomeReportAsync();
        ReportItems.Clear();
        foreach (var item in items) ReportItems.Add(item);

        TotalIncome = await _db.GetTotalIncomeAsync();
        TotalTransactions = await _db.GetTotalSalesCountAsync();
    }
}