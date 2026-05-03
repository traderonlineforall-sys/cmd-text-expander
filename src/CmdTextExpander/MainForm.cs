using System;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Windows.Forms;

namespace CmdTextExpander;

public sealed class MainForm : Form
{
    private readonly SnippetStore _store;
    private readonly NativeTextEngine _engine;
    private readonly BindingList<Snippet> _view = new();

    private readonly DataGridView _grid = new();
    private readonly TextBox _searchBox = new();
    private readonly TextBox _keywordBox = new();
    private readonly TextBox _groupBox = new();
    private readonly TextBox _snippetBox = new();
    private readonly Label _status = new();
    private readonly Label _countLabel = new();

    private string? _editingId;

    private static readonly Color Bg = Color.FromArgb(11, 18, 32);
    private static readonly Color PanelBg = Color.FromArgb(15, 23, 42);
    private static readonly Color FieldBg = Color.White;
    private static readonly Color TextDark = Color.Black;
    private static readonly Color TextLight = Color.FromArgb(226, 232, 240);
    private static readonly Color Muted = Color.FromArgb(148, 163, 184);
    private static readonly Color Blue = Color.FromArgb(37, 99, 235);
    private static readonly Color Green = Color.FromArgb(34, 197, 94);
    private static readonly Color Red = Color.FromArgb(239, 68, 68);

    public MainForm()
    {
        Text = "cmd";
        Width = 1120;
        Height = 760;
        MinimumSize = new Size(900, 620);
        StartPosition = FormStartPosition.CenterScreen;
        BackColor = Bg;
        ForeColor = TextLight;
        Font = new Font("Segoe UI", 10);

        var dataPath = Path.Combine(AppContext.BaseDirectory, "snippets.json");
        _store = new SnippetStore(dataPath);
        _engine = new NativeTextEngine(this, _store, SetStatus);

        BuildLayout();
        RefreshGrid();
        SafeStartEngine();
    }

