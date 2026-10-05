namespace student_report_app_v1.Models;

public class Student
{
    public int    Id            { get; set; } = 0;
    public string Name          { get; set; } = string.Empty;
    public string Gender        { get; set; } = string.Empty;
    public DateTime DateOfBirth { get; set; }
    public string Email         { get; set; } = string.Empty;
    public string Major         { get; set; } = string.Empty;
}


