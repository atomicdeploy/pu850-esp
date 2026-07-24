namespace AsaFirmwareTransfer;

internal static class DownloadApp
{
    public static async Task<ExitCode> RunAsync(
        UploaderOptions options,
        ConsoleUi console,
        CancellationToken cancellationToken)
    {
        var destination = options.DownloadPath;
        var partialPath = destination + ".part";
        try
        {
            var directory = Path.GetDirectoryName(destination);
            if (!string.IsNullOrEmpty(directory))
            {
                Directory.CreateDirectory(directory);
            }
        }
        catch (Exception exception) when (
            exception is IOException or UnauthorizedAccessException or NotSupportedException)
        {
            console.Error($"Could not prepare the destination directory: {exception.Message}");
            return ExitCode.LocalFile;
        }

        console.Heading("ASA firmware download");
        console.Detail("Download API", options.EffectiveDownloadApi);
        console.Detail("Destination", destination);
        console.Detail("Partial file", partialPath);
        console.Detail("Resume", options.Resume ? "strict single-range" : "disabled");
        console.Detail("Remote verification", options.Verify ? "required" : "disabled by request");

        var resolver = new EndpointResolver(options.EffectiveDownloadApi, options.ConnectTimeout);
        ResolvedEndpoint endpoint;
        try
        {
            endpoint = await resolver.ResolveAsync(console, forceRefresh: false, cancellationToken)
                .ConfigureAwait(false);
        }
        catch (HttpRequestException exception)
        {
            console.Error(exception.Message);
            return ExitCode.NetworkOrUpload;
        }

        try
        {
            var result = await DownloadOnceAsync(
                options,
                console,
                endpoint,
                partialPath,
                cancellationToken).ConfigureAwait(false);
            return Finish(console, destination, partialPath, result);
        }
        catch (Exception firstFailure) when (
            resolver.UsesLocalDns &&
            firstFailure is DownloadNetworkException or HttpRequestException)
        {
            console.ClearProgress();
            console.Warning(firstFailure.Message);
            console.Info("Cached .local address failed; resolving once more and retrying.");
            try
            {
                endpoint = await resolver.ResolveAsync(console, forceRefresh: true, cancellationToken)
                    .ConfigureAwait(false);
                var result = await DownloadOnceAsync(
                    options,
                    console,
                    endpoint,
                    partialPath,
                    cancellationToken).ConfigureAwait(false);
                return Finish(console, destination, partialPath, result);
            }
            catch (Exception retryFailure)
            {
                return ReportFailure(console, retryFailure);
            }
        }
        catch (Exception exception)
        {
            return ReportFailure(console, exception);
        }
    }

    internal static async Task<DownloadedFile> DownloadOnceAsync(
        UploaderOptions options,
        ConsoleUi console,
        ResolvedEndpoint endpoint,
        string partialPath,
        CancellationToken cancellationToken)
    {
        var downloadClient = new DownloadClient(options.ConnectTimeout, options.RequestHeaders);
        var infoClient = new UploadClient(options.ConnectTimeout, options.RequestHeaders);

        DeviceFirmwareInfo? deviceInfo = null;
        if (options.Verify)
        {
            deviceInfo = await infoClient.GetFirmwareInfoAsync(
                endpoint,
                options.ConnectTimeout,
                cancellationToken).ConfigureAwait(false);
            if (deviceInfo.FirmwareHash is null)
            {
                throw new DownloadIntegrityException(
                    $"{deviceInfo.EndpointPath} did not provide a valid running firmware hash.");
            }

            console.Detail("Running firmware MD5", deviceInfo.FirmwareHash);
            console.Verbose($"Hash source: {deviceInfo.EndpointPath}", options.Verbose);
        }

        var metadata = await downloadClient.GetMetadataAsync(
            endpoint,
            options.ConnectTimeout,
            cancellationToken).ConfigureAwait(false);
        console.Detail("Remote size", $"{metadata.Length:N0} bytes");
        console.Detail("Byte ranges", metadata.AcceptsRanges ? "supported" : "not advertised");
        if (metadata.ExpectedMd5 is not null)
        {
            console.Detail("Header MD5", metadata.ExpectedMd5);
        }

        DownloadClient.EnsureHashesAgree(
            metadata.ExpectedMd5,
            deviceInfo?.FirmwareHash,
            "firmware response",
            deviceInfo?.EndpointPath ?? "device info");

        var progress = new SynchronousProgress<(long Received, long Total)>(
            value => console.DownloadProgress(value.Received, value.Total));
        var result = await downloadClient.DownloadAsync(
            endpoint,
            metadata,
            partialPath,
            options.Resume,
            options.DownloadTimeout,
            progress,
            cancellationToken).ConfigureAwait(false);
        console.ClearProgress();

        DownloadClient.EnsureHashesAgree(
            result.Md5,
            deviceInfo?.FirmwareHash,
            "downloaded firmware",
            deviceInfo?.EndpointPath ?? "device info");
        if (metadata.ExpectedMd5 is null && deviceInfo?.FirmwareHash is null)
        {
            console.Warning(
                "The device supplied no firmware hash; only length and transport completion were checked.");
        }

        return result;
    }

    internal static ExitCode Finish(
        ConsoleUi console,
        string destination,
        string partialPath,
        DownloadedFile result)
    {
        try
        {
            File.Move(partialPath, destination, overwrite: true);
        }
        catch (Exception exception) when (
            exception is IOException or UnauthorizedAccessException or NotSupportedException)
        {
            console.Error($"Downloaded data is valid, but final atomic replacement failed: {exception.Message}");
            console.Detail("Verified partial file", partialPath);
            return ExitCode.LocalFile;
        }

        console.Detail("Downloaded bytes", $"{result.Length:N0}");
        console.Detail("Downloaded MD5", result.Md5);
        console.Success(
            result.Resumed
                ? "Firmware download resumed, verified, and atomically installed."
                : "Firmware download verified and atomically installed.");
        return ExitCode.Success;
    }

    internal static ExitCode ReportFailure(ConsoleUi console, Exception exception)
    {
        console.ClearProgress();
        switch (exception)
        {
            case DownloadIntegrityException:
                console.Error(exception.Message);
                console.Info("The .part file was retained for inspection or a strict resume.");
                return ExitCode.DownloadIntegrity;
            case DownloadLocalFileException:
                console.Error(exception.Message);
                return ExitCode.LocalFile;
            case DownloadNetworkException:
            case HttpRequestException:
                console.Error(exception.Message);
                return ExitCode.NetworkOrUpload;
            case OperationCanceledException:
                System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(exception).Throw();
                return ExitCode.Unexpected;
            default:
                console.Error($"Unexpected download failure: {exception.Message}");
                return ExitCode.Unexpected;
        }
    }
}
