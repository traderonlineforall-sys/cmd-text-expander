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
    private readonly Button _enableButton = new();
    private readonly Button _disableButton = new();
    private readonly NotifyIcon _tray;
    private string? _editingId;

    public MainForm()
    {
        Text = "cmd";
        Width = 1220;
        Height = 780;
        MinimumSize = new Size(1050, 650);
        StartPosition = FormStartPosition.CenterScreen;
        BackColor = Color.FromArgb(11, 18, 32);
        ForeColor = Color.FromArgb(226, 232, 240);
        Font = new Font("Segoe UI", 10);

        var dataPath = Path.Combine(AppContext.BaseDirectory, "snippets.json");
        _store = new SnippetStore(dataPath);
        _engine = new NativeTextEngine(this, _store, SetStatus);
        _tray = BuildTrayIcon();

        BuildLayout();
        RefreshGrid();
        _engine.Start();
    }

    private NotifyIcon BuildTrayIcon()
    {
        var menu = new ContextMenuStrip();
        menu.Items.Add("Show", null, (_, _) => ShowFromTray());
        menu.Items.Add("Enable Expander", null, (_, _) => _engine.Start());
        menu.Items.Add("Disable Expander", null, (_, _) => _engine.Stop());
        menu.Items.Add("Exit", null, (_, _) => Close());

        var icon = new NotifyIcon
        {
            Icon = SystemIcons.Application,
            Text = "cmd Text Expander",
            Visible = true,
            ContextMenuStrip = menu
        };
        icon.DoubleClick += (_, _) => ShowFromTray();
        return icon;
    }

    private void BuildLayout()
    {
        var header = new Panel
        {
            Dock = DockStyle.Top,
            Height = 86,
            BackColor = Color.FromArgb(15, 23, 42),
            Padding = new Padding(14)
        };
        Controls.Add(header);

        var title = new Label
        {
            Text = "cmd — Portable Text Expander",
            Dock = DockStyle.Left,
            Width = 360,
            Font = new Font("Segoe UI", 18, FontStyle.Bold),
            ForeColor = Color.White,
            TextAlign = ContentAlignment.MiddleLeft
        };
        header.Controls.Add(title);

        _enableButton.Text = "Enable";
        _enableButton.Width = 110;
        _enableButton.Height = 38;
        _enableButton.Left = 385;
        _enableButton.Top = 22;
        _enableButton.BackColor = Color.FromArgb(34, 197, 94);
        _enableButton.ForeColor = Color.White;
        _enableButton.FlatStyle = FlatStyle.Flat;
        _enableButton.Click += (_, _) => _engine.Start();
        header.Controls.Add(_enableButton);

        _disableButton.Text = "Disable";
        _disableButton.Width = 110;
        _disableButton.Height = 38;
        _disableButton.Left = 505;
        _disableButton.Top = 22;
        _disableButton.BackColor = Color.FromArgb(239, 68, 68);
        _disableButton.ForeColor = Color.White;
        _disableButton.FlatStyle = FlatStyle.Flat;
        _disableButton.Click += (_, _) => _engine.Stop();
        header.Controls.Add(_disableButton);

        var openData = new Button
        {
            Text = "Open Data Folder",
            Width = 150,
            Height = 38,
            Left = 625,
            Top = 22,
            BackColor = Color.FromArgb(37, 99, 235),
            ForeColor = Color.White,
            FlatStyle = FlatStyle.Flat
        };
        openData.Click += (_, _) => Process.Start("explorer.exe", AppContext.BaseDirectory);
        header.Controls.Add(openData);

        _status.Text = "Ready";
        _status.Dock = DockStyle.Bottom;
        _status.Height = 24;
        _status.ForeColor = Color.FromArgb(148, 163, 184);
        header.Controls.Add(_status);

        var split = new SplitContainer
        {
            Dock = DockStyle.Fill,
            SplitterDistance = 700,
            BackColor = Color.FromArgb(11, 18, 32),
            Panel1MinSize = 520,
            Panel2MinSize = 430,
            FixedPanel = FixedPanel.Panel2
        };
        Controls.Add(split);

        BuildLibraryPanel(split.Panel1);
        BuildEditorPanel(split.Panel2);
    }

    private void BuildLibraryPanel(Control parent)
    {
        parent.Padding = new Padding(12);
        parent.BackColor = Color.FromArgb(11, 18, 32);

        var searchRow = new Panel { Dock = DockStyle.Top, Height = 52, BackColor = Color.FromArgb(11, 18, 32) };
        parent.Controls.Add(searchRow);
        _searchBox.PlaceholderText = "Search keyword or text...";
        _searchBox.Left = 0;
        _searchBox.Top = 8;
        _searchBox.Width = 420;
        _searchBox.Height = 32;
        _searchBox.TextChanged += (_, _) => RefreshGrid();
        searchRow.Controls.Add(_searchBox);

        var exportButton = new Button { Text = "Export Backup", Left = 435, Top = 6, Width = 130, Height = 34 };
        exportButton.Click += (_, _) => ExportBackup();
        searchRow.Controls.Add(exportButton);

        var importButton = new Button { Text = "Import Backup", Left = 575, Top = 6, Width = 130, Height = 34 };
        importButton.Click += (_, _) => ImportBackup();
        searchRow.Controls.Add(importButton);

        _grid.Dock = DockStyle.Fill;
        _grid.AutoGenerateColumns = false;
        _grid.AllowUserToAddRows = false;
        _grid.ReadOnly = true;
        _grid.SelectionMode = DataGridViewSelectionMode.FullRowSelect;
        _grid.MultiSelect = false;
        _grid.BackgroundColor = Color.FromArgb(15, 23, 42);
        _grid.GridColor = Color.FromArgb(51, 65, 85);
        _grid.ForeColor = Color.White;
        _grid.ColumnHeadersDefaultCellStyle.BackColor = Color.FromArgb(30, 41, 59);
        _grid.ColumnHeadersDefaultCellStyle.ForeColor = Color.White;
        _grid.EnableHeadersVisualStyles = false;
        _grid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "Keyword", DataPropertyName = "Keyword", Width = 140 });
        _grid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "Group", DataPropertyName = "Group", Width = 110 });
        _grid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "Text", DataPropertyName = "Text", AutoSizeMode = DataGridViewAutoSizeColumnMode.Fill });
        _grid.CellDoubleClick += (_, _) => LoadSelectedForEdit();
        parent.Controls.Add(_grid);
    }

    private void BuildEditorPanel(Control parent)
    {
        parent.Padding = new Padding(18);
        parent.BackColor = Color.FromArgb(15, 23, 42);

        var heading = new Label
        {
            Text = "Add / Edit Canned Response",
            Dock = DockStyle.Top,
            Height = 44,
            Font = new Font("Segoe UI", 14, FontStyle.Bold),
            ForeColor = Color.White
        };
        parent.Controls.Add(heading);

        var panel = new Panel
        {
            Dock = DockStyle.Top,
            Height = 440,
            Padding = new Padding(0, 8, 0, 0),
            BackColor = Color.FromArgb(15, 23, 42)
        };
        parent.Controls.Add(panel);

        AddLabel(panel, "Keyword / الاختصار", 0);
        _keywordBox.SetBounds(0, 26, 380, 34);
        _keywordBox.PlaceholderText = "Example: ;hi or ;35";
        _keywordBox.BackColor = Color.White;
        _keywordBox.ForeColor = Color.Black;
        panel.Controls.Add(_keywordBox);

        AddLabel(panel, "Group / التصنيف", 72);
        _groupBox.SetBounds(0, 98, 380, 34);
        _groupBox.PlaceholderText = "Optional category";
        _groupBox.BackColor = Color.White;
        _groupBox.ForeColor = Color.Black;
        panel.Controls.Add(_groupBox);

        AddLabel(panel, "Snippet Text / النص الكامل", 144);
        _snippetBox.Multiline = true;
        _snippetBox.ScrollBars = ScrollBars.Vertical;
        _snippetBox.SetBounds(0, 170, 380, 170);
        _snippetBox.BackColor = Color.White;
        _snippetBox.ForeColor = Color.Black;
        panel.Controls.Add(_snippetBox);

        var save = new Button { Text = "Save", Left = 0, Top = 354, Width = 90, Height = 36, BackColor = Color.FromArgb(37, 99, 235), ForeColor = Color.White, FlatStyle = FlatStyle.Flat };
        save.Click += (_, _) => SaveSnippet();
        panel.Controls.Add(save);

        var clear = new Button { Text = "New", Left = 100, Top = 354, Width = 90, Height = 36 };
        clear.Click += (_, _) => ClearEditor();
        panel.Controls.Add(clear);

        var delete = new Button { Text = "Delete", Left = 200, Top = 354, Width = 90, Height = 36, BackColor = Color.FromArgb(239, 68, 68), ForeColor = Color.White, FlatStyle = FlatStyle.Flat };
        delete.Click += (_, _) => DeleteSelected();
        panel.Controls.Add(delete);

        var copy = new Button { Text = "Copy Text", Left = 300, Top = 354, Width = 90, Height = 36 };
        copy.Click += (_, _) => Clipboard.SetText(_snippetBox.Text ?? string.Empty);
        panel.Controls.Add(copy);

        var hint = new Label
        {
            Dock = DockStyle.Fill,
            Text = "How to use:\n1. Add a keyword like ;hi or ;35.\n2. Write the full response.\n3. Open any normal text field in Windows.\n4. Type the keyword. It expands automatically when it matches a saved canned response.",
            ForeColor = Color.FromArgb(203, 213, 225),
            Padding = new Padding(0, 8, 0, 0)
        };
        parent.Controls.Add(hint);
    }

    private static void AddLabel(Control parent, string text, int top)
    {
        parent.Controls.Add(new Label { Text = text, Left = 0, Top = top, Width = 390, Height = 22, ForeColor = Color.FromArgb(148, 163, 184) });
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
        _grid.DataSource = _view;
        SetStatus($"{items.Count} snippets loaded. Expander is {(_engine.Enabled ? "enabled" : "disabled")}.");
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
        using var dialog = new SaveFileDialog { Filter = "JSON backup (*.json)|*.json|Beeftext backup (*.btbackup)|*.btbackup", FileName = "cmd-snippets.json" };
        if (dialog.ShowDialog(this) == DialogResult.OK)
        {
            File.Copy(Path.Combine(AppContext.BaseDirectory, "snippets.json"), dialog.FileName, true);
            SetStatus("Backup exported.");
        }
    }

    private void ImportBackup()
    {
        using var dialog = new OpenFileDialog { Filter = "Backup files (*.json;*.btbackup)|*.json;*.btbackup|JSON backup (*.json)|*.json|Beeftext backup (*.btbackup)|*.btbackup|All files (*.*)|*.*" };
        if (dialog.ShowDialog(this) == DialogResult.OK)
        {
            try
            {
                _store.ImportFromFile(dialog.FileName);
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

    private void ShowFromTray()
    {
        Show();
        WindowState = FormWindowState.Normal;
        Activate();
    }

    protected override void OnResize(EventArgs e)
    {
        base.OnResize(e);
        if (WindowState == FormWindowState.Minimized)
        {
            Hide();
            _tray.ShowBalloonTip(900, "cmd", "Text expander is still running in the tray.", ToolTipIcon.Info);
        }
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        _tray.Visible = false;
        _tray.Dispose();
        _engine.Dispose();
        base.OnFormClosing(e);
    }
}
