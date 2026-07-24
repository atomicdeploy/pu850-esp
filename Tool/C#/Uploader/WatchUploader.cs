namespace AsaFirmwareTransfer;

internal static class WatchUploader
{
    public static async Task<ExitCode> RunAsync(
        UploaderOptions options,
        ConsoleUi console,
        WatchStopController stopController,
        CancellationToken cancellationToken)
    {
        var directory = Path.GetDirectoryName(options.FirmwarePath);
        if (string.IsNullOrWhiteSpace(directory) || !Directory.Exists(directory))
        {
            console.Error($"Watch directory does not exist: {directory}");
            return ExitCode.FirmwareValidation;
        }

        await using var monitor = new BuildWatchMonitor(
            options.FirmwarePath,
            options.ManifestPath,
            options.WatchPollInterval);
        var runtime = new UploaderRuntime(options);
        WatchBuildIdentity? lastUploaded = null;
        long handledGeneration = 0;
        var includeCurrent = true;

        console.Heading("Continuous firmware watch mode");
        console.Detail("Firmware", options.FirmwarePath);
        console.Detail("Manifest", options.ManifestPath);
        console.Info(
            $"Waiting for firmware and MD5 to remain unchanged for " +
            $"{options.WatchDebounce.TotalSeconds:0.###}s.");

        while (true)
        {
            StableBuild stableBuild;
            try
            {
                stableBuild = await monitor.WaitForStableAsync(
                    handledGeneration,
                    includeCurrent,
                    options.WatchDebounce,
                    console,
                    stopController.StopToken,
                    cancellationToken).ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (
                stopController.IsStopRequested &&
                !cancellationToken.IsCancellationRequested)
            {
                console.Success("Watcher stopped cleanly.");
                return ExitCode.Success;
            }

            includeCurrent = false;
            if (stopController.IsStopRequested)
            {
                console.Success("Watcher stopped cleanly.");
                return ExitCode.Success;
            }

            console.Success("Newest firmware and MD5 manifest are stable.");
            var candidate = await TryReadIdentityAsync(options, cancellationToken).ConfigureAwait(false);
            if (!stopController.TryBeginOperation())
            {
                console.Success("Watcher stopped before starting another upload.");
                return ExitCode.Success;
            }

            ExitCode result;
            try
            {
                if (candidate is not null && candidate == lastUploaded)
                {
                    handledGeneration = stableBuild.Generation;
                    console.Info("Stable rebuild is byte-identical to the last verified upload; skipped.");
                    continue;
                }

                result = await UploaderApp.RunAsync(
                    options,
                    console,
                    cancellationToken,
                    runtime).ConfigureAwait(false);
                handledGeneration = stableBuild.Generation;
            }
            finally
            {
                stopController.EndOperation();
            }

            if (result == ExitCode.Success)
            {
                lastUploaded = candidate;
            }
            else
            {
                console.Warning(
                    $"Watched upload ended with exit code {(int)result}; " +
                    "waiting for the next stable build.");
            }

            if (stopController.IsStopRequested)
            {
                console.Success("In-flight upload finished; watcher stopped without replaying pending changes.");
                return ExitCode.Success;
            }
        }
    }

    private static async Task<WatchBuildIdentity?> TryReadIdentityAsync(
        UploaderOptions options,
        CancellationToken cancellationToken)
    {
        try
        {
            var manifest = await FirmwareManifest.LoadAsync(options.ManifestPath, cancellationToken)
                .ConfigureAwait(false);
            var hashes = manifest.SelectFor(Path.GetFileName(options.FirmwarePath));
            var actualHash = await FirmwareManifest.ComputeMd5Async(
                options.FirmwarePath,
                cancellationToken).ConfigureAwait(false);
            return string.Equals(actualHash, hashes.CompressedMd5, StringComparison.OrdinalIgnoreCase)
                ? new WatchBuildIdentity(actualHash, hashes.RawMd5)
                : null;
        }
        catch (FirmwareValidationException)
        {
            return null;
        }
    }
}

internal sealed class WatchStopController : IDisposable
{
    private readonly CancellationTokenSource stopSource = new();
    private readonly object sync = new();
    private int requestCount;
    private bool operationActive;

    public CancellationToken StopToken => stopSource.Token;
    public bool IsStopRequested
    {
        get
        {
            lock (sync)
            {
                return requestCount > 0;
            }
        }
    }

    public bool RequestStop()
    {
        bool firstRequest;
        lock (sync)
        {
            requestCount++;
            firstRequest = requestCount == 1;
        }

        if (firstRequest)
        {
            stopSource.Cancel();
        }

        return firstRequest;
    }

    public bool TryBeginOperation()
    {
        lock (sync)
        {
            if (requestCount > 0)
            {
                return false;
            }

            if (operationActive)
            {
                throw new InvalidOperationException("A watched upload operation is already active.");
            }

            operationActive = true;
            return true;
        }
    }

