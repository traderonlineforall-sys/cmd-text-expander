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
                Snippets = ParseSnippets(json);
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

    public void ImportFromFile(string sourcePath)
    {
        var json = File.ReadAllText(sourcePath);
        var imported = ParseSnippets(json);
        if (imported.Count == 0)
            throw new InvalidOperationException("No snippets were found in this backup file.");

        Snippets = imported;
        Save();
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

    private static List<Snippet> ParseSnippets(string json)
    {
        var result = new List<Snippet>();
        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;

        if (root.ValueKind == JsonValueKind.Array)
        {
            foreach (var item in root.EnumerateArray())
                AddSnippetFromElement(result, item, null);
        }
        else if (root.ValueKind == JsonValueKind.Object)
        {
            var groupNames = ReadGroupNames(root);
            if (root.TryGetProperty("combos", out var combos) && combos.ValueKind == JsonValueKind.Array)
            {
                foreach (var combo in combos.EnumerateArray())
                    AddSnippetFromElement(result, combo, groupNames);
            }
            else
            {
                AddSnippetFromElement(result, root, groupNames);
            }
        }

        return result
            .Where(x => !string.IsNullOrWhiteSpace(x.Keyword) && !string.IsNullOrEmpty(x.Text))
            .GroupBy(x => x.Keyword, StringComparer.Ordinal)
            .Select(g => g.Last())
            .ToList();
    }

    private static Dictionary<string, string> ReadGroupNames(JsonElement root)
    {
        var groups = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        if (!root.TryGetProperty("groups", out var arr) || arr.ValueKind != JsonValueKind.Array)
            return groups;

        foreach (var group in arr.EnumerateArray())
        {
            var id = GetString(group, "uuid") ?? GetString(group, "id") ?? string.Empty;
            var name = GetString(group, "name") ?? string.Empty;
            if (!string.IsNullOrWhiteSpace(id) && !string.IsNullOrWhiteSpace(name))
                groups[id] = name;
        }

        return groups;
    }

    private static void AddSnippetFromElement(List<Snippet> result, JsonElement item, Dictionary<string, string>? groupNames)
    {
        if (item.ValueKind != JsonValueKind.Object) return;

        var keyword = GetString(item, "keyword") ?? GetString(item, "key") ?? string.Empty;
        var text = GetString(item, "text") ?? GetString(item, "snippet") ?? GetString(item, "value") ?? string.Empty;
        var groupRaw = GetString(item, "group") ?? GetString(item, "groupName") ?? GetString(item, "sheetName") ?? string.Empty;
        var group = groupRaw;
        if (groupNames is not null && !string.IsNullOrWhiteSpace(groupRaw) && groupNames.TryGetValue(groupRaw, out var mapped))
            group = mapped;

        var enabled = true;
        if (item.TryGetProperty("enabled", out var en) && en.ValueKind == JsonValueKind.False)
            enabled = false;

        result.Add(new Snippet
        {
            Id = GetString(item, "id") ?? GetString(item, "uuid") ?? Guid.NewGuid().ToString("N"),
            Keyword = keyword.Trim(),
            Text = text,
            Group = group,
            Enabled = enabled,
            UpdatedAt = DateTime.Now
        });
    }

    private static string? GetString(JsonElement element, string name)
    {
        if (!element.TryGetProperty(name, out var value)) return null;
        return value.ValueKind switch
        {
            JsonValueKind.String => value.GetString(),
            JsonValueKind.Number => value.ToString(),
            JsonValueKind.True => "true",
            JsonValueKind.False => "false",
            _ => null
        };
    }
}
