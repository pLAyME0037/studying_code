using SQLite;
using user_info.Models.WUCompany;

namespace user_info.Services.WUCompany;

public class DBService
{
    private SQLiteAsyncConnection? _db_conn;

    private async Task Init() {
        if (_db_conn != null) return;

        string baseDir;
        try {
            baseDir = FileSystem.Current.AppDataDirectory;
        } catch (Exception) {
            baseDir = Environment.GetFolderPath(Environment.SpecialFolder
                                                           .LocalApplicationData);
        }
        string dbPath = Path.Combine(baseDir, "WUCompany.db3");

        _db_conn = new SQLiteAsyncConnection(dbPath);
        await _db_conn.CreateTableAsync<User>();
    }

    public async Task<int> RegisterUserAsync(User user) {
        await Init();
        return await _db_conn!.InsertAsync(user);
    }

    public async Task<int> UpdateUserAsync(User user) {
        await Init();
        return await _db_conn!.UpdateAsync(user);
    }

    public async Task<User?> LoginAsync(string username, string password) {
        await Init();
        return await _db_conn!.Table<User>()
                              .Where(u => u.Username == username
                                       && u.Password == password)
                              .FirstOrDefaultAsync();
    }

    public async Task<User?> GetUserByUserName(string username) {
        await Init();
        return await _db_conn!.Table<User>()
                              .Where(u => u.Username == username)
                              .FirstOrDefaultAsync();
    }
}


