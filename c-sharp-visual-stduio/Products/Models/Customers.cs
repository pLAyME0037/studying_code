using System;
using System.Collections.Generic;
using System.Text;
using SQLite;

namespace Products.Models;

[Table("customers")]
public class Customer
{
    [PrimaryKey, AutoIncrement]
    public int Id { get; set; }

    [NotNull]
    public string Name { get; set; } = string.Empty;

    public string Phone { get; set; } = string.Empty;
}

