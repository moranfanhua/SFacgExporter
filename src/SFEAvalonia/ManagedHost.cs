using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;

namespace SFExporter.Desktop;

internal interface ISessionProtector
{
    byte[] Protect(byte[] data);
    byte[] Unprotect(byte[] data);
}

// Platform services only protect stored sessions. HTTP and request signing run in SFECore.
internal sealed class ManagedHost
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr RequestCallback(IntPtr request);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate void FreeCallback(IntPtr response);
    [DllImport("SFECore", EntryPoint = "sf_set_host_callbacks", CallingConvention = CallingConvention.Cdecl)]
    private static extern void SetCallbacks(RequestCallback request, FreeCallback free);

    private static ManagedHost? installed;
    private static readonly object installationLock = new();
    internal static bool IsInstalled { get { lock (installationLock) return installed is not null; } }
    // Native stores pointers; keep delegates alive for the lifetime of the application.
    private readonly RequestCallback requestCallback;
    private readonly FreeCallback freeCallback;
    private readonly ISessionProtector protector;

    private ManagedHost(ISessionProtector protector)
    {
        this.protector = protector;
        requestCallback = Handle;
        freeCallback = Marshal.FreeCoTaskMem;
    }

    internal static ManagedHost Install(ISessionProtector protector)
    {
        lock (installationLock)
        {
            if (installed is not null) throw new InvalidOperationException("Session protection already initialized");
            var host = new ManagedHost(protector);
            SetCallbacks(host.requestCallback, host.freeCallback);
            installed = host;
            return host;
        }
    }

#if !ANDROID
    internal static void EnsureDesktop(string sessionDirectory)
    {
        lock (installationLock)
            if (installed is null) Install(DesktopSessionProtector.Create(sessionDirectory));
    }
#endif

    private IntPtr Handle(IntPtr request)
    {
        string response;
        try
        {
            using var document = JsonDocument.Parse(Marshal.PtrToStringUTF8(request)!);
            var r = document.RootElement;
            string result = r.GetProperty("action").GetString() switch
            {
                "protect" => Transform(r, false),
                "unprotect" => Transform(r, true),
                _ => throw new InvalidOperationException("未知的会话加密操作")
            };
            response = JsonSerializer.Serialize(new { ok = true, result });
        }
        catch (Exception)
        {
            // Never let managed exceptions cross the native callback or expose session contents.
            response = JsonSerializer.Serialize(new { ok = false, error = "本机登录会话加密或读取失败，请重新登录" });
        }
        return Marshal.StringToCoTaskMemUTF8(response);
    }

    private string Transform(JsonElement request, bool decrypt)
    {
        byte[] data = Convert.FromBase64String(request.GetProperty("data").GetString()!);
        byte[]? output = null;
        try
        {
            output = decrypt ? protector.Unprotect(data) : protector.Protect(data);
            return Convert.ToBase64String(output);
        }
        finally
        {
            CryptographicOperations.ZeroMemory(data);
            if (output is not null) CryptographicOperations.ZeroMemory(output);
        }
    }
}
