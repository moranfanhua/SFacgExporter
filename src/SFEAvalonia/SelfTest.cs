using System.IO.Compression;
using System.Text;
using System.Text.Json;
using System.Xml.Linq;
namespace SFExporter.Desktop;

internal static class SelfTest
{
    private static void Check(bool ok, string message) { if (!ok) throw new InvalidOperationException(message); }
    public static async Task Run()
    {
        string root = Path.Combine(AppContext.BaseDirectory, "test-artifacts", "interop-" + Guid.NewGuid().ToString("N"));
        using var engine = new NativeEngine(root, Path.Combine(root, "session.dat"));
        await CheckCipherTable(engine, root);
        await engine.CallAsync(new { action = "self_test", root });
        string[] downloaded = Directory.GetFiles(Path.Combine(root, "download-books"));
        Check(downloaded.Length == 2 && downloaded.Any(f => f.EndsWith(".txt")) && downloaded.Any(f => f.EndsWith(".epub")), "Fresh native downloads generate only ebooks");
        using (var fresh = ZipFile.OpenRead(downloaded.Single(f => f.EndsWith(".epub"))))
        {
            Check(fresh.Entries.Count(e => e.FullName.StartsWith("OEBPS/chapter")) == 5, "Fresh free and VIP chapters are all present");
            foreach (var entry in fresh.Entries.Where(e => e.FullName.EndsWith(".xhtml") || e.FullName.EndsWith(".opf") || e.FullName.EndsWith(".xml"))) using (var stream = entry.Open()) XDocument.Load(stream);
        }
        using (var images = ZipFile.OpenRead(Path.Combine(root, "image-books", "images.epub")))
        {
            Check(images.Entries.Count(e => e.FullName.StartsWith("OEBPS/images/")) == 2, "Cover and dimensional illustration are embedded by C++");
            foreach (var entry in images.Entries.Where(e => e.FullName.EndsWith(".xml") || e.FullName.EndsWith(".xhtml") || e.FullName.EndsWith(".opf"))) using (var stream = entry.Open()) XDocument.Load(stream);
            using var pageStream = images.GetEntry("OEBPS/chapter00000.xhtml")!.Open();
            var page = XDocument.Load(pageStream);
            XNamespace x = "http://www.w3.org/1999/xhtml";
            Check(page.Descendants(x + "img").Count() == 1 && !page.ToString().Contains("700,875"), "The image tag becomes an EPUB image, not dimensions or a stray URL");
            foreach (var entry in images.Entries.Where(e => e.FullName.StartsWith("OEBPS/images/")))
            { using var source = entry.Open(); using var buffer = new MemoryStream(); source.CopyTo(buffer); Check(buffer.ToArray().Length == 43 && Encoding.ASCII.GetString(buffer.ToArray()[..6]) == "GIF89a", "Complete image bytes are preserved"); }
        }
        var signature = await engine.CallAsync(new { action = "sign", nonce = "8BB4D95F-7BE3-40D4-BC98-E4DF0B5D4568", timestamp = 1791777000123L, device = "910D166A-736E-3231-8B21-8D12DFD75F16" });
        Check(signature.GetString() == "C60655DD55198E0A9F176799E3C8206A", "C++ request signature must match the protocol vector");
        string raw = "一", clear = (await engine.CallAsync(new { action = "decode", text = raw })).GetString()!;
        var chapters = Enumerable.Range(1, 5).Select(i => new Chapter(i, "原创章节 " + i, "第一卷", i > 3, $"https://book.sfacg.com/Novel/0/1/{i}/")).ToArray();
        var book = new Novel("0", "原生导出测试 & 😀", "测试程序", "", chapters);
        string cache = Path.Combine(root, ".sfcore-cache", "0");
        Directory.CreateDirectory(Path.Combine(cache, "raw"));
        await File.WriteAllTextAsync(Path.Combine(cache, "novel.json"), JsonSerializer.Serialize(book));
        foreach (var chapter in chapters)
        {
            string text = clear + (chapter.Id == 4 ? "岚曜砥😀𠮷" : "");
            await File.WriteAllTextAsync(Path.Combine(cache, chapter.Id + ".json"), JsonSerializer.Serialize(new { BookId = "0", chapter.Id, DecoderVersion = 2, Content = text }));
            if (!chapter.Vip) await File.WriteAllTextAsync(Path.Combine(cache, "raw", chapter.Id + ".json"), JsonSerializer.Serialize(new { BookId = "0", chapter.Id, ProtocolVersion = 2, Content = raw }));
        }
        int progressEvents = 0;
        var logs = new List<string>();
        engine.Event += e => { if (e.Type == "progress") progressEvents++; if (e.Type == "log") logs.Add(e.Text); };
        GC.Collect(); GC.WaitForPendingFinalizers();
        var result = await engine.CallAsync(new { action = "export", id = "0", cacheRoot = root, volumes = new[] { "第一卷" }, txt = true, epub = true });
        Check(result.GetProperty("exported").GetInt32() == 5 && result.GetProperty("failed").GetInt32() == 0, "All chapters must be exported by the native engine");
        Check(progressEvents == 10 && logs.Count == 0, "Native callbacks survive GC, and successful downloads stay silent");
        string folder = result.GetProperty("folder").GetString()!;
        string[] files = Directory.GetFiles(folder);
        Check(files.Length == 2 && files.Any(p => p.EndsWith(".txt")) && files.Any(p => p.EndsWith(".epub")), "Only ebook files may be generated");
        var txt = File.ReadAllText(files.Single(p => p.EndsWith(".txt")));
        Check(txt.Contains("岚曜砥😀𠮷") && chapters.All(c => txt.Contains(c.Title)), "Unicode and all chapter titles survive native TXT generation");
        using (var zip = ZipFile.OpenRead(files.Single(p => p.EndsWith(".epub"))))
        {
            Check(zip.Entries[0].FullName == "mimetype" && zip.Entries[0].CompressedLength == zip.Entries[0].Length, "EPUB mimetype is first and stored");
            Check(zip.Entries.Count(e => e.FullName.StartsWith("OEBPS/chapter")) == 5, "Every chapter is included in the EPUB");
            foreach (var entry in zip.Entries.Where(e => e.FullName.EndsWith(".xhtml") || e.FullName.EndsWith(".opf") || e.FullName.EndsWith(".xml"))) using (var stream = entry.Open()) XDocument.Load(stream);
        }
        bool rejected = false;
        try { await engine.CallAsync(new { action = "catalog", id = "" }); } catch (InvalidOperationException) { rejected = true; }
        Check(rejected, "An empty ID must be rejected only when submitting, not prefilled");
        Console.WriteLine("Native core and C# interop tests passed.");
    }

