using System.Diagnostics;

namespace AsaFirmwareTransfer;

internal static class UploaderApp
{
    public static async Task<ExitCode> RunAsync(
        UploaderOptions options,
        ConsoleUi console,
        CancellationToken cancellationToken,
        UploaderRuntime? runtime = null)
    {
        if (!File.Exists(options.FirmwarePath))
        {
            console.Error($"Firmware file does not exist: {options.FirmwarePath}");
            return ExitCode.FirmwareValidation;
        }

        FirmwareHashes hashes;
        CalculatedFirmwareHashes calculated;
        try
        {
            var manifest = await FirmwareManifest.LoadAsync(options.ManifestPath, cancellationToken)
                .ConfigureAwait(false);
            hashes = manifest.SelectFor(Path.GetFileName(options.FirmwarePath));
            calculated = await FirmwareManifest.ComputeFirmwareHashesAsync(
                options.FirmwarePath,
                cancellationToken)
                .ConfigureAwait(false);
        }
        catch (FirmwareValidationException exception)
        {
            console.Error(exception.Message);
            return ExitCode.FirmwareValidation;
        }

        var file = new FileInfo(options.FirmwarePath);
        console.Heading("ASA firmware upload");
        console.Detail("Firmware", file.FullName);
        console.Detail("Compressed size", $"{calculated.CompressedSize:N0} bytes");
        console.Detail("Raw size", $"{calculated.RawSize:N0} bytes");
        console.Detail("Modified", file.LastWriteTime);
        console.Detail("Compressed MD5", calculated.CompressedMd5);
        console.Detail("Raw firmware MD5", calculated.RawMd5);

        if (!string.Equals(
                calculated.CompressedMd5,
                hashes.CompressedMd5,
                StringComparison.OrdinalIgnoreCase))
        {
            console.Error("Compressed firmware MD5 mismatch.");
            console.Detail("Expected", hashes.CompressedMd5);
            console.Detail("Calculated", calculated.CompressedMd5);
            return ExitCode.FirmwareValidation;
        }

        if (!string.Equals(calculated.RawMd5, hashes.RawMd5, StringComparison.OrdinalIgnoreCase))
        {
            console.Error("Raw firmware MD5 mismatch.");
            console.Detail("Expected", hashes.RawMd5);
            console.Detail("Calculated", calculated.RawMd5);
            return ExitCode.FirmwareValidation;
        }

        console.Success("Compressed and raw firmware match the manifest.");

        runtime ??= new UploaderRuntime(options);
        var resolver = runtime.Resolver;
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

        console.Detail("Update API", endpoint.Current);
        var client = runtime.Client;

        DeviceFirmwareInfo? preflight = null;
        try
        {
            preflight = await client.GetFirmwareInfoAsync(
                endpoint,
                options.ConnectTimeout,
                cancellationToken).ConfigureAwait(false);
            if (preflight.FirmwareHash is not null)
            {
                console.Detail("Installed firmware MD5", preflight.FirmwareHash);
                console.Verbose($"Preflight source: {preflight.EndpointPath}", options.Verbose);
            }
            else
            {
                console.Warning(
                    $"{preflight.EndpointPath} responded but did not provide a valid firmware hash.");
            }
        }
        catch (HttpRequestException exception)
        {
            console.Warning($"Upload preflight could not read the installed firmware: {exception.Message}");
        }

        if (options.BackupPath is not null)
        {
            var backupResult = await BackupInstalledFirmwareAsync(
                options,
                console,
                endpoint,
                cancellationToken).ConfigureAwait(false);
            if (backupResult != ExitCode.Success)
            {
                console.Error("Upload aborted because the requested verified backup did not complete.");
                return backupResult;
            }
        }

        if (options.SkipIdentical &&
            preflight?.FirmwareHash is not null &&
            preflight.FirmwareHash.Equals(hashes.RawMd5, StringComparison.OrdinalIgnoreCase))
        {
            console.Success("The device already runs the requested firmware hash; upload skipped.");
            return ExitCode.Success;
        }

        var progress = new SynchronousProgress<(long Sent, long Total)>(
            value => console.UploadProgress(value.Sent, value.Total));

        try
        {
            await client.UploadAsync(
                endpoint,
                options.FirmwarePath,
                calculated.CompressedMd5,
                options.UploadTimeout,
                progress,
                cancellationToken).ConfigureAwait(false);
        }
        catch (UploadNetworkException firstFailure) when (
            resolver.UsesLocalDns && firstFailure.IsSafeToRetry)
        {
            console.ClearProgress();
            console.Warning(firstFailure.Message);
            console.Info("Cached .local address failed; resolving once more and retrying the upload.");
            try
            {
                endpoint = await resolver.ResolveAsync(console, forceRefresh: true, cancellationToken)
                    .ConfigureAwait(false);
                await client.UploadAsync(
                    endpoint,
                    options.FirmwarePath,
                    calculated.CompressedMd5,
                    options.UploadTimeout,
                    progress,
                    cancellationToken).ConfigureAwait(false);
            }
            catch (Exception retryFailure) when (
                retryFailure is UploadNetworkException or HttpRequestException)
            {
                console.ClearProgress();
                console.Error(retryFailure.Message);
                return ExitCode.NetworkOrUpload;
            }
            catch (UploadServerException exception)
            {
                console.ClearProgress();
                console.Error(exception.Message);
                return ExitCode.NetworkOrUpload;
            }
            catch (UnexpectedUploadResponseException exception)
            {
                console.ClearProgress();
                console.Error(exception.Message);
                return ExitCode.UnexpectedResponse;
            }
        }
        catch (UploadNetworkException exception)
        {
            console.ClearProgress();
            console.Error(exception.Message);
            return ExitCode.NetworkOrUpload;
        }
        catch (UploadServerException exception)
        {
            console.ClearProgress();
            console.Error(exception.Message);
            return ExitCode.NetworkOrUpload;
        }
        catch (UnexpectedUploadResponseException exception)
        {
            console.ClearProgress();
            console.Error(exception.Message);
            return ExitCode.UnexpectedResponse;
        }

        console.ClearProgress();
        console.Success("Device accepted the upload with exact response \"ok!\".");
        if (!options.Verify)
        {
            console.Warning("Post-upload device verification was disabled by request.");
            return ExitCode.Success;
        }

        console.Info("Waiting for the device to reboot and report the raw firmware hash…");

        return await VerifyAfterRebootAsync(
            options,
            console,
            resolver,
            endpoint,
            client,
            hashes.RawMd5,
            cancellationToken).ConfigureAwait(false);
    }

