using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using SFExporter.Desktop;

namespace SFExporter.AndroidApp;

public sealed partial class MobileApp : Application
{
    internal static MobileView? CurrentView { get; private set; }
    public override void Initialize() => AvaloniaXamlLoader.Load(this);

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is ISingleViewApplicationLifetime lifetime)
        {
            var context = Android.App.Application.Context;
            string root = context.FilesDir!.AbsolutePath;
            string resourceDirectory = Path.Combine(root, "resource");
            Directory.CreateDirectory(resourceDirectory);
            string table = Path.Combine(resourceDirectory, "cipher-table.json");
            // Replace the packaged table on upgrade. No migration of old desktop/app caches.
            using (var input = context.Assets!.Open("resource/cipher-table.json"))
            using (var output = File.Create(table)) input.CopyTo(output);
            string certificates = Path.Combine(resourceDirectory, "cacert.pem");
            using (var input = context.Assets!.Open("resource/cacert.pem"))
            using (var output = File.Create(certificates)) input.CopyTo(output);
            ManagedHost.Install(new AndroidSessionProtector());
            var engine = new NativeEngine(root, Path.Combine(root, "session.dat"), table, certificates);
            lifetime.MainView = CurrentView = new MobileView(engine, root);
        }
        base.OnFrameworkInitializationCompleted();
    }
}
