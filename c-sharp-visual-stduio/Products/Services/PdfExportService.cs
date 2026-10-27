using QuestPDF.Fluent;
using QuestPDF.Helpers;
using QuestPDF.Infrastructure;
using Products.Models;

namespace Products.Services;

public class PdfExportService
{
    public PdfExportService()
    {
        QuestPDF.Settings.License = LicenseType.Community;
    }

    public async Task<string> GenerateIncomeReportPdfAsync(List<IncomeReportItem> items)
    {
        var totalIncome = items.Sum(x => x.TotalAmount);
        var filePath = Path.Combine(FileSystem.CacheDirectory,
                                    $"IncomeReport_{DateTime.Now:yyyyMMdd_HHmmss}.pdf");

        Document.Create(container =>
        {
            container.Page(page =>
            {
                page.Size(PageSizes.A4);
                page.Margin(1, Unit.Centimetre);
                page.DefaultTextStyle(x => x.FontSize(10));

                page.Header().Text("Income & Sales Report").SemiBold().FontSize(18);

                page.Content().PaddingVertical(10).Table(table =>
                {
                    table.ColumnsDefinition(cols =>
                    {
                        cols.RelativeColumn(3); // Customer
                        cols.RelativeColumn(3); // Product
                        cols.RelativeColumn(1); // Qty
                        cols.RelativeColumn(2); // Unit Price
                        cols.RelativeColumn(2); // Total
                        cols.RelativeColumn(3); // Date
                    });

                    table.Header(header =>
                    {
                        header.Cell().Text("Customer").Bold();
                        header.Cell().Text("Product").Bold();
                        header.Cell().Text("Qty").Bold();
                        header.Cell().Text("Unit").Bold();
                        header.Cell().Text("Total").Bold();
                        header.Cell().Text("Date").Bold();
                    });

                    foreach (var item in items)
                    {
                        table.Cell().Text(item.CustomerName);
                        table.Cell().Text(item.ProductName);
                        table.Cell().Text(item.Quantity.ToString());
                        table.Cell().Text($"{item.UnitPrice:C}");
                        table.Cell().Text($"{item.TotalAmount:C}");
                        table.Cell().Text(item.SoldAt.ToLocalTime().ToString("g"));
                    }
                });

                page.Footer().AlignRight().Text($"Total Income: {totalIncome:C}").Bold().FontSize(14);
            });
        }).GeneratePdf(filePath);

        await Launcher.Default.OpenAsync(new OpenFileRequest
        {
            Title = "Income Report",
            File = new ReadOnlyFile(filePath)
        });

        return filePath;
    }
}