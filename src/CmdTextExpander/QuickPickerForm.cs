using System;
using System.Collections.Generic;
using System.Drawing;
using System.Linq;
using System.Windows.Forms;

namespace CmdTextExpander;

public sealed class QuickPickerForm : Form
{
    private readonly List<Snippet> _all;
    private readonly ListBox _list = new();
    private readonly TextBox _search = new();
    private readonly Label _hint = new();

    public Snippet? SelectedSnippet { get; private set; }

    public QuickPickerForm(IEnumerable<Snippet> snippets)
    {
        _all = snippets.Where(s => s.Enabled).OrderBy(s => s.Group).ThenBy(s => s.Keyword).ToList();

        Text = "cmd quick canned picker";
        Width = 720;
        Height = 460;
        StartPosition = FormStartPosition.CenterScreen;
        FormBorderStyle = FormBorderStyle.FixedSingle;
        MaximizeBox = false;
        MinimizeBox = false;
        TopMost = true;
        BackColor = Color.FromArgb(11, 18, 32);
        ForeColor = Color.FromArgb(226, 232, 240);
        Font = new Font("Segoe UI", 10);
        KeyPreview = true;

        var title = new Label
        {
            Text = "Quick Canned Responses — Ctrl + Space",
            Dock = DockStyle.Top,
            Height = 42,
            Font = new Font("Segoe UI", 14, FontStyle.Bold),
            ForeColor = Color.White,
            Padding = new Padding(14, 8, 14, 0)
        };
        Controls.Add(title);

        _search.Dock = DockStyle.Top;
        _search.Height = 34;
        _search.Margin = new Padding(12);
        _search.PlaceholderText = "Search keyword, group, or response text...";
        _search.TextChanged += (_, _) => RefreshList();
        _search.KeyDown += SearchKeyDown;
        Controls.Add(_search);

        _hint.Dock = DockStyle.Bottom;
        _hint.Height = 34;
        _hint.ForeColor = Color.FromArgb(148, 163, 184);
        _hint.Text = "Enter = paste selected canned | Esc = close | Double click = paste";
        _hint.Padding = new Padding(12, 6, 12, 0);
        Controls.Add(_hint);

        _list.Dock = DockStyle.Fill;
        _list.BackColor = Color.FromArgb(15, 23, 42);
        _list.ForeColor = Color.White;
        _list.BorderStyle = BorderStyle.None;
        _list.Font = new Font("Segoe UI", 11);
        _list.DoubleClick += (_, _) => AcceptCurrent();
        _list.KeyDown += ListKeyDown;
        Controls.Add(_list);

        Shown += (_, _) => _search.Focus();
        KeyDown += (_, e) => { if (e.KeyCode == Keys.Escape) Close(); };
        RefreshList();
    }

    private void RefreshList()
    {
        var q = (_search.Text ?? string.Empty).Trim();
        var filtered = _all.Where(s =>
            string.IsNullOrWhiteSpace(q) ||
            s.Keyword.Contains(q, StringComparison.OrdinalIgnoreCase) ||
            s.Group.Contains(q, StringComparison.OrdinalIgnoreCase) ||
            s.Text.Contains(q, StringComparison.OrdinalIgnoreCase))
            .Take(200)
            .ToList();

        _list.BeginUpdate();
        _list.Items.Clear();
        foreach (var item in filtered)
        {
            _list.Items.Add(new SnippetListItem(item));
        }
        _list.EndUpdate();

        if (_list.Items.Count > 0) _list.SelectedIndex = 0;
    }

    private void SearchKeyDown(object? sender, KeyEventArgs e)
    {
        if (e.KeyCode == Keys.Enter)
        {
            AcceptCurrent();
            e.SuppressKeyPress = true;
        }
        else if (e.KeyCode == Keys.Down && _list.Items.Count > 0)
        {
            _list.Focus();
            _list.SelectedIndex = Math.Min(_list.Items.Count - 1, Math.Max(0, _list.SelectedIndex + 1));
            e.SuppressKeyPress = true;
        }
        else if (e.KeyCode == Keys.Escape)
        {
            Close();
        }
    }

    private void ListKeyDown(object? sender, KeyEventArgs e)
    {
        if (e.KeyCode == Keys.Enter)
        {
            AcceptCurrent();
            e.SuppressKeyPress = true;
        }
        else if (e.KeyCode == Keys.Escape)
        {
            Close();
        }
    }

    private void AcceptCurrent()
    {
        if (_list.SelectedItem is SnippetListItem item)
        {
            SelectedSnippet = item.Snippet;
            DialogResult = DialogResult.OK;
            Close();
        }
    }

    private sealed class SnippetListItem
    {
        public Snippet Snippet { get; }

        public SnippetListItem(Snippet snippet)
        {
            Snippet = snippet;
        }

        public override string ToString()
        {
            var group = string.IsNullOrWhiteSpace(Snippet.Group) ? "General" : Snippet.Group;
            var preview = (Snippet.Text ?? string.Empty).Replace("\r", " ").Replace("\n", " ").Trim();
            if (preview.Length > 85) preview = preview[..85] + "...";
            return $"{Snippet.Keyword}   [{group}]   {preview}";
        }
    }
}
