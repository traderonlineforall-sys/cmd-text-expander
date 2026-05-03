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

    private static readonly Color Bg = Color.FromArgb(11, 18, 32);
    private static readonly Color PanelBg = Color.FromArgb(15, 23, 42);
    private static readonly Color SoftPanel = Color.FromArgb(17, 24, 39);
    private static readonly Color Muted = Color.FromArgb(148, 163, 184);

    public MainForm()
    {
        Text = "cmd";
        Width = 1000;
        Height = 720;
        MinimumSize = new Size(820, 560);
        StartPosition = FormStartPosition.CenterScreen;
        BackColor = Bg;
        ForeColor = Color.FromArgb(226, 232, 240);
        Font = new Font("Segoe UI", 10);

        var dataPath = Path.Combine(AppContext.BaseDirectory, "snippets.json");
        _store = new SnippetStore(dataPath);
        _engine = new NativeTextEngine(this, _store, SetStatus);
        _tray = BuildTrayIcon();

        BuildLayout();
        RefreshGrid();

        try
        {
            _engine.Start();
        }
        catch (Exception ex)
        {
            SetStatus("Expander did not start: " + ex.Message);
        }
    }

    private NotifyIcon BuildTrayIcon()
    {
        var menu = new ContextMenuStrip();
        menu.Items.Add("Show", null, (_, _) => ShowFromTray());
        menu.Items.Add("Enable Expander", null, (_, _) => { SafeStartEngine(); RefreshGrid(); });
        menu.Items.Add("Disable Expander", null, (_, _) => { _engine.Stop(); RefreshGrid(); });
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
            BackColor = PanelBg,
            Padding = new Padding(14)
        };
        Controls.Add(header);

        var title = new Label
        {
            Text = "cmd — Portable Text Expander",
            Left = 14,
            Top = 10,
            Width = 350,
            Height = 32,
            Font = new Font("Segoe UI", 18, FontStyle.Bold),
            ForeColor = Color.White,
            TextAlign = ContentAlignment.MiddleLeft
        };
        header.Controls.Add(title);

        var subtitle = new Label
        {
            Text = "Type saved keyword anywhere. It expands automatically when matched.",
            Left = 16,
            Top = 48,
            Width = 600,
            Height = 24,
            ForeColor = Muted
        };
        header.Controls.Add(subtitle);

        _enableButton.Text = "Enable";
        _enableButton.SetBounds(380, 18, 110, 38);
        StyleButton(_enableButton, Color.FromArgb(34, 197, 94), Color.White);
        _enableButton.Click += (_, _) => { SafeStartEngine(); RefreshGrid(); };
        header.Controls.Add(_enableButton);

        _disableButton.Text = "Disable";
        _disableButton.SetBounds(500, 18, 110, 38);
        StyleButton(_disableButton, Color.FromArgb(239, 68, 68), Color.White);
        _disableButton.Click += (_, _) => { _engine.Stop(); RefreshGrid(); };
        header.Controls.Add(_disableButton);

        var openData = new Button { Text = "Open Data Folder" };
        openData.SetBounds(620, 18, 160, 38);
        StyleButton(openData, Color.FromArgb(37, 99, 235), Color.White);
        openData.Click += (_, _) => Process.Start("explorer.exe", AppContext.BaseDirectory);
        header.Controls.Add(openData);

        _status.Text = "Ready";
        _status.Left = 800;
        _status.Top = 22;
        _status.Width = 180;
        _status.Height = 42;
        _status.ForeColor = Muted;
        header.Controls.Add(_status);

        var main = new Panel
        {
            Dock = DockStyle.Fill,
            BackColor = Bg
        };
        Controls.Add(main);
        main.BringToFront();
        header.BringToFront();

        var editor = new Panel
        {
            Dock = DockStyle.Top,
            Height = 280,
            BackColor = PanelBg,
            Padding = new Padding(14)
        };
        main.Controls.Add(editor);

        var library = new Panel
        {
            Dock = DockStyle.Fill,
            BackColor = Bg,
            Padding = new Padding(12)
        };
        main.Controls.Add(library);
        library.BringToFront();
        editor.BringToFront();

        BuildEditorPanel(editor);
        BuildLibraryPanel(library);
    }

    private static void StyleButton(Button button, Color back, Color fore)
    {
        button.BackColor = back;
        button.ForeColor = fore;
        button.FlatStyle = FlatStyle.Flat;
        button.FlatAppearance.BorderSize = 0;
        button.Font = new Font("Segoe UI", 10, FontStyle.Bold);
        button.Cursor = Cursors.Hand;
    }

    private void BuildEditorPanel(Panel parent)
    {
        var heading = new Label
        {
            Text = "Add / Edit Canned Response",
            Left = 14,
            Top = 10,
            Width = 360,
            Height = 30,
            Font = new Font("Segoe UI", 14, FontStyle.Bold),
            ForeColor = Color.White
        };
        parent.Controls.Add(heading);

        AddLabel(parent, "Keyword / الاختصار", 14, 48);
        _keywordBox.SetBounds(14, 72, 260, 32);
        _keywordBox.PlaceholderText = "Example: ;hi or ;35";
        _keywordBox.BackColor = Color.White;
        _keywordBox.ForeColor = Color.Black;
        parent.Controls.Add(_keywordBox);

        AddLabel(parent, "Group / التصنيف", 294, 48);
        _groupBox.SetBounds(294, 72, 260, 32);
        _groupBox.PlaceholderText = "Optional category";
        _groupBox.BackColor = Color.White;
        _groupBox.ForeColor = Color.Black;
        parent.Controls.Add(_groupBox);

        AddLabel(parent, "Snippet Text / النص الكامل", 14, 112);
        _snippetBox.SetBounds(14, 136, 720, 88);
        _snippetBox.Multiline = true;
        _snippetBox.ScrollBars = ScrollBars.Vertical;
        _snippetBox.BackColor = Color.White;
        _snippetBox.ForeColor = Color.Black;
        parent.Controls.Add(_snippetBox);

        var save = new Button { Text = "Save" };
        save.SetBounds(14, 234, 90, 34);
        StyleButton(save, Color.FromArgb(37, 99, 235), Color.White);
        save.Click += (_, _) => SaveSnippet();
        parent.Controls.Add(save);

        var clear = new Button { Text = "New" };
        clear.SetBounds(114, 234, 90, 34);
        clear.Click += (_, _) => ClearEditor();
        parent.Controls.Add(clear);

        var delete = new Button { Text = "Delete" };
        delete.SetBounds(214, 234, 90, 34);
        StyleButton(delete, Color.FromArgb(239, 68, 68), Color.White);
        delete.Click += (_, _) => DeleteSelected();
        parent.Controls.Add(delete);

        var copy = new Button { Text = "Copy Text" };
        copy.SetBounds(314, 234, 110, 34);
        copy.Click += (_, _) => Clipboard.SetText(_snippetBox.Text ?? string.Empty);
        parent.Controls.Add(copy);

        var hint = new Label
        {
            Text = "Type the Keyword in any normal text field. The full canned response will appear automatically.",
            Left = 450,
            Top = 238,
            Width = 480,
            Height = 30,
            ForeColor = Color.FromArgb(203, 213, 225)
        };
        parent.Controls.Add(hint);

        parent.Resize += (_, _) =>
        {
            var w = Math.Max(300, parent.ClientSize.Width - 28);
            _snippetBox.Width = w;
            hint.Left = Math.Min(450, parent.ClientSize.Width - 20);
            hint.Width = Math.Max(0, parent.ClientSize.Width - hint.Left - 14);
        };
    }

    private void BuildLibraryPanel(Control parent)
    {
        var searchRow = new Panel { Dock = DockStyle.Top, Height = 52, BackColor = Bg };
        parent.Controls.Add(searchRow);

        _searchBox.PlaceholderText = "Search keyword or text...";
        _searchBox.SetBounds(0, 8, 420, 32);
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
        _grid.BackgroundColor = PanelBg;
        _grid.GridColor = Color.FromArgb(51, 65, 85);
        _grid.ForeColor = Color.White;
        _grid.ColumnHeadersDefaultCellStyle.BackColor = Color.FromArgb(30, 41, 59);
        _grid.ColumnHeadersDefaultCellStyle.ForeColor = Color.White;
        _grid.EnableHeadersVisualStyles = false;
        _grid.Columns.Clear();
        _grid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "Keyword", DataPropertyName = "Keyword", Width = 140 });
        _grid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "Group", DataPropertyName = "Group", Width = 130 });
        _grid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "Text", DataPropertyName = "Text", AutoSizeMode = DataGridViewAutoSizeColumnMode.Fill });
        _grid.CellDoubleClick += (_, _) => LoadSelectedForEdit();
        parent.Controls.Add(_grid);
    }

    private static void AddLabel(Control parent, string text, int left, int top)
    {
        parent.Controls.Add(new Label { Text = text, Left = left, Top = top, Width = 260, Height = 22, ForeColor = Muted });
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
        SetStatus($"{items.Count} snippets. {(_engine.Enabled ? "Enabled" : "Disabled")}");
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

    private void ShowFromTray()
    {
        Show();
        WindowState = FormWindowState.Normal;
        Activate();
    }

    protected override void OnResize(EventArgs e)
    {
        base.OnResize(e);
        // Do not auto-hide. The window remains visible on the taskbar when minimized.
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        _tray.Visible = false;
        _tray.Dispose();
        _engine.Dispose();
        base.OnFormClosing(e);
    }
}
