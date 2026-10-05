using QuestPDF.Fluent;
using QuestPDF.Helpers;
using student_report_app_v1.Models;

namespace student_report_app_v1.Services;

public class PDFReportService
{
    public string GenerateStudentReport(List<Student> students) {
        string filename = $"StudentReport_{DateTime.Now:yyyyMMddHHmmss}.pdf";
        string basepath = FileSystem.Current.AppDataDirectory;
        string filepath = Path.Combine(basepath, filename);

        Document.Create(document => {
            document.Page(page => {
                page.Size(PageSizes.A4); 
                page.Margin(30);
                page.DefaultTextStyle(x => x.FontSize(10));

                page.Header()
                    .Column(column => {
                        column.Item()
                              .AlignCenter()
                              .Text("WESTERN UNIVERSITY")
                              .Bold()
                              .FontSize(20);
                        column.Item()
                              .AlignCenter()
                              .Text("STUDENT REPORT")
                              .Bold()
                              .FontSize(16);
                        column.Item()
                              .AlignCenter()
                              .Text($"Generated: {DateTime.Now:dd/MM/yyyy HH:mm}")
                              .FontSize(9);
                        column.Item()
                              .PaddingBottom(10);
                });
                page.Content()
                    .Column(column => {
                        column.Item()
                              .PaddingBottom(10)
                              .Text($"Total Student {students.Count}")
                              .Bold()
                              .FontSize(12);
                        column.Item()
                              .Table(table => {
                                  table.ColumnsDefinition(column => {
                                      column.ConstantColumn(40);
                                      column.RelativeColumn(2);
                                      column.ConstantColumn(40);
                                      column.RelativeColumn(2);
                                      column.RelativeColumn(2);
                                  }); 
                                  table.Header(header => {
                                      header.Cell()
                                            .Background(QuestPDF.Helpers.Colors.Grey.Lighten2)
                                            .Border(1)
                                            .BorderColor(QuestPDF.Helpers.Colors.Grey.Medium)
                                            .Padding(5)
                                            .Text("Id")
                                            .Bold();
                                      header.Cell()
                                            .Background(QuestPDF.Helpers.Colors.Grey.Lighten2)
                                            .Border(1)
                                            .BorderColor(QuestPDF.Helpers.Colors.Grey.Medium)
                                            .Padding(5)
                                            .Text("Name")
                                            .Bold();
                                      header.Cell()
                                            .Background(QuestPDF.Helpers.Colors.Grey.Lighten2)
                                            .Border(1)
                                            .BorderColor(QuestPDF.Helpers.Colors.Grey.Medium)
                                            .Padding(5)
                                            .Text("Gender")
                                            .Bold();
                                      header.Cell()
                                            .Background(QuestPDF.Helpers.Colors.Grey.Lighten2)
                                            .Border(1)
                                            .BorderColor(QuestPDF.Helpers.Colors.Grey.Medium)
                                            .Padding(5)
                                            .Text("Email")
                                            .Bold();
                                      header.Cell()
                                            .Background(QuestPDF.Helpers.Colors.Grey.Lighten2)
                                            .Border(1)
                                            .BorderColor(QuestPDF.Helpers.Colors.Grey.Medium)
                                            .Padding(5)
                                            .Text("Major")
                                            .Bold();
                                  });
                                  foreach (var student in students) {
                                      table.Cell()
                                           .Border(1)
                                           .BorderColor(QuestPDF.Helpers.Colors.Grey.Lighten2)
                                           .Padding(5)
                                           .Text(student.Id.ToString());
                                      table.Cell()
                                           .Border(1)
                                           .BorderColor(QuestPDF.Helpers.Colors.Grey.Lighten2)
                                           .Padding(5)
                                           .Text(student.Name ?? "");
                                      table.Cell()
                                           .Border(1)
                                           .BorderColor(QuestPDF.Helpers.Colors.Grey.Lighten2)
                                           .Padding(5)
                                           .Text(student.Gender ?? "");
                                      table.Cell()
                                           .Border(1)
                                           .BorderColor(QuestPDF.Helpers.Colors.Grey.Lighten2)
                                           .Padding(5)
                                           .Text(student.Email ?? "");
                                      table.Cell()
                                           .Border(1)
                                           .BorderColor(QuestPDF.Helpers.Colors.Grey.Lighten2)
                                           .Padding(5)
                                           .Text(student.Major ?? "");
                                  }
                              });
                });
                page.Footer()
                    .AlignCenter()
                    .Text(text => {
                        text.Span("Page ");
                        text.CurrentPageNumber();
                        text.Span(" of ");
                        text.TotalPages();
                    });
            });
        }).GeneratePdf(filepath);

        return filepath;
    }
}


