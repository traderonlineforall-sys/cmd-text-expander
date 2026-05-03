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
    private static readonly Color TextColor = Color.FromArgb(226, 232, 240);
    private static readonly Color Muted = Color.FromArgb(148, 163, 184);
    private static readonly Color Blue = Color.FromArgb(37, 99, 235);
    private static readonly Color Green = Color.FromArgb(34, 197, 94);
    private static readonly Color Red = Color.FromArgb(239, 68, 68);

    public MainForm()
    {
        Text = "cmd";
        Width = 1180;
        Height = 760;
        MinimumSize = new Size(980, 640);
        StartPosition = FormStartPosition.CenterScreen;
        BackColor = Bg;
        ForeColor = TextColor;
        Font = new Font("Segoe UI", 10);

        var dataPath = Path.Combine(AppContext.BaseDirectory, "snippets.json");
        _store = new SnippetStore(dataPath);
        _engine = new NativeTextEngine(this, _store, SetStatus);
        _tray = BuildTrayIcon();

        BuildLayout();
        try
        {
            _engine.Start();
        }
        catch (Exception ex)
        {
            SetStatus("Text expander could not start: " + ex.Message);
        }
        RefreshGrid();
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
        SuspendLayout();

        var header = new Panel
        {
            Dock = DockStyle.Top,
            Height = 92,
            BackColor = PanelBg,
            Padding = new Padding(14)
        };

        var body = new SplitContainer
        {
            Dock = DockStyle.Fill,
            SplitterDistance = 690,
            Panel1MinSize = 480,
            Panel2MinSize = 390,
            BackColor = Bg,
            FixedPanel = FixedPanel.Panel2
        };

        Controls.Add(body);
        Controls.Add(header);

        var title = new Label
        {
            Text = "cmd — Portable Text Expander",
            AutoSize = false,
            Left = 14,
            Top = 12,
            Width = 350,
            Height = 32,
            Font = new Font("Segoe UI", 18, FontStyle.Bold),
            ForeColor = Color.White,
            TextAlign = ContentAlignment.MiddleLeft
        };
        header.Controls.Add(title);

        var subtitle = new Label
        {
            Text = "Beeftext-style canned responses. Type keyword then Space / Enter / Tab.",
            AutoSize = false,
            Left = 16,
            Top = 48,
            Width = 640,
            Height = 22,
            ForeColor = Muted,
            TextAlign = ContentAlignment.MiddleLeft
        };
        header.Controls.Add(subtitle);

        _enableButton.Text = "Enable";
        StyleButton(_enableButton, Green, Color.White);
        _enableButton.SetBounds(380, 18, 110, 38);
        _enableButton.Click += (_, _) => { _engine.Start(); RefreshGrid(); };
        header.Controls.Add(_enableButton);

        _disableButton.Text = "Disable";
        StyleButton(_disableButton, Red, Color.White);
        _disableButton.SetBounds(502, 18, 110, 38);
        _disableButton.Click += (_, _) => { _engine.Stop(); RefreshGrid(); };
        header.Controls.Add(_disableButton);

        var openData = new Button { Text = "Open Data Folder" };
        StyleButton(openData, Blue, Color.White);
        openData.SetBounds(624, 18, 160, 38);
        openData.Click += (_, _) => Process.Start("explorer.exe", AppContext.BaseDirectory);
        header.Controls.Add(openData);

        _status.Text = "Ready";
        _status.AutoSize = false;
        _status.Left = 800;
        _status.Top = 24;
        _status.Width = 320;
        _status.Height = 38;
        _status.ForeColor = Muted;
        _status.TextAlign = ContentAlignment.MiddleLeft;
        header.Controls.Add(_status);

        BuildLibraryPanel(body.Panel1);
        BuildEditorPanel(body.Panel2);

        ResumeLayout(true);
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

    private void BuildLibraryPanel(Control parent)
    {
        parent.Padding = new Padding(12);
        parent.BackColor = Bg;

        var searchRow = new TableLayoutPanel
        {
            Dock = DockStyle.Top,
            Height = 52,
            BackColor = Bg,
            ColumnCount = 3,
            RowCount = 1,
            Padding = new Padding(0, 6, 0, 6)
        };
        searchRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        searchRow.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 138));
        searchRow.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 138));
        parent.Controls.Add(searchRow);

        _searchBox.PlaceholderText = "Search keyword or text...";
        _searchBox.Dock = DockStyle.Fill;
        searchRow.Controls.Add(_searchBox, 0, 0);
        _searchBox.TextChanged += (_, _) => RefreshGrid();

        var exportButton = new Button { Text = "Export Backup", Dock = DockStyle.Fill };
        exportButton.Click += (_, _) => ExportBackup();
        searchRow.Controls.Add(exportButton, 1, 0);

        var importButton = new Button { Text = "Import Backup", Dock = DockStyle.Fill };
        importButton.Click += (_, _) => ImportBackup();
        searchRow.Controls.Add(importButton, 2, 0);

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
        _grid.DefaultCellStyle.BackColor = SoftPanel;
        _grid.DefaultCellStyle.ForeColor = Color.White;
        _grid.DefaultCellStyle.SelectionBackColor = Blue;
        _grid.DefaultCellStyle.SelectionForeColor = Color.White;
        _grid.ColumnHeadersDefaultCellStyle.BackColor = Color.FromArgb(30, 41, 59);
        _grid.ColumnHeadersDefaultCellStyle.ForeColor = Color.White;
        _grid.EnableHeadersVisualStyles = false;
        _grid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "Keyword", DataPropertyName = "Keyword", Width = 145 });
        _grid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "Group", DataPropertyName = "Group", Width = 115 });
        _grid.Columns.Add(new DataGridViewTextBoxColumn { HeaderText = "Text", DataPropertyName = "Text", AutoSizeMode = DataGridViewAutoSizeColumnMode.Fill });
        _grid.CellDoubleClick += (_, _) => LoadSelectedForEdit();
        parent.Controls.Add(_grid);
    }

    private void BuildEditorPanel(Control parent)
    {
        parent.Padding = new Padding(14);
        parent.BackColor = PanelBg;

        var layout = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            BackColor = PanelBg,
            ColumnCount = 1,
            RowCount = 9,
            Padding = new Padding(0),
            AutoSize = false
        };
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 42));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 24));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 40));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 24));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 40));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 24));
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 48));
        layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 130));
        parent.Controls.Add(layout);

        var heading = new Label
        {
            Text = "Add / Edit Canned Response",
            Dock = DockStyle.Fill,
            Font = new Font("Segoe UI", 14, FontStyle.Bold),
            ForeColor = Color.White,
            TextAlign = ContentAlignment.MiddleLeft
        };
        layout.Controls.Add(heading, 0, 0);

        layout.Controls.Add(MakeLabel("Keyword — الاختصار المطلوب"), 0, 1);
        _keywordBox.Dock = DockStyle.Fill;
        _keywordBox.PlaceholderText = "مثال: ;hi أو .شكر";
        _keywordBox.BackColor = Color.White;
        _keywordBox.ForeColor = Color.Black;
        layout.Controls.Add(_keywordBox, 0, 2);

        layout.Controls.Add(MakeLabel("Group — التصنيف اختياري"), 0, 3);
        _groupBox.Dock = DockStyle.Fill;
        _groupBox.PlaceholderText = "مثال: Support / Billing / Technical";
        _groupBox.BackColor = Color.White;
        _groupBox.ForeColor = Color.Black;
        layout.Controls.Add(_groupBox, 0, 4);

        layout.Controls.Add(MakeLabel("Snippet Text — النص الكامل"), 0, 5);
        _snippetBox.Dock = DockStyle.Fill;
        _snippetBox.Multiline = true;
        _snippetBox.ScrollBars = ScrollBars.Vertical;
        _snippetBox.AcceptsReturn = true;
        _snippetBox.AcceptsTab = true;
        _snippetBox.BackColor = Color.White;
        _snippetBox.ForeColor = Color.Black;
        layout.Controls.Add(_snippetBox, 0, 6);

        var buttons = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 4,
            RowCount = 1,
            BackColor = PanelBg,
            Padding = new Padding(0, 8, 0, 4)
        };
        buttons.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 25));
        buttons.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 25));
        buttons.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 25));
        buttons.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 25));
        layout.Controls.Add(buttons, 0, 7);

        var save = new Button { Text = "Save", Dock = DockStyle.Fill };
        StyleButton(save, Blue, Color.White);
        save.Click += (_, _) => SaveSnippet();
        buttons.Controls.Add(save, 0, 0);

        var clear = new Button { Text = "New", Dock = DockStyle.Fill };
        clear.Click += (_, _) => ClearEditor();
        buttons.Controls.Add(clear, 1, 0);

        var delete = new Button { Text = "Delete", Dock = DockStyle.Fill };
        StyleButton(delete, Red, Color.White);
        delete.Click += (_, _) => DeleteSelected();
        buttons.Controls.Add(delete, 2, 0);

        var copy = new Button { Text = "Copy Text", Dock = DockStyle.Fill };
        copy.Click += (_, _) => Clipboard.SetText(_snippetBox.Text ?? string.Empty);
        buttons.Controls.Add(copy, 3, 0);

        var hint = new Label
        {
            Dock = DockStyle.Fill,
            Text = "طريقة الاستخدام:\n1. اكتب Keyword مثل ;hi في الخانة الأولى.\n2. اكتب الرد الكامل في Snippet Text.\n3. اضغط Save.\n4. افتح Notepad أو Chrome واكتب الاختصار ثم Space / Enter / Tab.\n\nدبل كليك على أي سطر من المكتبة لتعديله.",
            ForeColor = Color.FromArgb(203, 213, 225),
            Padding = new Padding(0, 8, 0, 0),
            TextAlign = ContentAlignment.TopLeft
        };
        layout.Controls.Add(hint, 0, 8);
    }

    private static Label MakeLabel(string text)
    {
        return new Label
        {
            Text = text,
            Dock = DockStyle.Fill,
            ForeColor = Muted,
            TextAlign = ContentAlignment.BottomLeft
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
        SetStatus($"{items.Count} snippets loaded. Expander is {(_engine.Enabled ? "enabled" : "disabled")}.");
    }

    private void SaveSnippet()
    {
        if (string.IsNullOrWhiteSpace(_keywordBox.Text))
        {
            MessageBox.Show("Keyword is required. اكتب الاختصار في أول خانة.", "cmd", MessageBoxButtons.OK, MessageBoxIcon.Warning);
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
        using var dialog = new SaveFileDialog { Filter = "JSON backup (*.json)|*.json", FileName = "cmd-snippets.json" };
        if (dialog.ShowDialog(this) == DialogResult.OK)
        {
            File.Copy(Path.Combine(AppContext.BaseDirectory, "snippets.json"), dialog.FileName, true);
            SetStatus("Backup exported.");
        }
    }

    private void ImportBackup()
    {
        using var dialog = new OpenFileDialog { Filter = "JSON backup (*.json)|*.json" };
        if (dialog.ShowDialog(this) == DialogResult.OK)
        {
            File.Copy(dialog.FileName, Path.Combine(AppContext.BaseDirectory, "snippets.json"), true);
            _store.Load();
            RefreshGrid();
            SetStatus("Backup imported.");
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
