using System.Net;
using System.Net.Http.Headers;
using System.Text;

namespace AsaFirmwareTransfer;

internal sealed class UploadClient
{
    private const string UserAgent = "ASA-Firmware-Transfer-CSharp/3.0";
    private readonly TimeSpan connectTimeout;
    private readonly IReadOnlyList<RequestHeader> requestHeaders;

    public UploadClient(
        TimeSpan connectTimeout,
        IReadOnlyList<RequestHeader>? requestHeaders = null)
    {
        this.connectTimeout = connectTimeout;
        this.requestHeaders = requestHeaders ?? [];
    }

    public async Task UploadAsync(
        ResolvedEndpoint endpoint,
        string firmwarePath,
        string compressedMd5,
        TimeSpan timeout,
        IProgress<(long Sent, long Total)> progress,
        CancellationToken cancellationToken)
    {
        using var client = CreateClient();
        using var request = new HttpRequestMessage(HttpMethod.Post, endpoint.Current);
        ApplyHeaders(request, endpoint, requestHeaders);

        using var multipart = new CommitTrackingMultipartFormDataContent();
        multipart.Add(new StringContent(compressedMd5), "MD5");
        multipart.Add(
            new ProgressFileContent(firmwarePath, progress),
            "firmware",
            Path.GetFileName(firmwarePath));
        request.Content = multipart;

        using var timeoutSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeoutSource.CancelAfter(timeout);

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
            var message = timeoutSource.IsCancellationRequested
                ? $"Upload timed out after {timeout.TotalSeconds:0.#}s."
                : $"Upload connection timed out after {connectTimeout.TotalSeconds:0.#}s.";
            throw new UploadNetworkException(
                AppendReplaySafety(message, multipart.BodySerializationStarted),
                exception,
                isSafeToRetry: !multipart.BodySerializationStarted);
        }
        catch (HttpRequestException exception)
        {
            var message = $"Upload connection failed: {exception.Message}";
            throw new UploadNetworkException(
                AppendReplaySafety(message, multipart.BodySerializationStarted),
                exception,
                isSafeToRetry: !multipart.BodySerializationStarted);
        }