    private static async Task<ExitCode> VerifyAfterRebootAsync(
        UploaderOptions options,
        ConsoleUi console,
        EndpointResolver resolver,
        ResolvedEndpoint initialEndpoint,
        UploadClient client,
        string expectedRawHash,
        CancellationToken cancellationToken)
    {
        if (options.InitialPollDelay > TimeSpan.Zero)
        {
            await Task.Delay(options.InitialPollDelay, cancellationToken).ConfigureAwait(false);
        }

        var endpoint = initialEndpoint;
        var stopwatch = Stopwatch.StartNew();
        var refreshedLocalAddress = false;
        var receivedInfo = false;
        string? lastFirmwareHash = null;
        string? lastError = null;

        while (stopwatch.Elapsed < options.RebootTimeout)
        {
            cancellationToken.ThrowIfCancellationRequested();
            console.PollProgress(stopwatch.Elapsed, options.RebootTimeout);

            try
            {
                var remaining = options.RebootTimeout - stopwatch.Elapsed;
                var requestTimeout = remaining < options.ConnectTimeout
                    ? remaining
                    : options.ConnectTimeout;
                if (requestTimeout <= TimeSpan.Zero)
                {
                    break;
                }

                var deviceInfo = await client.GetFirmwareInfoAsync(endpoint, requestTimeout, cancellationToken)
                    .ConfigureAwait(false);
                receivedInfo = true;
                if (deviceInfo.FirmwareHash is { } reportedHash)
                {
                    lastFirmwareHash = reportedHash;
                    if (string.Equals(reportedHash, expectedRawHash, StringComparison.OrdinalIgnoreCase))
                    {
                        console.ClearProgress();
                        WriteUsefulInfo(console, deviceInfo.Values);
                        console.Verbose(
                            $"Post-upload hash source: {deviceInfo.EndpointPath}",
                            options.Verbose);
                        console.Success("Firmware hash matches the raw image. Update verified.");
                        return ExitCode.Success;
                    }

                    lastError = $"Device still reports firmware hash {reportedHash}.";
                }
                else
                {
                    lastError = $"{deviceInfo.EndpointPath} did not contain a firmware hash.";
                }
            }
            catch (HttpRequestException exception)
            {
                lastError = exception.Message;
                console.Verbose(lastError, options.Verbose);
                if (resolver.UsesLocalDns && !refreshedLocalAddress)
                {
                    refreshedLocalAddress = true;
                    try
                    {
                        endpoint = await resolver.ResolveAsync(console, forceRefresh: true, cancellationToken)
                            .ConfigureAwait(false);
                    }
                    catch (HttpRequestException resolveException)
                    {
                        lastError = resolveException.Message;
                        console.Verbose(lastError, options.Verbose);
                    }
                }
            }

            var delay = options.PollInterval;
            var available = options.RebootTimeout - stopwatch.Elapsed;
            if (delay > available)
            {
                delay = available;
            }

            if (delay > TimeSpan.Zero)
            {
                await Task.Delay(delay, cancellationToken).ConfigureAwait(false);
            }
        }

        console.ClearProgress();
        if (receivedInfo)
        {
            if (lastFirmwareHash is not null)
            {
                console.Error("Firmware hash did not match before the verification deadline.");
                console.Detail("Expected", expectedRawHash);
                console.Detail("Received", lastFirmwareHash);
            }
            else
            {
                console.Error(
                    "The device came back online, but its information endpoint did not report a firmware hash.");
            }

            return ExitCode.FirmwareVerification;
        }

        console.Error(
            $"Device did not return firmware information within {options.RebootTimeout.TotalSeconds:0.#}s.");
        if (!string.IsNullOrWhiteSpace(lastError))
        {
            console.Detail("Last error", lastError);
        }

        return ExitCode.RebootTimeout;
    }

