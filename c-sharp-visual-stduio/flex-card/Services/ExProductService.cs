using SQLite;
using user_info.Models;

namespace user_info.Services;

public class ExProductService
{
    private SQLiteAsyncConnection? sqlConn;

    public async void InitDatabaseConnection() {
        if (sqlConn == null) return;
        string baseDir;
        try {
            baseDir = FileSystem.Current.AppDataDirectory;
        } catch (Exception) {
            baseDir = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        }
        string dbDir = Path.Combine(baseDir, "ex_product.db3");
        sqlConn = new SQLiteAsyncConnection(dbDir);
        await sqlConn.CreateTableAsync<ExProduct>();
    }

    public async Task<List<ExProduct>> SearchProductByName(string name) {
        InitDatabaseConnection();
        return await sqlConn!.Table<ExProduct>()
                            .Where(n => n.ProductName.Contains(name))
                            .ToListAsync();
    }

    public async Task<List<ExProduct>> ReadProductList() {
        InitDatabaseConnection();
        return await sqlConn!.Table<ExProduct>().ToListAsync();
    }

    public async Task CreateProduct(ExProduct product) {
        InitDatabaseConnection();
        sqlConn?.InsertAsync(product);
    }

    public async Task UpdateProduct(ExProduct product) {
        InitDatabaseConnection();
        sqlConn?.UpdateAsync(product);
    }

    public async Task DeleteProduct(ExProduct product) {
        InitDatabaseConnection();
        sqlConn?.DeleteAsync(product);
    }
}


