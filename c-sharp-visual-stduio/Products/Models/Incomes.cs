using SQLite;

namespace Products.Models;

[Table("income")]
public class Income
{
    [PrimaryKey, AutoIncrement]
    public int Id              { get; set; }
    [Indexed]
    public int CustomerId      { get; set; }
    [Indexed]
    public int ProductId       { get; set; }
    public int Quantity        { get; set; }
    public decimal UnitPrice   { get; set; }
    public decimal TotalAmount { get; set; }
    public DateTime SoldAt     { get; set; }
}