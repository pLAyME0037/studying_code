using Microsoft.Data.SqlClient;
using student_report_app_v1.Models;

namespace student_report_app_v1.Services;

public class DBService
{
    private readonly string connectionString = "Server=DESKTOP-GODAJOS\\SQLExpress;"
                                             + "Database=WesternUniversity;"
                                             + "User Id=sa;"
                                             + "Password=123456;"
                                             + "TrustServerCertificate=True;";

    public async Task<List<Student>> GetStudentsAsync() {
        var student = new List<Student>();
        using SqlConnection connection = new SqlConnection(connectionString);
        await connection.OpenAsync();

        string sql = @"SELECT Id, Name, Gender, DateOfBirth, Major, Email"
                   + "FROM Student"
                   + "ORDER BY Id";
        using SqlCommand command = new SqlCommand(sql, connection);
        using SqlDataReader reader = await command.ExecuteReaderAsync();

        while (await reader.ReadAsync()) {
            student.Add(new Student {
                Id          = reader.GetInt32(0),
                Name        = reader.GetString(1),
                Gender      = reader.IsDBNull(2) ? "" : reader.GetString(2),
                DateOfBirth = reader.IsDBNull(3) ? DateTime.MinValue
                                                 : reader.GetDateTime(3),
                Major       = reader.IsDBNull(4) ? "" : reader.GetString(4),
                Email       = reader.IsDBNull(5) ? "" : reader.GetString(5),
            });
        }
        return student;
    }
}


