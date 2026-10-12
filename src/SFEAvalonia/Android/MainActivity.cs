using Android.App;
using Android.Content.PM;
using Avalonia;
using Avalonia.Android;

namespace SFExporter.AndroidApp;

[Activity(Label = "菠萝包小说导出", Theme = "@style/SFETheme", MainLauncher = true, Exported = true,
    ConfigurationChanges = ConfigChanges.Orientation | ConfigChanges.ScreenSize | ConfigChanges.UiMode,
    WindowSoftInputMode = Android.Views.SoftInput.AdjustResize)]
public sealed class MainActivity : AvaloniaMainActivity<MobileApp>
{
    protected override AppBuilder CustomizeAppBuilder(AppBuilder builder) => base.CustomizeAppBuilder(builder).LogToTrace();

    protected override void OnStop()
    {
        // Export is a foreground operation. Cancel cleanly if Android backgrounds the activity.
        MobileApp.CurrentView?.StopWhenBackgrounded();
        base.OnStop();
    }
}