        using (response)
        {
            string responseText;
            try
            {
                responseText = await ReadLimitedStringAsync(
                    response.Content,
                    TransferLimits.MaximumResponseBytes,
                    timeoutSource.Token).ConfigureAwait(false);
            }
            catch (ResponseBodyTooLargeException exception)
            {
                throw new UnexpectedUploadResponseException(exception.Message);
            }
            catch (OperationCanceledException exception) when (!cancellationToken.IsCancellationRequested)
            {
                throw new UploadNetworkException(
                    $"Upload response timed out after {timeout.TotalSeconds:0.#}s. " +
                    "The request body was sent; automatic replay was refused.",
                    exception,
                    isSafeToRetry: false);
            }
            catch (Exception exception) when (exception is HttpRequestException or IOException)
            {
                throw new UploadNetworkException(
                    $"Upload response failed: {exception.Message} " +
                    "The request body was sent; automatic replay was refused.",
                    exception,
                    isSafeToRetry: false);
            }

            if (!response.IsSuccessStatusCode)
            {
                throw new UploadServerException(
                    $"Update endpoint returned HTTP {(int)response.StatusCode} {response.ReasonPhrase}: " +
                    Truncate(responseText, 500));
            }

            if (!IsAcceptedResponse(responseText))
            {
                throw new UnexpectedUploadResponseException(
                    $"Update endpoint returned {Quote(responseText)}; exact response \"ok!\" was required.");
            }
        }
    }

    public async Task<DeviceFirmwareInfo> GetFirmwareInfoAsync(
        ResolvedEndpoint endpoint,
        TimeSpan requestTimeout,
        CancellationToken cancellationToken)
    {
        using var client = CreateClient();
        using var timeoutSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeoutSource.CancelAfter(requestTimeout);

        try
        {
            var updateInfo = await RequestInfoAsync(
                client,
                endpoint,
                endpoint.UpdateInfoUri,
                timeoutSource.Token).ConfigureAwait(false);
            if (updateInfo is not null)
            {
                return updateInfo;
            }

            return await RequestInfoAsync(
                client,
                endpoint,
                endpoint.InfoUri,
                timeoutSource.Token,
                requireSuccess: true).ConfigureAwait(false) ??
                throw new HttpRequestException("/info returned no result.");
        }
        catch (OperationCanceledException exception) when (!cancellationToken.IsCancellationRequested)
        {
            var timeout = timeoutSource.IsCancellationRequested ? requestTimeout : connectTimeout;
            throw new HttpRequestException(
                $"Firmware information request timed out after {timeout.TotalSeconds:0.#}s.",
                exception);
        }
    }

    internal static bool IsAcceptedResponse(string body) =>
        string.Equals(body, "ok!", StringComparison.Ordinal);

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

    internal static void ApplyHeaders(
        HttpRequestMessage request,
        ResolvedEndpoint endpoint,
        IReadOnlyList<RequestHeader> requestHeaders)
    {
        request.Headers.UserAgent.ParseAdd(UserAgent);
        request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue("application/json"));
        request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue("text/plain", 0.9));
        if (endpoint.HostHeader is not null)
        {
            request.Headers.Host = endpoint.HostHeader;
        }

        foreach (var header in requestHeaders)
        {
            if (!request.Headers.TryAddWithoutValidation(header.Name, header.Value))
            {
                throw new InvalidOperationException(
                    $"Request header {header.Name} is not supported in this context.");
            }
        }
    }

    internal static string Truncate(string value, int maximum) =>
        value.Length <= maximum ? value : value[..maximum] + "…";

    private async Task<DeviceFirmwareInfo?> RequestInfoAsync(
        HttpClient client,
        ResolvedEndpoint endpoint,
        Uri uri,
        CancellationToken cancellationToken,
        bool requireSuccess = false)
    {
        using var request = new HttpRequestMessage(HttpMethod.Get, uri);
        ApplyHeaders(request, endpoint, requestHeaders);
        using var response = await client.SendAsync(
            request,
            HttpCompletionOption.ResponseHeadersRead,
            cancellationToken).ConfigureAwait(false);
        string body;
        try
        {
            body = await ReadLimitedStringAsync(
                response.Content,
                TransferLimits.MaximumResponseBytes,
                cancellationToken).ConfigureAwait(false);
        }
        catch (ResponseBodyTooLargeException exception)
        {
            throw new HttpRequestException(
                $"{uri.AbsolutePath} response exceeded the safety limit.",
                exception);
        }
        catch (IOException exception)
        {
            throw new HttpRequestException(
                $"{uri.AbsolutePath} response could not be read: {exception.Message}",
                exception);
        }
        if (!response.IsSuccessStatusCode)
        {
            if (!requireSuccess &&
                response.StatusCode is HttpStatusCode.NotFound or
                    HttpStatusCode.MethodNotAllowed or HttpStatusCode.NotImplemented)
            {
                return null;
            }

            throw new HttpRequestException(
                $"{uri.AbsolutePath} returned HTTP {(int)response.StatusCode} " +
                $"{response.ReasonPhrase}: {Truncate(body, 200)}");
        }

        var values = InfoParser.Parse(body);
        InfoParser.TryGetDeviceFirmwareHash(values, out var hash);
        return new DeviceFirmwareInfo(uri.AbsolutePath, body, values, hash.Length == 0 ? null : hash);
    }

    internal static async Task<string> ReadLimitedStringAsync(
        HttpContent content,
        int maximumBytes,
        CancellationToken cancellationToken)
    {
        if (maximumBytes <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(maximumBytes));
        }

        var declaredLength = content.Headers.ContentLength;
        if (declaredLength > maximumBytes)
        {
            throw new ResponseBodyTooLargeException(
                $"HTTP response exceeds the {maximumBytes:N0}-byte safety limit.");
        }

        await using var input = await content.ReadAsStreamAsync(cancellationToken)
            .ConfigureAwait(false);
        using var output = new MemoryStream(
            declaredLength is > 0 && declaredLength <= maximumBytes
                ? (int)declaredLength.Value
                : 0);
        var buffer = new byte[16 * 1024];
        var total = 0;
        while (true)
        {
            var read = await input.ReadAsync(buffer, cancellationToken).ConfigureAwait(false);
            if (read == 0)
            {
                break;
            }

            total += read;
            if (total > maximumBytes)
            {
                throw new ResponseBodyTooLargeException(
                    $"HTTP response exceeds the {maximumBytes:N0}-byte safety limit.");
            }

            await output.WriteAsync(buffer.AsMemory(0, read), cancellationToken)
                .ConfigureAwait(false);
        }

        var value = Encoding.UTF8.GetString(output.GetBuffer(), 0, total);
        return value.Length > 0 && value[0] == '\uFEFF' ? value[1..] : value;
    }

    private static string AppendReplaySafety(string message, bool bodySerializationStarted) =>
        bodySerializationStarted
            ? message + " The request body may have been sent; automatic replay was refused."
            : message;

    private static string Quote(string value)
    {
        var escaped = value
            .Replace("\\", "\\\\", StringComparison.Ordinal)
            .Replace("\r", "\\r", StringComparison.Ordinal)
            .Replace("\n", "\\n", StringComparison.Ordinal)
            .Replace("\"", "\\\"", StringComparison.Ordinal);
        return $"\"{Truncate(escaped, 200)}\"";
    }
}