    public void EndOperation()
    {
        lock (sync)
        {
            if (!operationActive)
            {
                throw new InvalidOperationException("No watched upload operation is active.");
            }

            operationActive = false;
        }
    }

    public void Dispose() => stopSource.Dispose();
}

internal sealed record WatchBuildIdentity(string CompressedMd5, string RawMd5);

internal sealed class BuildWatchMonitor : IAsyncDisposable
{
    private readonly string firmwarePath;
    private readonly string manifestPath;
    private readonly string gzipPath;
    private readonly string localHeaderPath;
    private readonly TimeSpan pollInterval;
    private readonly TimeProvider timeProvider;
    private readonly long startTimestamp;
    private readonly WatchStateMachine state = new();
    private readonly object stateLock = new();
    private readonly CancellationTokenSource shutdown = new();
    private readonly FileSystemWatcher? watcher;
    private readonly Task pollTask;

    public BuildWatchMonitor(
        string firmwarePath,
        string manifestPath,
        TimeSpan pollInterval,
        TimeProvider? timeProvider = null)
    {
        this.firmwarePath = Path.GetFullPath(firmwarePath);
        this.manifestPath = Path.GetFullPath(manifestPath);
        gzipPath = this.firmwarePath + ".gz";
        localHeaderPath = Path.Combine(
            Path.GetDirectoryName(this.firmwarePath)!,
            "~local.h");
        this.pollInterval = pollInterval;
        this.timeProvider = timeProvider ?? TimeProvider.System;
        startTimestamp = this.timeProvider.GetTimestamp();

        lock (stateLock)
        {
            state.Observe(Capture(), MonotonicNow(), forceEvent: true);
        }

        watcher = TryCreateFileSystemWatcher(Path.GetDirectoryName(this.firmwarePath)!);
        pollTask = PollAsync(shutdown.Token);
    }

    public async Task<StableBuild> WaitForStableAsync(
        long afterGeneration,
        bool includeCurrent,
        TimeSpan debounce,
        ConsoleUi console,
        CancellationToken stopToken,
        CancellationToken cancellationToken)
    {
        using var waitCancellation = CancellationTokenSource.CreateLinkedTokenSource(
            stopToken,
            cancellationToken,
            shutdown.Token);
        var waitInterval = pollInterval < TimeSpan.FromMilliseconds(250)
            ? pollInterval
            : TimeSpan.FromMilliseconds(250);
        string? lastStatus = null;

        while (true)
        {
            waitCancellation.Token.ThrowIfCancellationRequested();
            StableBuild stable;
            string status;
            lock (stateLock)
            {
                var now = MonotonicNow();
                if (state.TryGetStable(
                    afterGeneration,
                    includeCurrent,
                    now,
                    debounce,
                    out stable))
                {
                    return stable;
                }

                status = state.GetStatus(afterGeneration, includeCurrent, now, debounce);
            }

            if (!string.Equals(status, lastStatus, StringComparison.Ordinal))
            {
                console.Info(status);
                lastStatus = status;
            }

            await Task.Delay(waitInterval, timeProvider, waitCancellation.Token).ConfigureAwait(false);
        }
    }

    public async ValueTask DisposeAsync()
    {
        watcher?.Dispose();
        shutdown.Cancel();
        try
        {
            await pollTask.ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            // Expected while shutting down the polling fallback.
        }
        finally
        {
            shutdown.Dispose();
        }
    }

    private FileSystemWatcher? TryCreateFileSystemWatcher(string directory)
    {
        try
        {
            var fileWatcher = new FileSystemWatcher(directory)
            {
                IncludeSubdirectories = false,
                NotifyFilter =
                    NotifyFilters.FileName |
                    NotifyFilters.LastWrite |
                    NotifyFilters.Size |
                    NotifyFilters.CreationTime,
                EnableRaisingEvents = false,
            };
            fileWatcher.Changed += OnFileChanged;
            fileWatcher.Created += OnFileChanged;
            fileWatcher.Deleted += OnFileChanged;
            fileWatcher.Renamed += OnFileRenamed;
            fileWatcher.Error += OnWatcherError;
            fileWatcher.EnableRaisingEvents = true;
            return fileWatcher;
        }
        catch (Exception exception) when (
            exception is IOException or UnauthorizedAccessException or PlatformNotSupportedException)
        {
            // The signature polling loop remains authoritative and cross-platform.
            return null;
        }
    }

    private async Task PollAsync(CancellationToken cancellationToken)
    {
        while (true)
        {
            await Task.Delay(pollInterval, timeProvider, cancellationToken).ConfigureAwait(false);
            var observation = Capture();
            lock (stateLock)
            {
                state.Observe(observation, MonotonicNow(), forceEvent: false);
            }
        }
    }

    private BuildObservation Capture()
    {
        try
        {
            return new BuildObservation(
                CaptureFile(firmwarePath),
                CaptureFile(manifestPath),
                CaptureFile(gzipPath),
                CaptureFile(localHeaderPath),
                null);
        }
        catch (Exception exception) when (
            exception is IOException or UnauthorizedAccessException)
        {
            return new BuildObservation(default, default, default, default, exception.Message);
        }
    }

