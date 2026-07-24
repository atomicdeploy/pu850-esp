using System.IO.Compression;
using System.Security.Cryptography;
using System.Text.RegularExpressions;

namespace AsaFirmwareTransfer;

internal sealed partial class FirmwareManifest
{
    private readonly IReadOnlyList<ManifestEntry> entries;

    private FirmwareManifest(IReadOnlyList<ManifestEntry> entries)
    {
        this.entries = entries;
    }

    public static async Task<FirmwareManifest> LoadAsync(string path, CancellationToken cancellationToken)
    {
        if (!File.Exists(path))
        {
            throw new FirmwareValidationException($"MD5 manifest does not exist: {path}");
        }

        string content;
        try
        {
            content = await File.ReadAllTextAsync(path, cancellationToken).ConfigureAwait(false);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            throw new FirmwareValidationException($"Could not read MD5 manifest: {exception.Message}", exception);
        }

        var normalized = content
            .Replace("\r\n", "\n", StringComparison.Ordinal)
            .Replace('\r', '\n')
            .TrimEnd('\n');
        var lines = normalized.Split('\n');
        if (lines.Length != 2 || lines.Any(line => string.IsNullOrWhiteSpace(line)))
        {
            throw new FirmwareValidationException(
                "MD5 manifest must contain exactly two non-empty md5sum records.");
        }

        var parsed = new List<ManifestEntry>(capacity: 2);
        var known = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        var lineNumber = 0;

        foreach (var rawLine in lines)
        {
            lineNumber++;
            var line = rawLine.Trim();
            var match = Md5LinePattern().Match(line);
            if (!match.Success)
            {
                throw new FirmwareValidationException($"Invalid MD5 manifest line {lineNumber}: {rawLine}");
            }

            var hash = match.Groups["hash"].Value.ToLowerInvariant();
            var name = match.Groups["name"].Value.Trim();
            var compressed = CompressedSuffixPattern().IsMatch(name);
            if (compressed)
            {
                name = CompressedSuffixPattern().Replace(name, string.Empty).Trim();
            }

            name = PortableFileName(name);
            if (name.Length == 0)
            {
                throw new FirmwareValidationException($"MD5 manifest line {lineNumber} has no file name.");
            }

            var key = $"{name}\0{compressed}";
            if (known.ContainsKey(key))
            {
                throw new FirmwareValidationException(
                    $"MD5 manifest contains a duplicate entry for \"{name}\"" +
                    (compressed ? " (compressed)." : "."));
            }

            known.Add(key, hash);
            parsed.Add(new ManifestEntry(hash, name, compressed));
        }

        return new FirmwareManifest(parsed);
    }

    public FirmwareHashes SelectFor(string firmwareFileName)
    {
        var portableName = PortableFileName(firmwareFileName);
        var compressed = entries.Where(
            entry => entry.Compressed &&
                     string.Equals(entry.FileName, portableName, StringComparison.OrdinalIgnoreCase)).ToArray();
        var raw = entries.Where(
            entry => !entry.Compressed &&
                     string.Equals(entry.FileName, portableName, StringComparison.OrdinalIgnoreCase)).ToArray();

        if (compressed.Length == 0)
        {
            throw new FirmwareValidationException(
                $"MD5 manifest does not contain a \"{portableName} (compressed)\" entry.");
        }

        if (compressed.Length > 1)
        {
            throw new FirmwareValidationException(
                $"MD5 manifest contains multiple compressed entries for \"{portableName}\".");
        }

        if (raw.Length == 0)
        {
            throw new FirmwareValidationException(
                $"MD5 manifest does not contain the raw firmware hash for \"{portableName}\".");
        }

        if (raw.Length > 1)
        {
            throw new FirmwareValidationException(
                $"MD5 manifest contains multiple raw entries for \"{portableName}\".");
        }

        return new FirmwareHashes(compressed[0].Md5, raw[0].Md5);
    }

    public static async Task<string> ComputeMd5Async(string path, CancellationToken cancellationToken)
    {
        try
        {
            await using var stream = new FileStream(
                path,
                FileMode.Open,
                FileAccess.Read,
                FileShare.Read,
                bufferSize: 128 * 1024,
                FileOptions.Asynchronous | FileOptions.SequentialScan);
            var hash = await MD5.HashDataAsync(stream, cancellationToken).ConfigureAwait(false);
            return Convert.ToHexString(hash).ToLowerInvariant();
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            throw new FirmwareValidationException($"Could not read firmware: {exception.Message}", exception);
        }
    }

