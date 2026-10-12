using System.Runtime.InteropServices;
using System.Text.Json;
namespace SFExporter.Desktop;

public sealed record EngineEvent(string Type, string Text, int Done, int Total);
public sealed class NativeEngine : IDisposable
{
    static NativeEngine()
    {
        if (!OperatingSystem.IsAndroid())
            NativeLibrary.SetDllImportResolver(typeof(NativeEngine).Assembly,
                (name, _, _) => name == "SFECore" ? NativeLibrary.Load(Path.Combine(AppContext.BaseDirectory, LibraryFileName)) : IntPtr.Zero);
    }
    internal static string LibraryFileName => OperatingSystem.IsWindows() ? "SFECore.dll"
        : OperatingSystem.IsMacOS() ? "libSFECore.dylib"
        : OperatingSystem.IsLinux() || OperatingSystem.IsAndroid() ? "libSFECore.so"
        : throw new PlatformNotSupportedException("Core supports Windows, Linux, macOS and Android");
    private readonly SemaphoreSlim gate = new(1, 1);
    private readonly Native.EventCallback callback;
    private IntPtr handle;
    private readonly bool usesHostCallbacks;
    public bool LoggedIn { get; private set; }
    public bool HasSavedSession { get; private set; }
    public event Action<EngineEvent>? Event;
    public NativeEngine(string cacheRoot, string? sessionFile = null, string? cipherTablePath = null, string? caBundlePath = null)
    {
        sessionFile = Path.GetFullPath(sessionFile ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "SFExporter", "session.dat"));
        usesHostCallbacks = Native.UsesHostCallbacks() != 0;
        if (usesHostCallbacks)
        {
#if ANDROID
            if (!ManagedHost.IsInstalled) throw new InvalidOperationException("Android platform services must be initialized before creating the core");
#else
            ManagedHost.EnsureDesktop(Path.GetDirectoryName(sessionFile)!);
#endif
        }
        callback = (ptr, _) =>
        {
            try
            {
                using var document = JsonDocument.Parse(Marshal.PtrToStringUTF8(ptr)!);
                var e = document.RootElement;
                Event?.Invoke(new(e.GetProperty("type").GetString()!, e.GetProperty("text").GetString()!, e.GetProperty("done").GetInt32(), e.GetProperty("total").GetInt32()));
            }
            catch { /* Managed exceptions must never unwind across the native callback. */ }
        };
        var config = JsonSerializer.Serialize(new
        {
            cacheRoot,
            sessionFile,
            cipherTablePath = cipherTablePath ?? Path.Combine(AppContext.BaseDirectory, "resource", "cipher-table.json"),
            caBundlePath = caBundlePath ?? Path.Combine(AppContext.BaseDirectory, "resource", "cacert.pem")
        });
        handle = Native.Create(config, callback, IntPtr.Zero);
        if (handle == IntPtr.Zero) throw new InvalidOperationException(Marshal.PtrToStringUTF8(Native.InitError()));
    }
    public async Task<JsonElement> CallAsync(object request, CancellationToken ct = default)
    {
        await gate.WaitAsync(ct);
        try
        {
            ObjectDisposedException.ThrowIf(handle == IntPtr.Zero, this);
            Native.ResetCancel(handle);
            using var registration = ct.Register(Cancel);
            var payload = JsonSerializer.Serialize(request);
            return await Task.Run(() =>
            {
                ct.ThrowIfCancellationRequested();
                var ptr = Native.Call(handle, payload);
                try
                {
                    using var doc = JsonDocument.Parse(Marshal.PtrToStringUTF8(ptr)!);
                    var envelope = doc.RootElement;
                    LoggedIn = envelope.GetProperty("loggedIn").GetBoolean();
                    HasSavedSession = envelope.GetProperty("hasSavedSession").GetBoolean();
                    if (!envelope.GetProperty("ok").GetBoolean())
                    {
                        if (envelope.TryGetProperty("cancelled", out var cancelled) && cancelled.GetBoolean()) throw new OperationCanceledException(ct);
                        throw new InvalidOperationException(envelope.GetProperty("error").GetString());
                    }
                    return envelope.GetProperty("result").Clone();
                }
                finally { Native.Free(ptr); }
            }, CancellationToken.None);
        }
        finally { gate.Release(); }
    }
    public void Cancel() { if (handle != IntPtr.Zero) Native.Cancel(handle); }
    public void Dispose()
    {
        if (!gate.Wait(0)) throw new InvalidOperationException("Cancel and await the running native operation before disposal");
        try { if (handle != IntPtr.Zero) { Native.Destroy(handle); handle = IntPtr.Zero; } }
        finally { gate.Release(); }
        GC.KeepAlive(callback);
    }
    private static class Native
    {
        [DllImport("SFECore", EntryPoint="sf_uses_host_callbacks", CallingConvention=CallingConvention.Cdecl)] internal static extern int UsesHostCallbacks();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] internal delegate void EventCallback(IntPtr text, IntPtr user);
        [DllImport("SFECore", EntryPoint="sf_create", CallingConvention=CallingConvention.Cdecl)] internal static extern IntPtr Create([MarshalAs(UnmanagedType.LPUTF8Str)] string config, EventCallback callback, IntPtr user);
        [DllImport("SFECore", EntryPoint="sf_init_error", CallingConvention=CallingConvention.Cdecl)] internal static extern IntPtr InitError();
        [DllImport("SFECore", EntryPoint="sf_call", CallingConvention=CallingConvention.Cdecl)] internal static extern IntPtr Call(IntPtr handle, [MarshalAs(UnmanagedType.LPUTF8Str)] string request);
        [DllImport("SFECore", EntryPoint="sf_free", CallingConvention=CallingConvention.Cdecl)] internal static extern void Free(IntPtr value);
        [DllImport("SFECore", EntryPoint="sf_reset_cancel", CallingConvention=CallingConvention.Cdecl)] internal static extern void ResetCancel(IntPtr handle);
        [DllImport("SFECore", EntryPoint="sf_cancel", CallingConvention=CallingConvention.Cdecl)] internal static extern void Cancel(IntPtr handle);
        [DllImport("SFECore", EntryPoint="sf_destroy", CallingConvention=CallingConvention.Cdecl)] internal static extern void Destroy(IntPtr handle);
    }
}