    private static void WriteUsefulInfo(
        ConsoleUi console,
        IReadOnlyDictionary<string, string> info)
    {
        string[] interesting =
        [
            "hostname",
            "firmware hash",
            "firmware_hash",
            "firmwareHash",
            "program usage",
            "build",
            "uptime",
        ];

        var written = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var wanted in interesting)
        {
            var match = info.FirstOrDefault(
                pair => string.Equals(pair.Key, wanted, StringComparison.OrdinalIgnoreCase));
            if (!string.IsNullOrEmpty(match.Key) && written.Add(match.Key))
            {
                console.Detail(match.Key, match.Value);
            }
        }
    }

    private static async Task<ExitCode> BackupInstalledFirmwareAsync(
        UploaderOptions options,
        ConsoleUi console,
        ResolvedEndpoint updateEndpoint,
        CancellationToken cancellationToken)
    {
        var backupPath = options.BackupPath!;
        var partialPath = backupPath + ".part";
        console.Info("Creating the requested verified pre-upload backup.");
        console.Detail("Backup", backupPath);

        try
        {
            var directory = Path.GetDirectoryName(backupPath);
            if (!string.IsNullOrEmpty(directory))
            {
                Directory.CreateDirectory(directory);
            }
        }
        catch (Exception exception) when (
            exception is IOException or UnauthorizedAccessException or NotSupportedException)
        {
            console.Error($"Could not prepare the backup directory: {exception.Message}");
            return ExitCode.LocalFile;
        }

        var downloadOptions = options with
        {
            Mode = TransferMode.Download,
            DownloadPath = backupPath,
            Watch = false,
            Resume = false,
            Verify = true,
            BackupPath = null,
            SkipIdentical = false,
        };

        try
        {
            var sameOrigin =
                options.UpdateApi.Scheme.Equals(
                    options.EffectiveDownloadApi.Scheme,
                    StringComparison.OrdinalIgnoreCase) &&
                options.UpdateApi.Host.Equals(
                    options.EffectiveDownloadApi.Host,
                    StringComparison.OrdinalIgnoreCase) &&
                options.UpdateApi.Port == options.EffectiveDownloadApi.Port;
            if (!sameOrigin)
            {
                return await DownloadApp.RunAsync(downloadOptions, console, cancellationToken)
                    .ConfigureAwait(false);
            }

            var downloadEndpoint = updateEndpoint.Rebase(options.EffectiveDownloadApi);
            var downloaded = await DownloadApp.DownloadOnceAsync(
                downloadOptions,
                console,
                downloadEndpoint,
                partialPath,
                cancellationToken).ConfigureAwait(false);
            return DownloadApp.Finish(console, backupPath, partialPath, downloaded);
        }
        catch (Exception exception)
        {
            return DownloadApp.ReportFailure(console, exception);
        }
    }
}

internal sealed class SynchronousProgress<T>(Action<T> callback) : IProgress<T>
{
    public void Report(T value) => callback(value);
}

internal sealed class UploaderRuntime
{
    public UploaderRuntime(UploaderOptions options)
    {
        Resolver = new EndpointResolver(options.UpdateApi, options.ConnectTimeout);
        Client = new UploadClient(options.ConnectTimeout, options.RequestHeaders);
    }

    public EndpointResolver Resolver { get; }
    public UploadClient Client { get; }
}