    public static async Task<CalculatedFirmwareHashes> ComputeFirmwareHashesAsync(
        string path,
        CancellationToken cancellationToken)
    {
        FileInfo file;
        try
        {
            file = new FileInfo(path);
            if (!file.Exists)
            {
                throw new FirmwareValidationException($"Firmware file does not exist: {path}");
            }

            if (file.Length is <= 0 or > TransferLimits.MaximumFirmwareBytes)
            {
                throw new FirmwareValidationException(
                    $"Compressed firmware must be between 1 and " +
                    $"{TransferLimits.MaximumFirmwareBytes:N0} bytes.");
            }
        }
        catch (FirmwareValidationException)
        {
            throw;
        }
        catch (Exception exception) when (
            exception is IOException or UnauthorizedAccessException or NotSupportedException)
        {
            throw new FirmwareValidationException(
                $"Could not inspect firmware: {exception.Message}",
                exception);
        }

        var compressedMd5 = await ComputeMd5Async(path, cancellationToken).ConfigureAwait(false);

        try
        {
            await using var compressed = new FileStream(
                path,
                FileMode.Open,
                FileAccess.Read,
                FileShare.Read,
                bufferSize: 128 * 1024,
                FileOptions.Asynchronous | FileOptions.SequentialScan);
            await using var raw = new GZipStream(
                compressed,
                CompressionMode.Decompress,
                leaveOpen: false);
            using var rawHash = IncrementalHash.CreateHash(HashAlgorithmName.MD5);
            var buffer = new byte[128 * 1024];
            long rawSize = 0;
            while (true)
            {
                var read = await raw.ReadAsync(buffer, cancellationToken).ConfigureAwait(false);
                if (read == 0)
                {
                    break;
                }

                rawSize += read;
                if (rawSize > TransferLimits.MaximumFirmwareBytes)
                {
                    throw new FirmwareValidationException(
                        $"Raw firmware exceeds the {TransferLimits.MaximumFirmwareBytes:N0}-byte safety limit.");
                }

                rawHash.AppendData(buffer, 0, read);
            }

            if (rawSize == 0)
            {
                throw new FirmwareValidationException("Raw firmware is empty.");
            }

            return new CalculatedFirmwareHashes(
                compressedMd5,
                Convert.ToHexString(rawHash.GetHashAndReset()).ToLowerInvariant(),
                file.Length,
                rawSize);
        }
        catch (FirmwareValidationException)
        {
            throw;
        }
        catch (InvalidDataException exception)
        {
            throw new FirmwareValidationException(
                $"Firmware is not a valid gzip stream: {exception.Message}",
                exception);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            throw new FirmwareValidationException(
                $"Could not decompress firmware: {exception.Message}",
                exception);
        }
    }

    private static string PortableFileName(string value)
    {
        var normalized = value.Trim().Trim('"').Replace('\\', '/');
        var slash = normalized.LastIndexOf('/');
        return slash >= 0 ? normalized[(slash + 1)..] : normalized;
    }

    [GeneratedRegex(@"^(?<hash>[0-9a-fA-F]{32})\s+\*?(?<name>.+?)\s*$", RegexOptions.CultureInvariant)]
    private static partial Regex Md5LinePattern();

    [GeneratedRegex(@"\s+\(compressed\)\s*$", RegexOptions.IgnoreCase | RegexOptions.CultureInvariant)]
    private static partial Regex CompressedSuffixPattern();
}

internal sealed record ManifestEntry(string Md5, string FileName, bool Compressed);

internal sealed record FirmwareHashes(string CompressedMd5, string RawMd5);

internal sealed record CalculatedFirmwareHashes(
    string CompressedMd5,
    string RawMd5,
    long CompressedSize,
    long RawSize);

internal static class TransferLimits
{
    public const long MaximumFirmwareBytes = 16L * 1024 * 1024;
    public const int MaximumResponseBytes = 1024 * 1024;
}

internal sealed class FirmwareValidationException : Exception
{
    public FirmwareValidationException(string message)
        : base(message)
    {
    }

    public FirmwareValidationException(string message, Exception innerException)
        : base(message, innerException)
    {
    }
}
