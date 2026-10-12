using System.Runtime.InteropServices;
using System.Runtime.Versioning;
using System.Security.Cryptography;

namespace SFExporter.Desktop;

internal static class DesktopSessionProtector
{
    internal static ISessionProtector Create(string sessionDirectory) => OperatingSystem.IsWindows()
        ? new WindowsSessionProtector() : new FileSessionProtector(Path.Combine(sessionDirectory, ".keys"));
}

// Windows host mode uses the same CurrentUser DPAPI format as the native Windows backend.
[SupportedOSPlatform("windows")]
internal sealed class WindowsSessionProtector : ISessionProtector
{
    [StructLayout(LayoutKind.Sequential)]
    private struct Blob { internal int Length; internal IntPtr Data; }
    [DllImport("crypt32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CryptProtectData(ref Blob input, IntPtr description, IntPtr entropy,
        IntPtr reserved, IntPtr prompt, uint flags, out Blob output);
    [DllImport("crypt32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CryptUnprotectData(ref Blob input, IntPtr description, IntPtr entropy,
        IntPtr reserved, IntPtr prompt, uint flags, out Blob output);
    [DllImport("kernel32.dll")] private static extern IntPtr LocalFree(IntPtr memory);

    public byte[] Protect(byte[] data) => Transform(data, false);
    public byte[] Unprotect(byte[] data) => Transform(data, true);
    private static byte[] Transform(byte[] data, bool decrypt)
    {
        var pinned = GCHandle.Alloc(data, GCHandleType.Pinned);
        Blob output = default;
        try
        {
            var input = new Blob { Length = data.Length, Data = pinned.AddrOfPinnedObject() };
            bool ok = decrypt
                ? CryptUnprotectData(ref input, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, 1, out output)
                : CryptProtectData(ref input, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, 1, out output);
            if (!ok) throw new CryptographicException("无法读取当前用户的登录会话，请重新登录");
            var result = new byte[output.Length];
            Marshal.Copy(output.Data, result, 0, result.Length);
            return result;
        }
        finally
        {
            pinned.Free();
            if (output.Data != IntPtr.Zero)
            {
                for (int i = 0; i < output.Length; i++) Marshal.WriteByte(output.Data, i, 0);
                LocalFree(output.Data);
            }
        }
    }
}

// Linux/macOS store an AES key in the application's private directory, never alongside ebook outputs.
// This protects against other local users; processes running as the same user can access the key.
internal sealed class FileSessionProtector : ISessionProtector
{
    private readonly Lazy<byte[]> key;
    internal FileSessionProtector(string keyDirectory)
    {
        string directory = Path.GetFullPath(keyDirectory);
        key = new Lazy<byte[]>(() => ReadOrCreateKey(directory));
    }
    public byte[] Protect(byte[] data)
    {
        var output = new byte[32 + data.Length];
        "SFE2"u8.CopyTo(output);
        RandomNumberGenerator.Fill(output.AsSpan(4, 12));
        using var aes = new AesGcm(key.Value, 16);
        aes.Encrypt(output.AsSpan(4, 12), data, output.AsSpan(32), output.AsSpan(16, 16));
        return output;
    }
    public byte[] Unprotect(byte[] data)
    {
        if (data.Length < 32 || !data.AsSpan(0, 4).SequenceEqual("SFE2"u8))
            throw new CryptographicException("登录会话格式无效，请重新登录");
        var output = new byte[data.Length - 32];
        using var aes = new AesGcm(key.Value, 16);
        aes.Decrypt(data.AsSpan(4, 12), data.AsSpan(32), data.AsSpan(16, 16), output);
        return output;
    }
    private static byte[] ReadOrCreateKey(string directory)
    {
        if (Directory.Exists(directory) && (File.GetAttributes(directory) & FileAttributes.ReparsePoint) != 0)
            throw new CryptographicException("会话密钥目录不能是符号链接");
        if (OperatingSystem.IsWindows()) Directory.CreateDirectory(directory);
        else
        {
            Directory.CreateDirectory(directory, UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute);
            File.SetUnixFileMode(directory, UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute);
        }
        string path = Path.Combine(directory, "session.key");
        if (!File.Exists(path))
        {
            byte[] generated = RandomNumberGenerator.GetBytes(32);
            string temporary = Path.Combine(directory, Guid.NewGuid().ToString("N") + ".part");
            try
            {
                var options = new FileStreamOptions { Mode = FileMode.CreateNew, Access = FileAccess.Write, Share = FileShare.None };
                if (!OperatingSystem.IsWindows()) options.UnixCreateMode = UnixFileMode.UserRead | UnixFileMode.UserWrite;
                using (var file = new FileStream(temporary, options)) { file.Write(generated); file.Flush(true); }
                try { File.Move(temporary, path, overwrite: false); }
                catch (IOException) when (File.Exists(path)) { /* Another process published its complete key first. */ }
            }
            finally
            {
                CryptographicOperations.ZeroMemory(generated);
                if (File.Exists(temporary)) File.Delete(temporary);
            }
        }
        if ((File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0)
            throw new CryptographicException("会话密钥不能是符号链接");
        if (!OperatingSystem.IsWindows()) File.SetUnixFileMode(path, UnixFileMode.UserRead | UnixFileMode.UserWrite);
        using var source = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        if (source.Length != 32) throw new CryptographicException("会话密钥损坏，请移除损坏的会话密钥并重新登录");
        var value = new byte[32];
        source.ReadExactly(value);
        return value;
    }
}
