using SQLite;

namespace user_info.Models.WUCompany;

public class User
{
    [PrimaryKey, AutoIncrement]
    public int Id          { get; set; } = 0;
    [MaxLength(50), Unique]
    public string Username { get; set; } = string.Empty;
    [MaxLength(50), Unique]
    public string Email    { get; set; } = string.Empty;
    [MaxLength(50)]
    public string Password { get; set; } = string.Empty;
}


