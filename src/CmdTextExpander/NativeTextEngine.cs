using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;

namespace CmdTextExpander;

public sealed class NativeTextEngine : IDisposable
{
    private const int WH_KEYBOARD_LL = 13;
    private const int WM_KEYDOWN = 0x0100;
    private const int WM_SYSKEYDOWN = 0x0104;
    private const int LLKHF_INJECTED = 0x10;

    private readonly Control _ui;
    private readonly SnippetStore _store;
    private readonly Action<string> _status;
    private readonly StringBuilder _buffer = new();
    private readonly LowLevelKeyboardProc _proc;
    private IntPtr _hook = IntPtr.Zero;
    private bool _enabled;
    private bool _internalPaste;

    public bool Enabled => _enabled;

    public NativeTextEngine(Control ui, SnippetStore store, Action<string> status)
    {
        _ui = ui;
        _store = store;
        _status = status;
        _proc = HookCallback;
    }

    public void Start()
    {
        if (_hook == IntPtr.Zero)
        {
            _hook = SetWindowsHookEx(WH_KEYBOARD_LL, _proc, IntPtr.Zero, 0);
            if (_hook == IntPtr.Zero) throw new InvalidOperationException("Failed to install keyboard hook.");
        }
        _enabled = true;
        _status("Text expander enabled.");
    }

    public void Stop()
    {
        _enabled = false;
        _buffer.Clear();
        _status("Text expander disabled.");
    }

    public void Dispose()
    {
        if (_hook != IntPtr.Zero)
        {
            UnhookWindowsHookEx(_hook);
            _hook = IntPtr.Zero;
        }
    }

    private IntPtr HookCallback(int nCode, IntPtr wParam, IntPtr lParam)
    {
        if (nCode < 0 || !_enabled || _internalPaste)
            return CallNextHookEx(_hook, nCode, wParam, lParam);

        if (wParam != (IntPtr)WM_KEYDOWN && wParam != (IntPtr)WM_SYSKEYDOWN)
            return CallNextHookEx(_hook, nCode, wParam, lParam);

        var info = Marshal.PtrToStructure<KBDLLHOOKSTRUCT>(lParam);
        if ((info.flags & LLKHF_INJECTED) != 0)
            return CallNextHookEx(_hook, nCode, wParam, lParam);

        var key = (Keys)info.vkCode;

        if (key == Keys.Back)
        {
            if (_buffer.Length > 0) _buffer.Length--;
            return CallNextHookEx(_hook, nCode, wParam, lParam);
        }

        if (key == Keys.Escape || key == Keys.Left || key == Keys.Right || key == Keys.Up || key == Keys.Down || key == Keys.Home || key == Keys.End)
        {
            _buffer.Clear();
            return CallNextHookEx(_hook, nCode, wParam, lParam);
        }

        if (IsDelimiter(key))
        {
            var keyword = _buffer.ToString();
            _buffer.Clear();
            if (!string.IsNullOrWhiteSpace(keyword))
            {
                var match = _store.MatchKeyword(keyword);
                if (match is not null)
                {
                    _ui.BeginInvoke(new Action(() => ReplaceKeyword(keyword.Length, match.Text)));
                    return (IntPtr)1;
                }
            }
            return CallNextHookEx(_hook, nCode, wParam, lParam);
        }

        var text = KeyToUnicode((uint)info.vkCode, info.scanCode);
        if (!string.IsNullOrEmpty(text))
        {
            foreach (var ch in text)
            {
                if (!char.IsControl(ch))
                {
                    _buffer.Append(ch);
                    if (_buffer.Length > 80) _buffer.Remove(0, _buffer.Length - 80);
                }
            }
        }

        return CallNextHookEx(_hook, nCode, wParam, lParam);
    }

    private static bool IsDelimiter(Keys key)
    {
        return key == Keys.Space || key == Keys.Enter || key == Keys.Tab;
    }

    private void ReplaceKeyword(int charsToDelete, string replacement)
    {
        try
        {
            _internalPaste = true;
            for (var i = 0; i < charsToDelete; i++) SendKeys.SendWait("{BACKSPACE}");

            var oldText = Clipboard.ContainsText() ? Clipboard.GetText() : null;
            Clipboard.SetText(replacement ?? string.Empty);
            SendKeys.SendWait("^v");
            _status("Expanded snippet.");

            if (oldText is not null)
            {
                var timer = new System.Windows.Forms.Timer { Interval = 400 };
                timer.Tick += (_, _) =>
                {
                    timer.Stop();
                    timer.Dispose();
                    try { Clipboard.SetText(oldText); } catch { }
                };
                timer.Start();
            }
        }
        catch (Exception ex)
        {
            _status("Expansion failed: " + ex.Message);
        }
        finally
        {
            _internalPaste = false;
        }
    }

    private static string KeyToUnicode(uint vkCode, uint scanCode)
    {
        var keyboardState = new byte[256];
        if (!GetKeyboardState(keyboardState)) return string.Empty;
        var sb = new StringBuilder(8);
        var result = ToUnicode(vkCode, scanCode, keyboardState, sb, sb.Capacity, 0);
        return result > 0 ? sb.ToString(0, result) : string.Empty;
    }

    private delegate IntPtr LowLevelKeyboardProc(int nCode, IntPtr wParam, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)]
    private struct KBDLLHOOKSTRUCT
    {
        public uint vkCode;
        public uint scanCode;
        public int flags;
        public uint time;
        public IntPtr dwExtraInfo;
    }

    [DllImport("user32.dll", SetLastError = true)]
    private static extern IntPtr SetWindowsHookEx(int idHook, LowLevelKeyboardProc lpfn, IntPtr hMod, uint dwThreadId);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool UnhookWindowsHookEx(IntPtr hhk);

    [DllImport("user32.dll")]
    private static extern IntPtr CallNextHookEx(IntPtr hhk, int nCode, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern bool GetKeyboardState(byte[] lpKeyState);

    [DllImport("user32.dll")]
    private static extern int ToUnicode(uint wVirtKey, uint wScanCode, byte[] lpKeyState, [Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder pwszBuff, int cchBuff, uint wFlags);
}
