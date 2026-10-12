using Android.Security.Keystore;
using Java.Security;
using Javax.Crypto;
using Javax.Crypto.Spec;
using SFExporter.Desktop;
using System.Security.Cryptography;

namespace SFExporter.AndroidApp;

internal sealed class AndroidSessionProtector : ISessionProtector
{
    private const string Alias = "sfexporter.session.v1";
    private readonly object gate = new();

    private static IKey Key()
    {
        using var store = KeyStore.GetInstance("AndroidKeyStore")!;
        store.Load(null);
        if (!store.ContainsAlias(Alias))
        {
            using var generator = KeyGenerator.GetInstance(KeyProperties.KeyAlgorithmAes, "AndroidKeyStore")!;
            using var builder = new KeyGenParameterSpec.Builder(Alias, KeyStorePurpose.Encrypt | KeyStorePurpose.Decrypt);
            using var spec = builder.SetBlockModes(KeyProperties.BlockModeGcm)!
                .SetEncryptionPaddings(KeyProperties.EncryptionPaddingNone)!.Build()!;
            generator.Init(spec);
            using var generated = generator.GenerateKey();
        }
        return store.GetKey(Alias, null) ?? throw new CryptographicException("Keystore key unavailable");
    }

    public byte[] Protect(byte[] data)
    {
        lock (gate)
        {
            using var key = Key();
            using var cipher = Cipher.GetInstance("AES/GCM/NoPadding")!;
            cipher.Init(CipherMode.EncryptMode, key);
            byte[] iv = cipher.GetIV()!, encrypted = cipher.DoFinal(data)!;
            if (iv.Length != 12) throw new CryptographicException("Unexpected Keystore IV");
            byte[] output = new byte[4 + iv.Length + encrypted.Length];
            "SFE1"u8.CopyTo(output);
            iv.CopyTo(output, 4);
            encrypted.CopyTo(output, 16);
            return output;
        }
    }

    public byte[] Unprotect(byte[] data)
    {
        lock (gate)
        {
            if (data.Length < 32 || !data.AsSpan(0, 4).SequenceEqual("SFE1"u8)) throw new CryptographicException("Invalid session format");
            try
            {
                using var key = Key();
                using var cipher = Cipher.GetInstance("AES/GCM/NoPadding")!;
                using var spec = new GCMParameterSpec(128, data[4..16]);
                cipher.Init(CipherMode.DecryptMode, key, spec);
                return cipher.DoFinal(data[16..])!;
            }
            catch (Java.Security.GeneralSecurityException e) { throw new CryptographicException("Keystore session cannot be decrypted", e); }
        }
    }
}
