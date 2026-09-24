using System.Net.Http.Json;
using remote_student_app.Models;

namespace remote_student_app.Services;

public class StudentService
{
    private readonly HttpClient httpClient;
    public StudentService() {
        httpClient = new HttpClient();
    }

    public async Task<List<Student>> GetStudentsAsync() {
        string url = "http://127.0.0.1:38273/api/student";
        var students = await httpClient.GetFromJsonAsync<List<Student>>(url);
        return students ?? new List<Student>();
    }
}


