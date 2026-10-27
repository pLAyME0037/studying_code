namespace Products.Models;

public class IncomeReportItem
{
    public int IncomeId        { get; set; }
    public string CustomerName { get; set; } = string.Empty;
    public string ProductName  { get; set; } = string.Empty;
    public int Quantity        { get; set; }
    public decimal UnitPrice   { get; set; }
    public decimal TotalAmount { get; set; }
    public DateTime SoldAt     { get; set; }
}