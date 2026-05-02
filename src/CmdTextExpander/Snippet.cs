using System;

namespace CmdTextExpander;

public sealed class Snippet
{
    public string Id { get; set; } = Guid.NewGuid().ToString("N");
    public string Keyword { get; set; } = string.Empty;
    public string Text { get; set; } = string.Empty;
    public string Group { get; set; } = string.Empty;
    public bool Enabled { get; set; } = true;
    public DateTime UpdatedAt { get; set; } = DateTime.Now;
}
