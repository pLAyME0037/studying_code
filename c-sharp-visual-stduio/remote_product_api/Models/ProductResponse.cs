namespace remote_product_app.Models;

public class ProductResponse
{
    public List<Product> Products { get; set; } = new();
    public int           Total    { get; set; } = 0;  
    public int           Skip     { get; set; } = 0;
    public int           Limit    { get; set; } = 0;
}


