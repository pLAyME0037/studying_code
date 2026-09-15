using SQLite;
using user_info.Models.StudentDB;

namespace user_info.Services.StudentDB;

public class DBService
{
    private SQLiteAsyncConnection? _database;

    public async Task InitializeDatabaseAsync() {
        if (_database != null) { return; }

        string baseDir;
        try {
            baseDir = FileSystem.Current.AppDataDirectory;
        } catch (Exception) {
            baseDir = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        }
        string dbPath = Path.Combine(baseDir, "WU_Student.db3");

        _database = new SQLiteAsyncConnection(dbPath);
        await _database.CreateTableAsync<Student>();
    }

    public async Task AddStudentAsync(Student student) {
        await InitializeDatabaseAsync();
        await _database!.InsertAsync(student);
    }

    public async Task UpdateStudentAsync(Student student) {
        await InitializeDatabaseAsync();
        await _database!.UpdateAsync(student);
    }

    public async Task DeleteStudentAsync(Student student) {
        await InitializeDatabaseAsync();
        await _database!.DeleteAsync(student);
    }

    public async Task<List<Student>> GetStudentAsync() {
        await InitializeDatabaseAsync();
        return await _database!.Table<Student>().ToListAsync();
    }

    public async Task<List<Student>> SearchStudentsByNameAsync(string name) {
        await InitializeDatabaseAsync();
        return await _database!.Table<Student>()
                               .Where(f => f.Name.Contains(name))
                               .ToListAsync();
    }

    public async Task<List<string>> GetDistinctMajorAsync() {
        string sql = @"SELECT DISTINCT Major
                       FROM Student
                       WHERE Major IS NOT NULL AND Major<>''
                       ORDER BY Major";
        return await _database!.QueryScalarsAsync<string>(sql);
    }

    public async Task<List<Student>> SearchStudentsByMajorAsync(string major) {
        await InitializeDatabaseAsync();
        return await _database!.Table<Student>()
                               .Where(f => f.Major.Contains(major))
                               .ToListAsync();
    }
}