    private static async Task CheckCipherTable(NativeEngine engine, string root)
    {
        string tablePath = Path.Combine(AppContext.BaseDirectory, "resource", "cipher-table.json");
        var table = JsonSerializer.Deserialize<Dictionary<string, string>>(await File.ReadAllTextAsync(tablePath))!;
        Check(table.Count == 3751 && table.Keys.ToHashSet().SetEquals(table.Values), "The distributed JSON contains the complete permutation");
        string encoded = string.Concat(table.Keys), expected = string.Concat(table.Values);
        var decoded = await engine.CallAsync(new { action = "decode", text = encoded });
        Check(decoded.GetString() == expected, "Every distributed mapping is loaded and decoded by C++");

        string fixtures = Path.Combine(root, "cipher-fixtures");
        Directory.CreateDirectory(fixtures);
        void Reject(string path)
        {
            bool rejected = false;
            try { using var invalid = new NativeEngine(root, Path.Combine(root, "unused-session.dat"), path); }
            catch (InvalidOperationException e) { rejected = e.Message.Contains("字符表"); }
            Check(rejected, "Missing or invalid JSON must produce a clear startup error: " + Path.GetFileName(path));
        }
        Reject(Path.Combine(fixtures, "missing.json"));
        string malformed = Path.Combine(fixtures, "malformed.json");
        await File.WriteAllTextAsync(malformed, "{\"一\":");
        Reject(malformed);
        string incomplete = Path.Combine(fixtures, "incomplete.json");
        await File.WriteAllTextAsync(incomplete, "{\"一\":\"一\"}");
        Reject(incomplete);

        var changed = new Dictionary<string, string>(table);
        string[] keys = changed.Keys.Take(2).ToArray();
        changed[keys[0]] = changed[keys[1]];
        string duplicate = Path.Combine(fixtures, "duplicate-values.json");
        await File.WriteAllTextAsync(duplicate, JsonSerializer.Serialize(changed));
        Reject(duplicate);

        // A valid alternate permutation verifies that the file, rather than an embedded array, is used.
        changed[keys[1]] = table[keys[0]];
        string alternate = Path.Combine(fixtures, "alternate.json");
        await File.WriteAllTextAsync(alternate, JsonSerializer.Serialize(changed));
        using var custom = new NativeEngine(root, Path.Combine(root, "unused-session.dat"), alternate);
        var customDecoded = await custom.CallAsync(new { action = "decode", text = string.Concat(keys) });
        Check(customDecoded.GetString() == changed[keys[0]] + changed[keys[1]], "The decoder uses the configured external JSON file");
    }
}
