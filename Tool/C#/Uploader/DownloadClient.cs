using System.Net;
using System.Net.Http.Headers;
using System.Security.Cryptography;

namespace AsaFirmwareTransfer;

internal sealed class DownloadClient
{
    private readonly TimeSpan connectTimeout;
    private readonly IReadOnlyList<RequestHeader> requestHeaders;

    public DownloadClient(
        TimeSpan connectTimeout,
        IReadOnlyList<RequestHeader>? requestHeaders = null)
    {
        this.connectTimeout = connectTimeout;
        this.requestHeaders = requestHeaders ?? [];
    }

    public async Task<DownloadMetadata> GetMetadataAsync(
        ResolvedEndpoint endpoint,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        using var client = CreateClient();
        using var request = new HttpRequestMessage(HttpMethod.Head, endpoint.Current);
        UploadClient.ApplyHeaders(request, endpoint, requestHeaders);
        using var timeoutSource = CreateTimeout(timeout, cancellationToken);

        try
        {
            using var response = await client.SendAsync(
                request,
                HttpCompletionOption.ResponseHeadersRead,
                timeoutSource.Token).ConfigureAwait(false);
            if (!response.IsSuccessStatusCode)
            {
                throw new DownloadNetworkException(
                    $"Firmware HEAD returned HTTP {(int)response.StatusCode} {response.ReasonPhrase}.");
            }

            var length = response.Content.Headers.ContentLength;
            if (length is null or <= 0)
            {
                throw new DownloadIntegrityException(
                    "Firmware HEAD did not provide a positive Content-Length.");
            }
            if (length > TransferLimits.MaximumFirmwareBytes)
            {
                throw new DownloadIntegrityException(
                    $"Firmware download exceeds the " +
                    $"{TransferLimits.MaximumFirmwareBytes:N0}-byte safety limit.");
            }

            return new DownloadMetadata(
                length.Value,
                ExtractExpectedMd5(response),
                response.Headers.AcceptRanges.Contains("bytes", StringComparer.OrdinalIgnoreCase));
        }
        catch (OperationCanceledException exception) when (!cancellationToken.IsCancellationRequested)
        {
            throw new DownloadNetworkException(
                $"Firmware HEAD timed out after {timeout.TotalSeconds:0.#}s.",
                exception);
        }
        catch (HttpRequestException exception)
        {
            throw new DownloadNetworkException(
                $"Firmware HEAD failed: {exception.Message}",
                exception);
        }
    }