    private static BuildFileStamp CaptureFile(string path)
    {
        var information = new FileInfo(path);
        information.Refresh();
        return information.Exists
            ? new BuildFileStamp(true, information.Length, information.LastWriteTimeUtc.Ticks)
            : default;
    }

    private void OnFileChanged(object sender, FileSystemEventArgs eventArgs)
    {
        try
        {
            if (IsRelevant(eventArgs.FullPath))
            {
                RecordEvent();
            }
        }
        catch (Exception exception) when (
            exception is IOException or UnauthorizedAccessException or ArgumentException)
        {
            RecordEvent();
        }
    }

    private void OnFileRenamed(object sender, RenamedEventArgs eventArgs)
    {
        try
        {
            if (IsRelevant(eventArgs.FullPath) || IsRelevant(eventArgs.OldFullPath))
            {
                RecordEvent();
            }
        }
        catch (Exception exception) when (
            exception is IOException or UnauthorizedAccessException or ArgumentException)
        {
            RecordEvent();
        }
    }

    private void OnWatcherError(object sender, ErrorEventArgs eventArgs) => RecordEvent();

    private void RecordEvent()
    {
        lock (stateLock)
        {
            state.Observe(state.Current, MonotonicNow(), forceEvent: true);
        }
    }

    private TimeSpan MonotonicNow() =>
        timeProvider.GetElapsedTime(startTimestamp, timeProvider.GetTimestamp());

    private bool IsRelevant(string path)
    {
        var comparison = OperatingSystem.IsWindows()
            ? StringComparison.OrdinalIgnoreCase
            : StringComparison.Ordinal;
        var fullPath = Path.GetFullPath(path);
        return
            string.Equals(fullPath, firmwarePath, comparison) ||
            string.Equals(fullPath, manifestPath, comparison) ||
            string.Equals(fullPath, gzipPath, comparison) ||
            string.Equals(fullPath, localHeaderPath, comparison);
    }
}

internal sealed class WatchStateMachine
{
    private BuildObservation? current;
    private long generation;
    private TimeSpan lastChanged;

    public BuildObservation Current => current ?? BuildObservation.Empty;

    public bool Observe(
        BuildObservation observation,
        TimeSpan now,
        bool forceEvent)
    {
        if (!forceEvent && observation == current)
        {
            return false;
        }

        current = observation;
        generation++;
        lastChanged = now;
        return true;
    }

    public bool TryGetStable(
        long afterGeneration,
        bool includeCurrent,
        TimeSpan now,
        TimeSpan debounce,
        out StableBuild stable)
    {
        stable = default;
        if (current is null ||
            !current.IsReady ||
            (!includeCurrent && generation <= afterGeneration) ||
            now - lastChanged < debounce)
        {
            return false;
        }

        stable = new StableBuild(generation, current);
        return true;
    }

    public string GetStatus(
        long afterGeneration,
        bool includeCurrent,
        TimeSpan now,
        TimeSpan debounce)
    {
        if (current is null)
        {
            return "Inspecting watched build files…";
        }

        if (!current.IsReady)
        {
            return current.WaitingReason;
        }

        if (!includeCurrent && generation <= afterGeneration)
        {
            return "Watching for the next firmware or MD5 change…";
        }

        if (now - lastChanged < debounce)
        {
            return "Build change detected; waiting for firmware and MD5 to settle…";
        }

        return "Stable build is ready.";
    }
}

internal readonly record struct StableBuild(long Generation, BuildObservation Observation);

internal sealed record BuildObservation(
    BuildFileStamp Firmware,
    BuildFileStamp Manifest,
    BuildFileStamp Gzip,
    BuildFileStamp LocalHeader,
    string? Error)
{
    public static BuildObservation Empty { get; } =
        new(default, default, default, default, "Build files have not been inspected.");

    public bool IsReady =>
        Error is null &&
        Firmware.Exists &&
        Manifest.Exists &&
        !Gzip.Exists &&
        !LocalHeader.Exists;

    public string WaitingReason
    {
        get
        {
            if (Error is not null)
            {
                return $"Waiting until build files are readable: {Error}";
            }

            if (LocalHeader.Exists)
            {
                return "Build is active (~local.h exists); waiting…";
            }

            if (Gzip.Exists)
            {
                return "Build is active (temporary firmware .gz exists); waiting…";
            }

            if (!Firmware.Exists && !Manifest.Exists)
            {
                return "Waiting for firmware and its MD5 manifest…";
            }

            if (!Firmware.Exists)
            {
                return "Waiting for the firmware file…";
            }

            return "Waiting for the MD5 manifest…";
        }
    }
}

internal readonly record struct BuildFileStamp(bool Exists, long Length, long LastWriteUtcTicks);
