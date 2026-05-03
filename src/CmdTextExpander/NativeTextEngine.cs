using System;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Forms;

namespace CmdTextExpander;

public sealed class NativeTextEngine : IDisposable
{
    private const int WH_KEYBOARD_LL = 13;
    private const int WM_KEYDOWN = 0x0100;
    private const int WM_SYSKEYDOWN = 0x0104;
    private const int LLKHF_INJECTED = 0x10;
    private const int INPUT_KEYBOARD = 1;
    private const uint KEYEVENTF_KEYUP = 0x0002;
    private const ushort VK_BACK = 0x08;
    private const ushort VK_CONTROL = 0x11;
    private const ushort VK_V = 0x56;

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
        _buffer.Clear();
        _status("Enabled. Type a saved keyword to expand immediately.");
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

        if (IsOurOwnWindowActive())
            return CallNextHookEx(_hook, nCode, wParam, lParam);

        var targetWindow = GetForegroundWindow();
        var key = (Keys)info.vkCode;

        if (key == Keys.Back)
        {
            if (_buffer.Length > 0) _buffer.Length--;
            return CallNextHookEx(_hook, nCode, wParam, lParam);
        }

        if (key == Keys.Escape || key == Keys.Left || key == Keys.Right || key == Keys.Up || key == Keys.Down || key == Keys.Home || key == Keys.End || key == Keys.Delete)
        {
            _buffer.Clear();
            return CallNextHookEx(_hook, nCode, wParam, lParam);
        }

        if (IsDelimiter(key))
        {
            var keyword = _buffer.ToString();
            var match = _store.MatchKeyword(keyword);
            _buffer.Clear();
            if (match is not null)
            {
                _ui.BeginInvoke(new Action(() => ReplaceInTargetWindow(targetWindow, keyword.Length, match.Text)));
                return (IntPtr)1;
            }
            return CallNextHookEx(_hook, nCode, wParam, lParam);
        }

        var typedText = KeyToUnicode((uint)info.vkCode, info.scanCode);
        if (string.IsNullOrEmpty(typedText))
            return CallNextHookEx(_hook, nCode, wParam, lParam);

        var printable = new string(typedText.Where(ch => !char.IsControl(ch)).ToArray());
        if (printable.Length == 0)
            return CallNextHookEx(_hook, nCode, wParam, lParam);

        var before = _buffer.ToString();
        var candidate = before + printable;
        var matchNow = FindImmediateMatch(candidate);
        if (matchNow is not null)
        {
            var alreadyTypedChars = Math.Max(0, matchNow.Keyword.Length - printable.Length);
            _buffer.Clear();
            _ui.BeginInvoke(new Action(() => ReplaceInTargetWindow(targetWindow, alreadyTypedChars, matchNow.Text)));
            return (IntPtr)1;
        }

        _buffer.Append(printable);
        if (_buffer.Length > 120) _buffer.Remove(0, _buffer.Length - 120);
        return CallNextHookEx(_hook, nCode, wParam, lParam);
    }

    private Snippet? FindImmediateMatch(string buffer)
    {
        if (string.IsNullOrEmpty(buffer)) return null;

        return _store.Snippets
            .Where(x => x.Enabled && !string.IsNullOrWhiteSpace(x.Keyword))
            .OrderByDescending(x => x.Keyword.Length)
            .FirstOrDefault(x => buffer.EndsWith(x.Keyword, StringComparison.Ordinal));
    }

    private bool IsOurOwnWindowActive()
    {
        try
        {
            var fg = GetForegroundWindow();
            if (fg == IntPtr.Zero) return false;
            if (fg == _ui.Handle) return true;
            return IsChild(_ui.Handle, fg);
        }
        catch
        {
            return false;
        }
    }

    private static bool IsDelimiter(Keys key)
    {
        return key == Keys.Space || key == Keys.Enter || key == Keys.Tab;
    }

    private void ReplaceInTargetWindow(IntPtr targetWindow, int charsToDelete, string replacement)
    {
        try
        {
            _internalPaste = true;
            charsToDelete = Math.Max(0, charsToDelete);

            if (targetWindow != IntPtr.Zero && targetWindow != _ui.Handle)
            {
                SetForegroundWindow(targetWindow);
                Thread.Sleep(30);
            }

            if (charsToDelete > 0)
                SendBackspaces(charsToDelete);

            var oldText = Clipboard.ContainsText() ? Clipboard.GetText() : null;
            Clipboard.SetText(replacement ?? string.Empty);
            Thread.Sleep(20);
            SendCtrlV();
            _status("Expanded snippet.");

            if (oldText is not null)
            {
                var timer = new System.Windows.Forms.Timer { Interval = 700 };
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

    private static void SendBackspaces(int count)
    {
        if (count <= 0) return;
        var inputs = new INPUT[count * 2];
        for (var i = 0; i < count; i++)
        {
            inputs[i * 2] = KeyInput(VK_BACK, false);
            inputs[i * 2 + 1] = KeyInput(VK_BACK, true);
        }
        SendInput((uint)inputs.Length, inputs, Marshal.SizeOf<INPUT>());
    }

    private static void SendCtrlV()
    {
        var inputs = new[]
        {
            KeyInput(VK_CONTROL, false),
            KeyInput(VK_V, false),
            KeyInput(VK_V, true),
            KeyInput(VK_CONTROL, true)
        };
        SendInput((uint)inputs.Length, inputs, Marshal.SizeOf<INPUT>());
    }

    private static INPUT KeyInput(ushort vk, bool keyUp)
    {
        return new INPUT
        {
            type = INPUT_KEYBOARD,
            u = new InputUnion
            {
                ki = new KEYBDINPUT
                {
                    wVk = vk,
                    wScan = 0,
                    dwFlags = keyUp ? KEYEVENTF_KEYUP : 0,
                    time = 0,
                    dwExtraInfo = UIntPtr.Zero
                }
            }
        };
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

    [StructLayout(LayoutKind.Sequential)]
    private struct INPUT
    {
        public int type;
        public InputUnion u;
    }

    [StructLayout(LayoutKind.Explicit)]
    private struct InputUnion
    {
        [FieldOffset(0)] public KEYBDINPUT ki;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct KEYBDINPUT
    {
        public ushort wVk;
        public ushort wScan;
        public uint dwFlags;
        public uint time;
        public UIntPtr dwExtraInfo;
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

    [DllImport("user32.dll")]
    private static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    private static extern bool IsChild(IntPtr hWndParent, IntPtr hWnd);

    [DllImport("user32.dll")]
    private static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern uint SendInput(uint nInputs, INPUT[] pInputs, int cbSize);
}
