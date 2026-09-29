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

    [ObservableProperty] private string   name  = string.Empty;
    [ObservableProperty] private string   sex   = string.Empty;
    [ObservableProperty] private string   email = string.Empty;
    [ObservableProperty] private string   major = string.Empty;
    [ObservableProperty] private int      age   = 0;

    [ObservableProperty] private Student? selectedStudent;
    [ObservableProperty] private bool     isEditing;

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

    // Load student into form for editing
    [RelayCommand]
    public void EditStudent(Student student) {
        SelectedStudent = student;
        IsEditing = true;
        Name  = student.Name;
        Sex   = student.Sex;
        Email = student.Email;
        Major = student.Major;
        Age   = student.Age;
    }

    [RelayCommand]
    public async Task UpdateStudent() {
        if (SelectedStudent is not { } student) { return; }

        student.Name  = Name;
        student.Sex   = Sex;
        student.Email = Email;
        student.Major = Major;
        student.Age   = Age;

        await _db_service.UpdateStudentAsync(student);
        await LoadStudentsAsync();   // Student = plain POCO, reload refreshes list labels

        SelectedStudent = null;
        IsEditing = false;
        ClearFields();
    }

    [RelayCommand]
    public async Task DeleteStudent(Student student) {
        await _db_service.DeleteStudentAsync(student);
        Students.Remove(student);

        if (SelectedStudent == student) {
            SelectedStudent = null;
            IsEditing = false;
            ClearFields();
        }
    }

    private void ClearFields() {
        Name  = string.Empty;
        Sex   = string.Empty;
        Email = string.Empty;
        Major = string.Empty;
        Age   = 0;
    }
}


