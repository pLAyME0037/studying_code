using user_info.Models.StudentDB;
using user_info.Services.StudentDB;
using user_info.ViewModels.StudentDB;

namespace user_info.Views.StudentDB;

public partial class StudentListAdd : ContentPage
{
    private readonly StudentViewModel? stu_vm;

    public StudentListAdd() {
        InitializeComponent();
        var db_service = new DBService();
        stu_vm = new StudentViewModel(db_service);
        BindingContext = stu_vm;
    }

    public StudentListAdd(StudentViewModel stu_vm) {
        InitializeComponent();
        BindingContext = stu_vm;
    }

    protected override async void OnAppearing() {
        base.OnAppearing();
        if (stu_vm != null) {
            await stu_vm.LoadStudentsAsync();
        }
    }

    // TEMP DIAGNOSTIC: log every click to stdout
    private void OnEditClicked(object? sender, EventArgs e) {
        var ctx = (sender as Button)?.BindingContext?.GetType().Name ?? "null";
        Console.WriteLine($"[DIAG] Edit clicked, BindingContext={ctx}");
        if (sender is Button { BindingContext: Student stu }) {
            stu_vm?.EditStudent(stu);
        }
    }

    private async void OnDeleteClicked(object? sender, EventArgs e) {
        var ctx = (sender as Button)?.BindingContext?.GetType().Name ?? "null";
        Console.WriteLine($"[DIAG] Delete clicked, BindingContext={ctx}");
        if (sender is Button { BindingContext: Student stu }) {
            if (stu_vm != null) {
                await stu_vm.DeleteStudent(stu);
            }
        }
    }

    // Add/Update buttons = page-level, no Command machinery (Linux IsEnabled bug)
    private async void OnAddClicked(object? sender, EventArgs e) {
        Console.WriteLine("[DIAG] Add clicked");
        if (stu_vm != null) {
            await stu_vm.AddStudent();
        }
    }

    private async void OnUpdateClicked(object? sender, EventArgs e) {
        Console.WriteLine("[DIAG] Update clicked");
        if (stu_vm != null) {
            await stu_vm.UpdateStudent();
        }
    }
}


