using Microsoft.AspNetCore.Mvc;
using remote_student_api.Models;

namespace remote_student_api.Controllers;
[ApiController]
[Route("api/[controller]")]

public class StudentController : ControllerBase
{
    private static readonly List<Student> students = new() {
        new Student {
            Id    = 1,
            Name  = "Dara",
            Email = "Dara@example.com",
            Major = "Computer Science"
        },
        new Student {
            Id    = 2,
            Name  = "John",
            Email = "John@example.com",
            Major = "Information Technology"
        },
        new Student {
            Id    = 3,
            Name  = "Rithy",
            Email = "Rithy@example.com",
            Major = "Information Technology"
        },
        new Student {
            Id    = 4,
            Name  = "Bora",
            Email = "Bora@example.com",
            Major = "Software Enginering"
        },
        new Student {
            Id    = 5,
            Name  = "Soka",
            Email = "Soka@example.com",
            Major = "Software Enginering"
        },
    };

    [HttpGet]
    public IActionResult GetStudent() {
        return Ok(students);
    }
}


