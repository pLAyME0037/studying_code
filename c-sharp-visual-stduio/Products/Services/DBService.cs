using SQLite;
using Products.Models;

namespace Products.Services;

public class DatabaseService
{
    private SQLiteAsyncConnection _db = null!;

    public async Task InitializeAsync()
    {
        if (_db != null) return;

        var dbPath = Path.Combine(FileSystem.AppDataDirectory, "products.db3");
        _db = new SQLiteAsyncConnection(dbPath);

        await _db.CreateTableAsync<Product>();
        await _db.CreateTableAsync<Customer>();
        await _db.CreateTableAsync<Income>();
    }

    public Task<List<Product>> GetProductsAsync() =>
        _db.Table<Product>().OrderBy(p => p.Name).ToListAsync();

    public Task<int> SaveProductAsync(Product item) =>
        item.Id == 0 ? _db.InsertAsync(item) : _db.UpdateAsync(item);

    public Task<List<Customer>> GetCustomersAsync() =>
        _db.Table<Customer>().OrderBy(c => c.Name).ToListAsync();

    public Task<int> SaveCustomerAsync(Customer item) =>
        item.Id == 0 ? _db.InsertAsync(item) : _db.UpdateAsync(item);

    public async Task CheckoutAsync(int customerId, IEnumerable<CartItem> cart)
    {
        var records = cart.Select(c => new Income
        {
            CustomerId = customerId,
            ProductId = c.ProductId,
            Quantity = c.Quantity,
            UnitPrice = c.UnitPrice,
            TotalAmount = c.LineTotal,
            SoldAt = DateTime.UtcNow
        }).ToList();

        await _db.InsertAllAsync(records);
    }

    public Task<List<IncomeReportItem>> GetIncomeReportAsync()
    {
        const string sql = @"
            SELECT 
                i.Id AS IncomeId,
                c.Name AS CustomerName,
                p.Name AS ProductName,
                i.Quantity,
                i.UnitPrice,
                i.TotalAmount,
                i.SoldAt
            FROM income i
            INNER JOIN customers c ON i.CustomerId = c.Id
            INNER JOIN products p ON i.ProductId = p.Id
            ORDER BY i.SoldAt DESC";

        return _db.QueryAsync<IncomeReportItem>(sql);
    }

    public Task<decimal> GetTotalIncomeAsync() =>
        _db.ExecuteScalarAsync<decimal>("SELECT COALESCE(SUM(TotalAmount), 0) FROM income");

    public Task<int> GetTotalSalesCountAsync() =>
        _db.Table<Income>().CountAsync();
}