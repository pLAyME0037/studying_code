namespace remote_product_app.Models;

public class Product
{
    public int    Id           { get; set; } = 0;
    public string Title        { get; set; } = string.Empty;
    public double Price        { get; set; } = 0;
    public string Category     { get; set; } = string.Empty;  
    public string Description  { get; set; } = string.Empty;   
    public string Thumbnail    { get; set; } = string.Empty;  
}


