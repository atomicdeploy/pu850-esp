using System.Text;

namespace AsaFirmwareTransfer;

internal sealed class ConsoleUi
{
    private readonly TextWriter output;
    private readonly TextWriter error;
    private readonly bool stdoutColor;
    private readonly bool stderrColor;
    private readonly bool stdoutRedirected;
    private readonly object sync = new();
    private int lastProgressBucket = -1;
    private bool progressVisible;

    public ConsoleUi(bool noColor)
        : this(
            noColor,
            Console.Out,
            Console.Error,
            Console.IsOutputRedirected,
            Console.IsErrorRedirected,
            EnvironmentAllowsColor())
    {
    }

    internal ConsoleUi(
        bool noColor,
        TextWriter output,
        TextWriter error,
        bool stdoutRedirected,
        bool stderrRedirected,
        bool environmentAllowsColor)
    {
        this.output = output;
        this.error = error;
        this.stdoutRedirected = stdoutRedirected;
        stdoutColor = !noColor && !stdoutRedirected && environmentAllowsColor;
        stderrColor = !noColor && !stderrRedirected && environmentAllowsColor;
    }

    public void Heading(string message) => WriteLine($"⚡ {message}", "1;36");
    public void Info(string message) => WriteLine($"ℹ  {message}", "36");
    public void Success(string message) => WriteLine($"✓ {message}", "1;32");
    public void Warning(string message) => WriteLine($"⚠  {message}", "1;33");
    public void Error(string message) => WriteError($"✗ {message}", "1;31");
    public void Detail(string label, object? value) => WriteLine($"  {label}: {value}", "90");

    public void UploadProgress(long transferred, long total) =>
        TransferProgress("Upload", "Uploading", transferred, total);

    public void DownloadProgress(long transferred, long total) =>
        TransferProgress("Download", "Downloading", transferred, total);

    private void TransferProgress(
        string redirectedLabel,
        string terminalLabel,
        long transferred,
        long total)
    {
        lock (sync)
        {
            var percent = total <= 0 ? 0 : Math.Clamp((int)Math.Round(transferred * 100d / total), 0, 100);
            if (!stdoutColor || stdoutRedirected)
            {
                var bucket = percent / 10;
                if (bucket == lastProgressBucket && percent != 100)
                {
                    return;
                }

                lastProgressBucket = bucket;
                output.WriteLine(
                    $"{redirectedLabel} progress: {percent}% " +
                    $"({FormatBytes(transferred)} / {FormatBytes(total)})");
                return;
            }

            var width = 30;
            var filled = percent * width / 100;
            var bar = new string('━', filled) + new string('─', width - filled);
            output.Write(
                $"\r\u001b[2K\u001b[36m{terminalLabel} [{bar}] " +
                $"\u001b[1m{percent,3}%\u001b[0m");
            progressVisible = true;
        }
    }

    public void PollProgress(TimeSpan elapsed, TimeSpan deadline)
    {
        lock (sync)
        {
            if (!stdoutColor || stdoutRedirected)
            {
                return;
            }

            var spinner = new[] { "⡿", "⣟", "⣯", "⣷", "⣾", "⣽", "⣻", "⢿" };
            var frame = spinner[(int)(elapsed.TotalMilliseconds / 125) % spinner.Length];
            output.Write(
                $"\r\u001b[2K\u001b[33m{frame} Waiting for /info " +
                $"({elapsed.TotalSeconds:0}/{deadline.TotalSeconds:0}s)\u001b[0m");
            progressVisible = true;
        }
    }

    public void ClearProgress()
    {
        lock (sync)
        {
            if (!progressVisible)
            {
                return;
            }

            output.Write(stdoutColor ? "\r\u001b[2K" : Environment.NewLine);
            progressVisible = false;
        }
    }

    public void Verbose(string message, bool enabled)
    {
        if (enabled)
        {
            WriteLine($"  ↳ {message}", "90");
        }
    }

    private void WriteLine(string text, string style)
    {
        lock (sync)
        {
            ClearProgress();
            var safeText = Sanitize(text);
            output.WriteLine(stdoutColor ? $"\u001b[{style}m{safeText}\u001b[0m" : safeText);
        }
    }

    private void WriteError(string text, string style)
    {
        lock (sync)
        {
            ClearProgress();
            var safeText = Sanitize(text);
            error.WriteLine(stderrColor ? $"\u001b[{style}m{safeText}\u001b[0m" : safeText);
        }
    }

    internal static string Sanitize(string? value)
    {
        if (string.IsNullOrEmpty(value))
        {
            return value ?? string.Empty;
        }

        StringBuilder? builder = null;
        for (var index = 0; index < value.Length; index++)
        {
            var character = value[index];
            if (!char.IsControl(character))
            {
                builder?.Append(character);
                continue;
            }

            builder ??= new StringBuilder(value.Length + 16).Append(value, 0, index);
            builder.Append(character switch
            {
                '\r' => "\\r",
                '\n' => "\\n",
                '\t' => "\\t",
                _ when character <= '\xFF' => $"\\x{(int)character:X2}",
                _ => $"\\u{(int)character:X4}",
            });
        }

        return builder?.ToString() ?? value;
    }

    private static string FormatBytes(long bytes)
    {
        string[] units = ["B", "KiB", "MiB", "GiB"];
        var value = (double)Math.Max(bytes, 0);
        var index = 0;
        while (value >= 1024 && index < units.Length - 1)
        {
            value /= 1024;
            index++;
        }

        return index == 0 ? $"{value:0} {units[index]}" : $"{value:0.00} {units[index]}";
    }

    private static bool EnvironmentAllowsColor() =>
        string.IsNullOrEmpty(Environment.GetEnvironmentVariable("NO_COLOR")) &&
        !string.Equals(Environment.GetEnvironmentVariable("TERM"), "dumb", StringComparison.OrdinalIgnoreCase);
}
