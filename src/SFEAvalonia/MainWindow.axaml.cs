using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Text;
using System.Text.Json;

namespace SFExporter.Desktop;
public sealed partial class MainWindow : Window
{
    internal string BookIdValue => BookIdInput.Text ?? "";
    private readonly NativeEngine engine;
    private readonly ObservableCollection<VolumeItem> volumeItems = new();
    private Novel? book;
    private CancellationTokenSource? operation;
    private bool busy;
    private string? lastOutput;
    private string? logPath;
    public MainWindow() : this(null) { }
    internal MainWindow(NativeEngine? supplied, bool restoreSession = true)
    {
        InitializeComponent();
        BookIdInput.Text = "";
        FolderInput.Text = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "菠萝包导出");
        engine = supplied ?? new NativeEngine(FolderInput.Text);
        Volumes.ItemsSource = volumeItems;
        engine.Event += e => Dispatcher.UIThread.Post(() =>
        {
            if (e.Type == "log") Write(e.Text);
            else if (e.Type == "status") StatusText.Text = e.Text;
            else if (e.Type == "progress") { Progress.Maximum = Math.Max(e.Total, 1); Progress.Value = e.Done; StatusText.Text = $"{e.Done}/{e.Total}：{e.Text}"; }
        });
        if (restoreSession) Opened += async (_, _) => await Run(async ct =>
        {
            await engine.CallAsync(new { action = "status" }, ct);
            if (engine.HasSavedSession)
            {
                StatusText.Text = "正在恢复登录会话……";
                await engine.CallAsync(new { action = "restore" }, ct);
                StatusText.Text = "已恢复保存的登录会话";
                Write(StatusText.Text);
            }
        });
        Closing += (_, e) => { if (busy) { e.Cancel = true; operation?.Cancel(); StatusText.Text = "正在停止，请稍候再关闭"; } };
        Closed += (_, _) => { if (supplied is null) engine.Dispose(); };
    }
    private void Write(string text)
    {
        string line = $"[{DateTime.Now:HH:mm:ss}] {text}{Environment.NewLine}";
        LogOutput.Text += line;
        LogOutput.CaretIndex = LogOutput.Text?.Length ?? 0;
        try
        {
            if (logPath is null) { var directory = Path.Combine(AppContext.BaseDirectory, "logs"); Directory.CreateDirectory(directory); logPath = Path.Combine(directory, $"运行日志-{DateTime.Now:yyyyMMdd-HHmmss}-{Environment.ProcessId}.log"); }
            File.AppendAllText(logPath, line, Encoding.UTF8);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException) { }
    }
    private async Task Run(Func<CancellationToken, Task> action)
    {
        if (busy) return;
        busy = true; operation = new();
        CatalogButton.IsEnabled = LoginButton.IsEnabled = LogoutButton.IsEnabled = CookieButton.IsEnabled = ExportButton.IsEnabled = Volumes.IsEnabled = BookIdInput.IsEnabled = FolderInput.IsEnabled = false;
        StopButton.IsEnabled = true;
        try { await action(operation.Token); }
        catch (OperationCanceledException) { StatusText.Text = "任务已停止，已取得的缓存可继续使用"; Write(StatusText.Text); }
        catch (Exception e) { StatusText.Text = e.Message; Write(e.Message); }
        finally
        {
            LoginState.Text = engine.LoggedIn ? "App 已登录" : "未登录";
            Title = "菠萝包小说导出 v0.10.1 · " + LoginState.Text;
            busy = false; operation.Dispose(); operation = null;
            CatalogButton.IsEnabled = LoginButton.IsEnabled = LogoutButton.IsEnabled = CookieButton.IsEnabled = ExportButton.IsEnabled = Volumes.IsEnabled = BookIdInput.IsEnabled = FolderInput.IsEnabled = true;
            StopButton.IsEnabled = false;
        }
    }
    private string Id() => (BookIdInput.Text ?? "").Trim();
    private string Folder() => Path.GetFullPath((FolderInput.Text ?? "").Trim());
    private async Task LoadCatalog(CancellationToken ct, bool refresh)
    {
        var value = await engine.CallAsync(new { action = "catalog", id = Id(), cacheRoot = Folder(), refresh }, ct);
        book = value.Deserialize<Novel>() ?? throw new Exception("目录无效");
        BookIdInput.Text = book.Id;
        volumeItems.Clear();
        foreach (var group in book.Chapters.GroupBy(c => c.Volume)) volumeItems.Add(new(group.Key, group.Count(), group.Count(c => c.Vip)));
        BookDescription.Text = $"《{book.Title}》 · {book.Author} · {book.Chapters.Length} 章";
        StatusText.Text = "请选择卷及格式，然后导出";
    }
    private async void ReadCatalog(object? sender, RoutedEventArgs e) => await Run(ct => LoadCatalog(ct, true));
    private async void Login(object? sender, RoutedEventArgs e) => await Run(async ct =>
    {
        string password = PasswordInput.Text ?? ""; PasswordInput.Text = "";
        await engine.CallAsync(new { action = "login", account = AccountInput.Text?.Trim(), password }, ct);
        StatusText.Text = engine.HasSavedSession ? "登录成功，会话已加密保存" : "登录成功";
        Write(StatusText.Text);
    });
    private async void Logout(object? sender, RoutedEventArgs e) => await Run(async ct =>
    {
        await engine.CallAsync(new { action = "logout" }, ct);
        AccountInput.Text = PasswordInput.Text = ""; StatusText.Text = "已退出登录并清除保存的会话"; Write(StatusText.Text);
    });
    private async void UseCookie(object? sender, RoutedEventArgs e)
    {
        if (busy) return;
        var input = new TextBox { PasswordChar = '●', Watermark = ".SFCommunity=…; session_APP=…" };
        var button = new Button { Content = "验证并保存", HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Right };
        var dialog = new Window { Title = "使用自己的 App 会话", Width = 680, Height = 190, WindowStartupLocation = WindowStartupLocation.CenterOwner, Content = new StackPanel { Margin = new Avalonia.Thickness(20), Spacing = 14, Children = { new TextBlock { Text = "会话只发送给 SF 官方服务，验证后在本机加密保存。" }, input, button } } };
        button.Click += (_, _) => dialog.Close(input.Text);
        var cookie = await dialog.ShowDialog<string?>(this);
        input.Text = "";
        if (string.IsNullOrWhiteSpace(cookie)) return;
        await Run(async ct => { await engine.CallAsync(new { action = "cookie", cookie }, ct); StatusText.Text = "会话验证成功并已保存"; Write(StatusText.Text); });
    }
    private async void Export(object? sender, RoutedEventArgs e) => await Run(async ct =>
    {
        if (book is null || book.Id != Id()) await LoadCatalog(ct, false);
        var result = await engine.CallAsync(new { action = "export", id = Id(), cacheRoot = Folder(), volumes = volumeItems.Where(v => v.Selected).Select(v => v.Name).ToArray(), txt = TxtFormat.IsChecked == true, epub = EpubFormat.IsChecked == true }, ct);
        lastOutput = result.GetProperty("folder").GetString();
        StatusText.Text = $"导出完成：{result.GetProperty("exported").GetInt32()}/{result.GetProperty("requested").GetInt32()} 章";
        Write(StatusText.Text); Write("文件保存在：" + lastOutput);
    });
    private void SelectAll(object? sender, RoutedEventArgs e) { if (!busy) foreach (var v in volumeItems) v.Selected = true; }
    private void SelectNone(object? sender, RoutedEventArgs e) { if (!busy) foreach (var v in volumeItems) v.Selected = false; }
    private void Stop(object? sender, RoutedEventArgs e) => operation?.Cancel();
    private async void ChooseFolder(object? sender, RoutedEventArgs e)
    {
        if (busy) return;
        var folders = await StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions { Title = "选择电子书保存位置", AllowMultiple = false });
        if (folders.Count > 0) FolderInput.Text = folders[0].TryGetLocalPath();
    }
    private void OpenFolder(object? sender, RoutedEventArgs e)
    {
        var path = lastOutput ?? FolderInput.Text;
        if (!string.IsNullOrEmpty(path) && Directory.Exists(path)) Process.Start(new ProcessStartInfo(path) { UseShellExecute = true });
    }
    private void OpenLog(object? sender, RoutedEventArgs e) { if (logPath is not null && File.Exists(logPath)) Process.Start(new ProcessStartInfo(logPath) { UseShellExecute = true }); }
}