    public async Task<DownloadedFile> DownloadAsync(
        ResolvedEndpoint endpoint,
        DownloadMetadata metadata,
        string partialPath,
        bool resume,
        TimeSpan timeout,
        IProgress<(long Received, long Total)> progress,
        CancellationToken cancellationToken)
    {
        var existing = resume && File.Exists(partialPath)
            ? new FileInfo(partialPath).Length
            : 0;
        if (existing > metadata.Length)
        {
            throw new DownloadIntegrityException(
                $"Partial file is larger than the advertised firmware ({existing} > {metadata.Length}).");
        }

        if (existing == metadata.Length && existing > 0)
        {
            var completedHash = await ComputeMd5Async(partialPath, cancellationToken).ConfigureAwait(false);
            ValidateExpectedHash(metadata.ExpectedMd5, completedHash, "completed partial file");
            progress.Report((metadata.Length, metadata.Length));
            return new DownloadedFile(metadata.Length, completedHash, Resumed: true);
        }

        if (existing > 0 && !metadata.AcceptsRanges)
        {
            throw new DownloadIntegrityException(
                "The device did not advertise byte-range support; strict resume was refused.");
        }

        using var client = CreateClient();
        using var request = new HttpRequestMessage(HttpMethod.Get, endpoint.Current);
        UploadClient.ApplyHeaders(request, endpoint, requestHeaders);
        if (existing > 0)
        {
            request.Headers.Range = new RangeHeaderValue(existing, null);
        }

        using var timeoutSource = CreateTimeout(timeout, cancellationToken);
        HttpResponseMessage response;
        try
        {
            response = await client.SendAsync(
                request,
                HttpCompletionOption.ResponseHeadersRead,
                timeoutSource.Token).ConfigureAwait(false);
        }
        catch (OperationCanceledException exception) when (!cancellationToken.IsCancellationRequested)
        {
            throw new DownloadNetworkException(
                $"Firmware download timed out after {timeout.TotalSeconds:0.#}s.",
                exception);
        }
        catch (HttpRequestException exception)
        {
            throw new DownloadNetworkException(
                $"Firmware download failed: {exception.Message}",
                exception);
        }

        using (response)
        {
            ValidateDownloadResponse(response, metadata, existing);
            var responseHash = ExtractExpectedMd5(response);
            EnsureHashesAgree(metadata.ExpectedMd5, responseHash, "HEAD", "GET");

            try
            {
                await using var input = await response.Content.ReadAsStreamAsync(timeoutSource.Token)
                    .ConfigureAwait(false);
                await using var output = new FileStream(
                    partialPath,
                    existing > 0 ? FileMode.Open : FileMode.Create,
                    FileAccess.Write,
                    FileShare.None,
                    bufferSize: 128 * 1024,
                    FileOptions.Asynchronous | FileOptions.SequentialScan);
                if (existing > 0)
                {
                    output.Position = existing;
                }

                var buffer = new byte[128 * 1024];
                var received = existing;
                progress.Report((received, metadata.Length));
                while (true)
                {
                    var read = await input.ReadAsync(buffer, timeoutSource.Token).ConfigureAwait(false);
                    if (read == 0)
                    {
                        break;
                    }

                    if (received + read > metadata.Length)
                    {
                        throw new DownloadIntegrityException(
                            "The device sent more bytes than its advertised firmware length.");
                    }

                    await output.WriteAsync(buffer.AsMemory(0, read), timeoutSource.Token)
                        .ConfigureAwait(false);
                    received += read;
                    progress.Report((received, metadata.Length));
                }

                await output.FlushAsync(timeoutSource.Token).ConfigureAwait(false);
            }
            catch (OperationCanceledException exception) when (!cancellationToken.IsCancellationRequested)
            {
                throw new DownloadNetworkException(
                    $"Firmware download timed out after {timeout.TotalSeconds:0.#}s.",
                    exception);
            }
            catch (HttpRequestException exception)
            {
                throw new DownloadNetworkException(
                    $"Firmware download stream failed: {exception.Message}",
                    exception);
            }
            catch (IOException exception)
            {
                throw new DownloadLocalFileException(
                    $"Could not write the partial firmware file: {exception.Message}",
                    exception);
            }
        }

        var actualLength = new FileInfo(partialPath).Length;
        if (actualLength != metadata.Length)
        {
            throw new DownloadIntegrityException(
                $"Downloaded length mismatch: expected {metadata.Length}, received {actualLength}.");
        }

        var md5 = await ComputeMd5Async(partialPath, cancellationToken).ConfigureAwait(false);
        ValidateExpectedHash(metadata.ExpectedMd5, md5, "download");
        return new DownloadedFile(actualLength, md5, existing > 0);
    }

    internal static string? ExtractExpectedMd5(HttpResponseMessage response)
    {
        var candidates = new List<(string Source, string Hash)>();
        if (response.Content.Headers.TryGetValues("Content-MD5", out var contentMd5Values))
        {
            foreach (var value in contentMd5Values)
            {
                byte[] digest;
                try
                {
                    digest = Convert.FromBase64String(value.Trim());
                }
                catch (FormatException exception)
                {
                    throw new DownloadIntegrityException(
                        $"Content-MD5 was not valid Base64: {exception.Message}");
                }

                if (digest.Length != 16)
                {
                    throw new DownloadIntegrityException(
                        "Content-MD5 did not decode to a 16-byte MD5 digest.");
                }

                candidates.Add(("Content-MD5", Convert.ToHexString(digest).ToLowerInvariant()));
            }
        }

        AddAsciiMd5Header(response, "X-Firmware-MD5", candidates);
        if (response.Headers.ETag is { } etag)
        {
            var tag = etag.Tag.Trim('"');
            if (IsMd5(tag))
            {
                candidates.Add(("ETag", tag.ToLowerInvariant()));
            }
        }

        if (candidates.Count == 0)
        {
            return null;
        }

        var expected = candidates[0];
        foreach (var candidate in candidates.Skip(1))
        {
            if (!candidate.Hash.Equals(expected.Hash, StringComparison.OrdinalIgnoreCase))
            {
                throw new DownloadIntegrityException(
                    $"Conflicting integrity headers: {expected.Source} and {candidate.Source} disagree.");
            }
        }

        return expected.Hash;
    }

