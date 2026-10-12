using System.ComponentModel;
namespace SFExporter.Desktop;
public sealed record Chapter(long Id, string Title, string Volume, bool Vip, string WebUrl);
public sealed record Novel(string Id, string Title, string Author, string Cover, Chapter[] Chapters);
public sealed class VolumeItem : INotifyPropertyChanged
{
    public string Name { get; }
    public string Description { get; }
    private bool selected = true;
    public bool Selected { get => selected; set { selected = value; PropertyChanged?.Invoke(this, new(nameof(Selected))); } }
    public event PropertyChangedEventHandler? PropertyChanged;
    public VolumeItem(string name, int count, int vip) { Name = name; Description = $"{count} 章 · VIP {vip} 章"; }
}
