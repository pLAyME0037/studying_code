using System.Collections.ObjectModel;
using System.ComponentModel.DataAnnotations;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using user_info.Models.StudentDB;
using user_info.Services.StudentDB;

namespace user_info.ViewModels.StudentDB;

public partial class StudentViewModel : ObservableObject
{
    private readonly DBService _db_service;

    public ObservableCollection<Student> Students { get; }

    [ObservableProperty] private string name  = string.Empty;
    [ObservableProperty] private string sex   = string.Empty;
    [ObservableProperty] private string email = string.Empty;
    [ObservableProperty] private string major = string.Empty;
    [ObservableProperty] private int    age   = 0;

    public StudentViewModel(DBService db_service) {
        _db_service = db_service;
        Students = new ObservableCollection<Student>();
        _ = LoadStudentsAsync();
    }

    public async Task LoadStudentsAsync() {
        try {
            var students = await _db_service.GetStudentAsync();
            Students.Clear();
            foreach (var student in students) {
                Students.Add(student);
            }
        } catch (Exception ex) {
            System.Diagnostics.Debug.WriteLine($"LoadStudents failed: {ex}");
        }
    }

    [RelayCommand]
    public async Task LoadStudent() {
        await LoadStudentsAsync();
    }

    [RelayCommand]
    public async Task AddStudent() {
        Student student = new() {
            Name  = Name,
            Sex   = Sex,
            Email = Email,
            Major = Major,
            Age   = Age,
        };

        await _db_service.AddStudentAsync(student);
        Students.Add(student);
        ClearFields();
    }

    // [RelayCommand]
    // public async Task UpdateStudent() {
    //     Student student = new() {
    //         Name  = Name,
    //         Email = Email,
    //         Major = Major,
    //         Age   = Age,
    //     };
    //
    //     await _db_service.UpdateStudentAsync(student);
    //     Students.UpdateStudent(student);
    //     ClearFields();
    // }

    private void ClearFields() {
        Name  = string.Empty;
        Sex   = string.Empty;
        Email = string.Empty;
        Major = string.Empty;
        Age   = 0;
    }
}


