using SQLite;
namespace user_info.Models;

public class ExProduct
{
    [PrimaryKey, AutoIncrement] public int ProductId { get; set; }
    [MaxLength(60)] public string ProductName { get; set; } = string.Empty;
    [MaxLength(60)] public int Unit           { get; set; }
    [MaxLength(60)] public int UnitPrice      { get; set; }
}


