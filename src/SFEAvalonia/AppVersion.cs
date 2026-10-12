using System.Reflection;

namespace SFExporter.Desktop;

internal static class AppVersion
{
    internal static string Current { get; } =
        (typeof(AppVersion).Assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion
         ?? typeof(AppVersion).Assembly.GetName().Version?.ToString()
         ?? "unknown").Split('+')[0];
}