    private void BuildLayout()
    {
        Controls.Clear();

        var root = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            BackColor = Bg,
            ColumnCount = 1,
            RowCount = 5,
            Padding = new Padding(12)
        };
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 78));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 246));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 54));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 28));
        Controls.Add(root);

        root.Controls.Add(BuildHeader(), 0, 0);
        root.Controls.Add(BuildEditor(), 0, 1);
        root.Controls.Add(BuildLibraryToolbar(), 0, 2);
        root.Controls.Add(BuildGrid(), 0, 3);
        root.Controls.Add(BuildStatusBar(), 0, 4);
    }

    private Control BuildHeader()
    {
        var header = new Panel
        {
            Dock = DockStyle.Fill,
            BackColor = PanelBg,
            Padding = new Padding(12)
        };

        var title = new Label
        {
            Text = "cmd — Portable Text Expander",
            Left = 12,
            Top = 8,
            Width = 360,
            Height = 30,
            Font = new Font("Segoe UI", 18, FontStyle.Bold),
            ForeColor = Color.White
        };
        header.Controls.Add(title);

        var subtitle = new Label
        {
            Text = "Beeftext-style replacement: save a keyword, type it anywhere, and the canned response expands.",
            Left = 14,
            Top = 42,
            Width = 720,
            Height = 22,
            ForeColor = Muted
        };
        header.Controls.Add(subtitle);

        var enable = MakeButton("Enable", Green, Color.White);
        enable.SetBounds(760, 18, 96, 38);
        enable.Click += (_, _) => { SafeStartEngine(); RefreshGrid(); };
        header.Controls.Add(enable);

        var disable = MakeButton("Disable", Red, Color.White);
        disable.SetBounds(866, 18, 96, 38);
        disable.Click += (_, _) => { _engine.Stop(); RefreshGrid(); };
        header.Controls.Add(disable);

        var openData = MakeButton("Open Data Folder", Blue, Color.White);
        openData.SetBounds(972, 18, 132, 38);
        openData.Click += (_, _) => Process.Start("explorer.exe", AppContext.BaseDirectory);
        header.Controls.Add(openData);

        header.Resize += (_, _) =>
        {
            openData.Left = Math.Max(760, header.ClientSize.Width - openData.Width - 12);
            disable.Left = openData.Left - disable.Width - 10;
            enable.Left = disable.Left - enable.Width - 10;
            subtitle.Width = Math.Max(300, enable.Left - 30);
        };

        return header;
    }

    private Control BuildEditor()
    {
        var box = new GroupBox
        {
            Dock = DockStyle.Fill,
            Text = "Add / Edit Canned Response",
            ForeColor = Color.White,
            BackColor = PanelBg,
            Padding = new Padding(14)
        };

        var editor = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 4,
            RowCount = 5,
            BackColor = PanelBg,
            Padding = new Padding(10)
        };
        editor.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 125));
        editor.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
        editor.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 110));
        editor.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50));
        editor.RowStyles.Add(new RowStyle(SizeType.Absolute, 36));
        editor.RowStyles.Add(new RowStyle(SizeType.Absolute, 36));
        editor.RowStyles.Add(new RowStyle(SizeType.Absolute, 92));
        editor.RowStyles.Add(new RowStyle(SizeType.Absolute, 44));
        editor.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        box.Controls.Add(editor);

        editor.Controls.Add(MakeEditorLabel("Keyword / الاختصار"), 0, 0);
        _keywordBox.Dock = DockStyle.Fill;
        _keywordBox.BackColor = FieldBg;
        _keywordBox.ForeColor = TextDark;
        _keywordBox.PlaceholderText = "Example: ;hi or ;35";
        editor.Controls.Add(_keywordBox, 1, 0);

        editor.Controls.Add(MakeEditorLabel("Group / التصنيف"), 2, 0);
        _groupBox.Dock = DockStyle.Fill;
        _groupBox.BackColor = FieldBg;
        _groupBox.ForeColor = TextDark;
        _groupBox.PlaceholderText = "Optional";
        editor.Controls.Add(_groupBox, 3, 0);

        editor.Controls.Add(MakeEditorLabel("Snippet Text / النص الكامل"), 0, 1);
        _snippetBox.Dock = DockStyle.Fill;
        _snippetBox.Multiline = true;
        _snippetBox.ScrollBars = ScrollBars.Vertical;
        _snippetBox.BackColor = FieldBg;
        _snippetBox.ForeColor = TextDark;
        editor.SetColumnSpan(_snippetBox, 3);
        editor.SetRowSpan(_snippetBox, 2);
        editor.Controls.Add(_snippetBox, 1, 1);

        var buttons = new FlowLayoutPanel
        {
            Dock = DockStyle.Fill,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false,
            BackColor = PanelBg,
            Padding = new Padding(0, 5, 0, 0)
        };
        editor.SetColumnSpan(buttons, 4);
        editor.Controls.Add(buttons, 0, 3);

        var save = MakeButton("Save", Blue, Color.White);
        save.Click += (_, _) => SaveSnippet();
        buttons.Controls.Add(save);

        var clear = MakeButton("New", Color.FromArgb(229, 231, 235), Color.Black);
        clear.Click += (_, _) => ClearEditor();
        buttons.Controls.Add(clear);

        var delete = MakeButton("Delete", Red, Color.White);
        delete.Click += (_, _) => DeleteSelected();
        buttons.Controls.Add(delete);

        var copy = MakeButton("Copy Text", Color.FromArgb(229, 231, 235), Color.Black);
        copy.Width = 110;
        copy.Click += (_, _) => Clipboard.SetText(_snippetBox.Text ?? string.Empty);
        buttons.Controls.Add(copy);

        var note = new Label
        {
            Text = "Tip: use keywords like ;5, ;net, ;thanks. Avoid one-letter keywords because they expand during normal typing.",
            Dock = DockStyle.Fill,
            ForeColor = Muted,
            TextAlign = ContentAlignment.MiddleLeft
        };
        editor.SetColumnSpan(note, 4);
        editor.Controls.Add(note, 0, 4);

        return box;
    }

    private Control BuildLibraryToolbar()
    {
        var toolbar = new Panel
        {
            Dock = DockStyle.Fill,
            BackColor = Bg,
            Padding = new Padding(0, 8, 0, 8)
        };

        _searchBox.SetBounds(0, 10, 430, 30);
        _searchBox.PlaceholderText = "Search keyword, group, or response text...";
        _searchBox.TextChanged += (_, _) => RefreshGrid();
        toolbar.Controls.Add(_searchBox);

        var exportButton = MakeButton("Export Backup", Color.FromArgb(229, 231, 235), Color.Black);
        exportButton.SetBounds(445, 8, 130, 34);
        exportButton.Click += (_, _) => ExportBackup();
        toolbar.Controls.Add(exportButton);

        var importButton = MakeButton("Import Backup", Color.FromArgb(229, 231, 235), Color.Black);
        importButton.SetBounds(585, 8, 130, 34);
        importButton.Click += (_, _) => ImportBackup();
        toolbar.Controls.Add(importButton);

        _countLabel.SetBounds(730, 13, 320, 24);
        _countLabel.ForeColor = Muted;
        toolbar.Controls.Add(_countLabel);

        toolbar.Resize += (_, _) =>
        {
            _searchBox.Width = Math.Max(260, Math.Min(520, toolbar.ClientSize.Width - 640));
            exportButton.Left = _searchBox.Right + 15;
            importButton.Left = exportButton.Right + 10;
            _countLabel.Left = importButton.Right + 15;
            _countLabel.Width = Math.Max(120, toolbar.ClientSize.Width - _countLabel.Left - 10);
        };

        return toolbar;
    }

    private Control BuildGrid()
    {
        _grid.Dock = DockStyle.Fill;
        _grid.AutoGenerateColumns = false;
        _grid.AllowUserToAddRows = false;
        _grid.ReadOnly = true;
        _grid.SelectionMode = DataGridViewSelectionMode.FullRowSelect;
        _grid.MultiSelect = false;
        _grid.RowHeadersVisible = false;
        _grid.BackgroundColor = PanelBg;
        _grid.GridColor = Color.FromArgb(51, 65, 85);
        _grid.ForeColor = Color.White;
        _grid.DefaultCellStyle.BackColor = Color.FromArgb(17, 24, 39);
        _grid.DefaultCellStyle.ForeColor = Color.White;
        _grid.DefaultCellStyle.SelectionBackColor = Blue;
        _grid.DefaultCellStyle.SelectionForeColor = Color.White;
        _grid.ColumnHeadersDefaultCellStyle.BackColor = Color.FromArgb(30, 41, 59);
        _grid.ColumnHeadersDefaultCellStyle.ForeColor = Color.White;
        _grid.EnableHeadersVisualStyles = false;
        _grid.Columns.Clear();
        _grid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "Keyword", DataPropertyName = nameof(Snippet.Keyword), Width = 150 });
        _grid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "Group", DataPropertyName = nameof(Snippet.Group), Width = 140 });
        _grid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "Response Text", DataPropertyName = nameof(Snippet.Text), AutoSizeMode = DataGridViewAutoSizeColumnMode.Fill });
        _grid.CellDoubleClick += (_, _) => LoadSelectedForEdit();
        _grid.KeyDown += (_, e) =>
        {
            if (e.KeyCode == Keys.Enter)
            {
                LoadSelectedForEdit();
                e.SuppressKeyPress = true;
            }
        };
        return _grid;
    }

    private Control BuildStatusBar()
    {
        _status.Dock = DockStyle.Fill;
        _status.ForeColor = Muted;
        _status.TextAlign = ContentAlignment.MiddleLeft;
        return _status;
    }

    private static Label MakeEditorLabel(string text)
    {
        return new Label
        {
            Text = text,
            Dock = DockStyle.Fill,
            ForeColor = Muted,
            TextAlign = ContentAlignment.MiddleLeft
        };
    }

    private static Button MakeButton(string text, Color back, Color fore)
    {
        return new Button
        {
            Text = text,
            Width = 100,
            Height = 32,
            BackColor = back,
            ForeColor = fore,
            FlatStyle = FlatStyle.Flat,
            Font = new Font("Segoe UI", 9, FontStyle.Bold),
            Cursor = Cursors.Hand,
            Margin = new Padding(0, 0, 10, 0)
        };
    }

    private void RefreshGrid()
    {
        var q = _searchBox.Text.Trim();
        var items = _store.Snippets
            .Where(x => string.IsNullOrWhiteSpace(q) || x.Keyword.Contains(q, StringComparison.OrdinalIgnoreCase) || x.Text.Contains(q, StringComparison.OrdinalIgnoreCase) || x.Group.Contains(q, StringComparison.OrdinalIgnoreCase))
            .OrderBy(x => x.Group)
            .ThenBy(x => x.Keyword)
            .ToList();

        _view.Clear();
        foreach (var item in items) _view.Add(item);
        _grid.DataSource = null;
        _grid.DataSource = _view;
        _countLabel.Text = $"Showing {items.Count} of {_store.Snippets.Count} snippets";
        SetStatus($"{(_engine.Enabled ? "Enabled" : "Disabled")} — double-click any row to edit.");
    }

    private void SaveSnippet()
    {
        if (string.IsNullOrWhiteSpace(_keywordBox.Text))
        {
            MessageBox.Show("Keyword is required. اكتب الاختصار في خانة Keyword.", "cmd", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            _keywordBox.Focus();
            return;
        }
        if (string.IsNullOrEmpty(_snippetBox.Text))
        {
            MessageBox.Show("Snippet text is required. اكتب النص الكامل.", "cmd", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            _snippetBox.Focus();
            return;
        }

        _store.Upsert(new Snippet
        {
            Id = _editingId ?? Guid.NewGuid().ToString("N"),
            Keyword = _keywordBox.Text.Trim(),
            Group = _groupBox.Text.Trim(),
            Text = _snippetBox.Text,
            Enabled = true
        });
        ClearEditor();
        RefreshGrid();
        SetStatus("Saved.");
    }

    private void LoadSelectedForEdit()
    {
        if (_grid.CurrentRow?.DataBoundItem is not Snippet item) return;
        _editingId = item.Id;
        _keywordBox.Text = item.Keyword;
        _groupBox.Text = item.Group;
        _snippetBox.Text = item.Text;
        SetStatus("Editing " + item.Keyword);
    }

    private void DeleteSelected()
    {
        if (!string.IsNullOrWhiteSpace(_editingId))
        {
            _store.Delete(_editingId);
            ClearEditor();
            RefreshGrid();
            return;
        }
        if (_grid.CurrentRow?.DataBoundItem is Snippet item && MessageBox.Show("Delete selected snippet?", "cmd", MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes)
        {
            _store.Delete(item.Id);
            RefreshGrid();
        }
    }

    private void ClearEditor()
    {
        _editingId = null;
        _keywordBox.Clear();
        _groupBox.Clear();
        _snippetBox.Clear();
        _keywordBox.Focus();
    }

    private void ExportBackup()
    {
        using var dialog = new SaveFileDialog
        {
            Filter = "JSON backup (*.json)|*.json|Beeftext backup (*.btbackup)|*.btbackup",
            FileName = "cmd-snippets.json"
        };
        if (dialog.ShowDialog(this) == DialogResult.OK)
        {
            File.Copy(Path.Combine(AppContext.BaseDirectory, "snippets.json"), dialog.FileName, true);
            SetStatus("Backup exported.");
        }
    }

    private void ImportBackup()
    {
        using var dialog = new OpenFileDialog
        {
            Filter = "Backup files (*.json;*.btbackup)|*.json;*.btbackup|JSON backup (*.json)|*.json|Beeftext backup (*.btbackup)|*.btbackup|All files (*.*)|*.*",
            Title = "Import snippets backup"
        };
        if (dialog.ShowDialog(this) == DialogResult.OK)
        {
            try
            {
                _store.ImportFromFile(dialog.FileName);
                ClearEditor();
                RefreshGrid();
                SetStatus("Backup imported.");
            }
            catch (Exception ex)
            {
                MessageBox.Show("Could not import backup: " + ex.Message, "cmd", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }
    }

    private void SetStatus(string message)
    {
        if (InvokeRequired)
        {
            BeginInvoke(new Action<string>(SetStatus), message);
            return;
        }
        _status.Text = message;
    }

    private void SafeStartEngine()
    {
        try
        {
            _engine.Start();
        }
        catch (Exception ex)
        {
            SetStatus("Enable failed: " + ex.Message);
        }
    }

    protected override void OnResize(EventArgs e)
    {
        base.OnResize(e);
        // No auto-hide. The program stays visible in the taskbar.
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        _engine.Dispose();
        base.OnFormClosing(e);
    }
}