internal sealed record DeviceFirmwareInfo(
    string EndpointPath,
    string RawBody,
    IReadOnlyDictionary<string, string> Values,
    string? FirmwareHash);

internal sealed class ProgressFileContent : HttpContent
{
    private readonly string path;
    private readonly long length;
    private readonly IProgress<(long Sent, long Total)> progress;

    public ProgressFileContent(string path, IProgress<(long Sent, long Total)> progress)
    {
        this.path = path;
        this.progress = progress;
        length = new FileInfo(path).Length;
        Headers.ContentType = new MediaTypeHeaderValue("application/octet-stream");
    }

    protected override bool TryComputeLength(out long computedLength)
    {
        computedLength = length;
        return true;
    }

    protected override Task SerializeToStreamAsync(Stream stream, TransportContext? context) =>
        CopyAsync(stream, CancellationToken.None);

    protected override Task SerializeToStreamAsync(
        Stream stream,
        TransportContext? context,
        CancellationToken cancellationToken) =>
        CopyAsync(stream, cancellationToken);

    private async Task CopyAsync(Stream destination, CancellationToken cancellationToken)
    {
        await using var source = new FileStream(
            path,
            FileMode.Open,
            FileAccess.Read,
            FileShare.Read,
            bufferSize: 128 * 1024,
            FileOptions.Asynchronous | FileOptions.SequentialScan);
        var buffer = new byte[128 * 1024];
        long sent = 0;
        progress.Report((0, length));

        while (true)
        {
            var read = await source.ReadAsync(buffer, cancellationToken).ConfigureAwait(false);
            if (read == 0)
            {
                break;
            }

            await destination.WriteAsync(buffer.AsMemory(0, read), cancellationToken).ConfigureAwait(false);
            sent += read;
            progress.Report((sent, length));
        }
    }
}

internal sealed class CommitTrackingMultipartFormDataContent : MultipartFormDataContent
{
    public bool BodySerializationStarted { get; private set; }

    protected override Task SerializeToStreamAsync(Stream stream, TransportContext? context)
    {
        BodySerializationStarted = true;
        return base.SerializeToStreamAsync(stream, context);
    }

    protected override Task SerializeToStreamAsync(
        Stream stream,
        TransportContext? context,
        CancellationToken cancellationToken)
    {
        BodySerializationStarted = true;
        return base.SerializeToStreamAsync(stream, context, cancellationToken);
    }
}

internal class UploadNetworkException : Exception
{
    public UploadNetworkException(
        string message,
        Exception innerException,
        bool isSafeToRetry)
        : base(message, innerException)
    {
        IsSafeToRetry = isSafeToRetry;
    }

    public bool IsSafeToRetry { get; }
}

internal sealed class ResponseBodyTooLargeException : Exception
{
    public ResponseBodyTooLargeException(string message)
        : base(message)
    {
    }
}

internal sealed class UploadServerException : Exception
{
    public UploadServerException(string message)
        : base(message)
    {
    }
}

internal sealed class UnexpectedUploadResponseException : Exception
{
    public UnexpectedUploadResponseException(string message)
        : base(message)
    {
    }
}
