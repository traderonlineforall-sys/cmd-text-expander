using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace CmdTextExpander;

public sealed class SnippetStore
{
    private readonly string _filePath;
    private readonly JsonSerializerOptions _jsonOptions = new() { WriteIndented = true };

    public event EventHandler? Changed;
    public List<Snippet> Snippets { get; private set; } = new();

    public SnippetStore(string filePath)
    {
        _filePath = filePath;
        Load();
    }

    public void Load()
    {
        try
        {
            if (File.Exists(_filePath))
            {
                var json = File.ReadAllText(_filePath);
                Snippets = JsonSerializer.Deserialize<List<Snippet>>(json, _jsonOptions) ?? new List<Snippet>();
            }
        }
        catch
        {
            Snippets = new List<Snippet>();
        }

        if (Snippets.Count == 0)
        {
            Snippets.Add(new Snippet
            {
                Keyword = ";hi",
                Text = "أهلاً بحضرتك، معاك إسلام من خدمة عملاء WE. ازاي أقدر أساعد حضرتك؟",
                Group = "Default"
            });
            Snippets.Add(new Snippet
            {
                Keyword = ";thanks",
                Text = "تحت أمرك يا فندم، سعدت بمساعدة حضرتك ونتمنالك يوم سعيد.",
                Group = "Default"
            });
            Save();
        }
    }

    public void Save()
    {
        var dir = Path.GetDirectoryName(_filePath);
        if (!string.IsNullOrWhiteSpace(dir)) Directory.CreateDirectory(dir);
        File.WriteAllText(_filePath, JsonSerializer.Serialize(Snippets, _jsonOptions));
        Changed?.Invoke(this, EventArgs.Empty);
    }

    public void Upsert(Snippet item)
    {
        item.Keyword = item.Keyword.Trim();
        item.UpdatedAt = DateTime.Now;
        var old = Snippets.FirstOrDefault(x => x.Id == item.Id);
        if (old is null)
        {
            if (string.IsNullOrWhiteSpace(item.Id)) item.Id = Guid.NewGuid().ToString("N");
            Snippets.Add(item);
        }
        else
        {
            old.Keyword = item.Keyword;
            old.Text = item.Text;
            old.Group = item.Group;
            old.Enabled = item.Enabled;
            old.UpdatedAt = item.UpdatedAt;
        }
        Save();
    }

    public void Delete(string id)
    {
        Snippets.RemoveAll(x => x.Id == id);
        Save();
    }

    public Snippet? MatchKeyword(string keyword)
    {
        return Snippets.FirstOrDefault(x => x.Enabled && string.Equals(x.Keyword, keyword, StringComparison.Ordinal));
    }
}
