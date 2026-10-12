using Avalonia;
using Avalonia.Controls;
using Avalonia.Media.Imaging;

namespace SFExporter.Desktop;

internal static class Program
{
    [STAThread]
    public static int Main(string[] args)
    {
        try
        {
            if (args.Contains("--self-test"))
            {
                SelfTest.Run().GetAwaiter().GetResult();
                return 0;
            }
            if (args.Contains("--render-test"))
            {
                BuildAvaloniaApp().SetupWithoutStarting();
                using var engine = new NativeEngine(Path.Combine(AppContext.BaseDirectory, "test-artifacts", "render"), Path.Combine(AppContext.BaseDirectory, "test-artifacts", "render-session.dat"));
                var window = new MainWindow(engine, restoreSession: false);
                if (!string.IsNullOrEmpty(window.BookIdValue)) throw new Exception("The book ID must start empty");
                window.ShowActivated = false;
                window.ShowInTaskbar = false;
                window.WindowStartupLocation = WindowStartupLocation.Manual;
                window.Position = new PixelPoint(-12000, -12000);
                window.Show();
                Avalonia.Threading.Dispatcher.UIThread.RunJobs();
                window.Measure(new Size(1000, 820));
                window.Arrange(new Rect(0, 0, 1000, 820));
                window.UpdateLayout();
                using var bitmap = new RenderTargetBitmap(new PixelSize(1000, 820));
                bitmap.Render(window);
                var folder = Path.Combine(AppContext.BaseDirectory, "test-artifacts");
                Directory.CreateDirectory(folder);
                bitmap.Save(Path.Combine(folder, "avalonia-ui.png"));
                window.Close();
                return 0;
            }
            return BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
        }
        catch (Exception e) { Console.Error.WriteLine(e); return 1; }
    }
    public static AppBuilder BuildAvaloniaApp() => AppBuilder.Configure<App>().UsePlatformDetect().LogToTrace();
}
