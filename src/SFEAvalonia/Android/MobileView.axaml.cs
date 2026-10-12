using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using SFExporter.Desktop;
using System.Collections.ObjectModel;
using System.Text.Json;

namespace SFExporter.AndroidApp;

public sealed partial class MobileView : UserControl
{
    private readonly NativeEngine engine;
    private readonly string root;
    private readonly ObservableCollection<VolumeItem> volumeItems = new();
    private Novel? book;
    private IStorageFolder? destination;
    private CancellationTokenSource? operation;
    private bool busy, initialized, exporting;

    internal MobileView(NativeEngine engine, string root, bool restoreSession = true)
    {
        InitializeComponent();
        this.engine = engine;
        this.root = root;
        BookIdInput.Text = "";
        Volumes.ItemsSource = volumeItems;
        engine.Event += e => Dispatcher.UIThread.Post(() =>
        {
            if (e.Type == "log") Write(e.Text);
            else if (e.Type == "status") StatusText.Text = e.Text;
            else if (e.Type == "progress") { Progress.Maximum = Math.Max(e.Total, 1); Progress.Value = e.Done; StatusText.Text = $"{e.Done}/{e.Total}：{e.Text}"; }
        });
        Loaded += async (_, _) =>
        {
            if (initialized || !restoreSession) return;
            initialized = true;
            await Run(async ct =>
            {
                var provider = TopLevel.GetTopLevel(this)?.StorageProvider;
                string bookmarkFile = Path.Combine(root, "destination.bookmark");
                if (provider is not null && File.Exists(bookmarkFile))
                {
                    try { destination = await provider.OpenFolderBookmarkAsync(await File.ReadAllTextAsync(bookmarkFile, ct)); }
                    catch { /* Directory permission can be revoked outside the app. */ }
                    FolderDescription.Text = destination?.Name ?? "保存目录授权已失效，请重新选择";
                }
                await engine.CallAsync(new { action = "status" }, ct);
                if (engine.HasSavedSession) { await engine.CallAsync(new { action = "restore" }, ct); StatusText.Text = "已恢复保存的登录会话"; }
            });
        };
    }

    internal void Stop() => operation?.Cancel();
    internal void StopWhenBackgrounded() { if (exporting) Stop(); }
    private void StopClicked(object? sender, RoutedEventArgs e) => Stop();
    private void Write(string text)
    {
        string value = (LogOutput.Text ?? "") + $"[{DateTime.Now:HH:mm:ss}] {text}\n";
        LogOutput.Text = value.Length > 24000 ? value[^24000..] : value;
    }

    private async Task Run(Func<CancellationToken, Task> action)
    {
        if (busy) return;
        busy = true; operation = new();
        LoginControls.IsEnabled = BookControls.IsEnabled = ExportControls.IsEnabled = false;
        StopButton.IsEnabled = true;
        try { await action(operation.Token); }
        catch (OperationCanceledException) { StatusText.Text = "任务已停止，可使用缓存继续导出"; }
        catch (Exception e) { StatusText.Text = e.Message; Write(e.Message); }
        finally
        {
            LoginState.Text = (engine.LoggedIn ? "App 已登录" : "未登录") + " · v0.10.1";
            busy = exporting = false; operation.Dispose(); operation = null;
            LoginControls.IsEnabled = BookControls.IsEnabled = ExportControls.IsEnabled = true;
            StopButton.IsEnabled = false;
        }
    }

    private string Id() => (BookIdInput.Text ?? "").Trim();
    private async Task LoadCatalog(CancellationToken ct, bool refresh)
    {
        var result = await engine.CallAsync(new { action = "catalog", id = Id(), cacheRoot = root, refresh }, ct);
        book = result.Deserialize<Novel>() ?? throw new InvalidOperationException("目录无效");
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
    });
    private async void Logout(object? sender, RoutedEventArgs e) => await Run(async ct =>
    {
        await engine.CallAsync(new { action = "logout" }, ct);
        AccountInput.Text = PasswordInput.Text = CookieInput.Text = "";
        StatusText.Text = "已退出登录并清除保存的会话";
    });
    private async void UseCookie(object? sender, RoutedEventArgs e) => await Run(async ct =>
    {
        string cookie = CookieInput.Text ?? ""; CookieInput.Text = "";
        await engine.CallAsync(new { action = "cookie", cookie }, ct);
        StatusText.Text = "会话验证成功并已保存";
    });
    private void SelectAll(object? sender, RoutedEventArgs e) { if (!busy) foreach (var v in volumeItems) v.Selected = true; }
    private void SelectNone(object? sender, RoutedEventArgs e) { if (!busy) foreach (var v in volumeItems) v.Selected = false; }

    private async void ChooseFolder(object? sender, RoutedEventArgs e)
    {
        if (busy) return;
        await Run(async ct =>
        {
            var provider = TopLevel.GetTopLevel(this)?.StorageProvider ?? throw new InvalidOperationException("系统目录选择器尚未初始化");
            var folders = await provider.OpenFolderPickerAsync(new FolderPickerOpenOptions { Title = "选择电子书保存目录", AllowMultiple = false });
            if (folders.Count == 0) return;
            destination?.Dispose(); destination = folders[0];
            FolderDescription.Text = destination.Name;
            string file = Path.Combine(root, "destination.bookmark");
            string? bookmark = await destination.SaveBookmarkAsync();
            if (bookmark is not null) await File.WriteAllTextAsync(file, bookmark, ct);
            else if (File.Exists(file)) File.Delete(file);
        });
    }

    private async void Export(object? sender, RoutedEventArgs e) => await Run(async ct =>
    {
        if (destination is null) throw new InvalidOperationException("请先选择保存目录");
        exporting = true;
        if (book is null || book.Id != Id()) await LoadCatalog(ct, false);
        string outputFolder = Path.Combine(root, "exports", Guid.NewGuid().ToString("N"));
        var result = await engine.CallAsync(new { action = "export", id = Id(), cacheRoot = root, outputFolder,
            volumes = volumeItems.Where(v => v.Selected).Select(v => v.Name).ToArray(), txt = TxtFormat.IsChecked == true, epub = EpubFormat.IsChecked == true }, ct);
        // Android's content:// URIs are handled by storage streams, not converted to filesystem paths.
        var files = result.GetProperty("files").EnumerateArray().Select(f => f.GetString()!).ToArray();
        foreach (string source in files)
        {
            ct.ThrowIfCancellationRequested();
            using var target = await destination.CreateFileAsync(Path.GetFileName(source)) ?? throw new IOException("无法在所选目录创建文件");
            try
            {
                using var input = File.OpenRead(source);
                using var stream = await target.OpenWriteAsync();
                if (stream.CanSeek) stream.SetLength(0);
                await input.CopyToAsync(stream, ct);
                await stream.FlushAsync(ct);
            }
            catch
            {
                // CreateFileAsync creates a new Android document; remove only that incomplete copy.
                try { await target.DeleteAsync(); } catch { }
                throw;
            }
        }
        foreach (string source in files) File.Delete(source);
        Directory.Delete(outputFolder);
        StatusText.Text = $"导出完成：{result.GetProperty("exported").GetInt32()}/{result.GetProperty("requested").GetInt32()} 章 · {destination.Name}";
    });
}
