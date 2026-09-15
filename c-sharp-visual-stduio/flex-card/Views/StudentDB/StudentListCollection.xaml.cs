using user_info.Services.StudentDB;
using user_info.ViewModels.StudentDB;

namespace user_info.Views.StudentDB;

public partial class StudentListCollection : ContentPage
{
    private readonly StudentViewModel stu_vm;

    public StudentListCollection() {
        InitializeComponent();
        var db_service = new DBService();
        stu_vm = new StudentViewModel(db_service);
        BindingContext = stu_vm;
    }

    // public StudentListCollection(StudentViewModel stu_vm) {
    //     InitializeComponent();
    //     BindingContext = stu_vm;
    // }

    protected override async void OnAppearing() {
        base.OnAppearing();
        if (stu_vm != null) {
            await stu_vm.LoadStudentsAsync();
        }
    }
}