    internal static void EnsureHashesAgree(
        string? first,
        string? second,
        string firstSource,
        string secondSource)
    {
        if (first is not null && second is not null &&
            !first.Equals(second, StringComparison.OrdinalIgnoreCase))
        {
            throw new DownloadIntegrityException(
                $"{firstSource} hash {first} does not match {secondSource} hash {second}.");
        }
    }

    private static void ValidateDownloadResponse(
        HttpResponseMessage response,
        DownloadMetadata metadata,
        long existing)
    {
        if (existing > 0)
        {
            if (response.StatusCode != HttpStatusCode.PartialContent)
            {
                throw new DownloadIntegrityException(
                    $"Resume required HTTP 206, but the device returned {(int)response.StatusCode}.");
            }

            var range = response.Content.Headers.ContentRange;
            if (range?.From != existing ||
                range.To != metadata.Length - 1 ||
                range.Length != metadata.Length)
            {
                throw new DownloadIntegrityException(
                    "Resume response contained an invalid or incomplete Content-Range.");
            }
        }
        else if (response.StatusCode != HttpStatusCode.OK)
        {
            throw new DownloadNetworkException(
                $"Firmware GET returned HTTP {(int)response.StatusCode} {response.ReasonPhrase}.");
        }

        var expectedBodyLength = metadata.Length - existing;
        if (response.Content.Headers.ContentLength != expectedBodyLength)
        {
            throw new DownloadIntegrityException(
                $"GET Content-Length mismatch: expected {expectedBodyLength}, " +
                $"received {response.Content.Headers.ContentLength?.ToString() ?? "none"}.");
        }
    }

    private static void AddAsciiMd5Header(
        HttpResponseMessage response,
        string name,
        ICollection<(string Source, string Hash)> candidates)
    {
        if (!response.Headers.TryGetValues(name, out var values))
        {
            return;
        }

        foreach (var value in values)
        {
            var candidate = value.Trim().Trim('"');
            if (!IsMd5(candidate))
            {
                throw new DownloadIntegrityException($"{name} was not a valid 32-digit MD5 hash.");
            }

            candidates.Add((name, candidate.ToLowerInvariant()));
        }
    }

    private static void ValidateExpectedHash(string? expected, string actual, string subject)
    {
        if (expected is not null && !expected.Equals(actual, StringComparison.OrdinalIgnoreCase))
        {
            throw new DownloadIntegrityException(
                $"MD5 mismatch for {subject}: expected {expected}, calculated {actual}.");
        }
    }

    private static bool IsMd5(string value) =>
        value.Length == 32 && value.All(character =>
            character is >= '0' and <= '9' or >= 'a' and <= 'f' or >= 'A' and <= 'F');

    private static async Task<string> ComputeMd5Async(
        string path,
        CancellationToken cancellationToken)
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
            var digest = await MD5.HashDataAsync(stream, cancellationToken).ConfigureAwait(false);
            return Convert.ToHexString(digest).ToLowerInvariant();
        }
        catch (IOException exception)
        {
            throw new DownloadLocalFileException(
                $"Could not read the partial firmware file: {exception.Message}",
                exception);
        }
        catch (UnauthorizedAccessException exception)
        {
            throw new DownloadLocalFileException(
                $"Could not read the partial firmware file: {exception.Message}",
                exception);
        }
    }

    private HttpClient CreateClient()
    {
        var handler = new SocketsHttpHandler
        {
            ConnectTimeout = connectTimeout,
            Expect100ContinueTimeout = TimeSpan.Zero,
            PooledConnectionLifetime = TimeSpan.FromSeconds(2),
            UseProxy = false,
            AllowAutoRedirect = false,
        };
        return new HttpClient(handler, disposeHandler: true)
        {
            Timeout = Timeout.InfiniteTimeSpan,
        };
    }

    private static CancellationTokenSource CreateTimeout(
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var source = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        source.CancelAfter(timeout);
        return source;
    }
}

internal sealed record DownloadMetadata(long Length, string? ExpectedMd5, bool AcceptsRanges);

internal sealed record DownloadedFile(long Length, string Md5, bool Resumed);

internal class DownloadNetworkException : Exception
{
    public DownloadNetworkException(string message)
        : base(message)
    {
    }

    public DownloadNetworkException(string message, Exception innerException)
        : base(message, innerException)
    {
    }
}

internal sealed class DownloadIntegrityException : Exception
{
    public DownloadIntegrityException(string message)
        : base(message)
    {
    }
}

internal sealed class DownloadLocalFileException : Exception
{
    public DownloadLocalFileException(string message, Exception innerException)
        : base(message, innerException)
    {
    }
}
