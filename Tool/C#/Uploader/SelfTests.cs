using System.IO.Compression;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;

namespace AsaFirmwareTransfer;

internal static class SelfTests
{
    private static int assertions;

    public static async Task<int> RunAsync()
    {
        var temporaryDirectory = Path.Combine(
            Path.GetTempPath(),
            $"asa-uploader-tests-{Guid.NewGuid():N}");
        Directory.CreateDirectory(temporaryDirectory);

        try
        {
            await TestManifestAsync(temporaryDirectory).ConfigureAwait(false);
            TestInfoParser();
            TestConsoleHardening();
            TestOptions();
            TestWatchStateMachine();
            TestWatchStopController();
            await TestPreStoppedWatchRunnerAsync(temporaryDirectory).ConfigureAwait(false);
            TestResponseValidation();
            await TestUploadAndVerificationAsync(temporaryDirectory).ConfigureAwait(false);
            await TestSkipIdenticalUploadAsync(temporaryDirectory).ConfigureAwait(false);
            await TestVerifiedDownloadAsync(temporaryDirectory).ConfigureAwait(false);
            await TestInfoFallbackDownloadAsync(temporaryDirectory).ConfigureAwait(false);
            await TestStrictResumeAsync(temporaryDirectory).ConfigureAwait(false);
            await TestDownloadHashMismatchAsync(temporaryDirectory).ConfigureAwait(false);
            await TestIgnoredRangeRejectedAsync(temporaryDirectory).ConfigureAwait(false);
            await TestConnectionFailureMappingAsync(temporaryDirectory).ConfigureAwait(false);
            await TestCommittedUploadIsNotRetryableAsync(temporaryDirectory).ConfigureAwait(false);
            await TestRedirectRefusedAsync(temporaryDirectory).ConfigureAwait(false);
            await TestResponseSizeBoundAsync().ConfigureAwait(false);
            await TestDownloadSizeBoundAsync().ConfigureAwait(false);
            Console.WriteLine($"✓ C# uploader self-tests passed ({assertions} assertions).");
            return 0;
        }
        catch (Exception exception)
        {
            Console.Error.WriteLine($"✗ C# uploader self-test failed: {exception}");
            return 1;
        }
        finally
        {
            try
            {
                Directory.Delete(temporaryDirectory, recursive: true);
            }
            catch
            {
                // A failed cleanup must not hide a test result.
            }
        }
    }

    private static async Task TestManifestAsync(string directory)
    {
        var firmware = Path.Combine(directory, "fixture.bin");
        var rawBytes = Encoding.UTF8.GetBytes("raw firmware fixture");
        var compressedBytes = Gzip(rawBytes);
        await File.WriteAllBytesAsync(firmware, compressedBytes).ConfigureAwait(false);

        var compressedHash = Md5(compressedBytes);
        var rawHash = Md5(rawBytes);
        var manifestPath = firmware + ".md5";
        await File.WriteAllTextAsync(
            manifestPath,
            $"{rawHash.ToUpperInvariant()}  *folder\\fixture.bin{Environment.NewLine}" +
            $"{compressedHash} *fixture.bin (compressed){Environment.NewLine}").ConfigureAwait(false);

        var manifest = await FirmwareManifest.LoadAsync(manifestPath, CancellationToken.None)
            .ConfigureAwait(false);
        var selected = manifest.SelectFor("fixture.bin");
        Equal(compressedHash, selected.CompressedMd5, "compressed manifest hash");
        Equal(rawHash, selected.RawMd5, "raw manifest hash");
        var calculated = await FirmwareManifest.ComputeFirmwareHashesAsync(
            firmware,
            CancellationToken.None).ConfigureAwait(false);
        Equal(compressedHash, calculated.CompressedMd5, "calculated compressed hash");
        Equal(rawHash, calculated.RawMd5, "calculated raw hash");
        Equal((long)rawBytes.Length, calculated.RawSize, "calculated raw size");

        var invalidGzip = Path.Combine(directory, "invalid-gzip.bin");
        await File.WriteAllBytesAsync(invalidGzip, Encoding.UTF8.GetBytes("not gzip"))
            .ConfigureAwait(false);
        await ThrowsAsync<FirmwareValidationException>(
            () => FirmwareManifest.ComputeFirmwareHashesAsync(
                invalidGzip,
                CancellationToken.None),
            "non-gzip firmware is rejected").ConfigureAwait(false);

        await File.WriteAllTextAsync(
            manifestPath,
            $"00000000000000000000000000000000 *fixture.bin{Environment.NewLine}" +
            $"{compressedHash} *fixture.bin (compressed){Environment.NewLine}").ConfigureAwait(false);
        var mismatchResult = await UploaderApp.RunAsync(
            new UploaderOptions
            {
                FirmwarePath = firmware,
                ManifestPath = manifestPath,
                UpdateApi = new Uri("http://127.0.0.1:1/update"),
                NoColor = true,
            },
            QuietConsole(),
            CancellationToken.None).ConfigureAwait(false);
        Equal(
            ExitCode.FirmwareValidation,
            mismatchResult,
            "raw manifest mismatch is rejected before network access");

        await File.WriteAllTextAsync(
            manifestPath,
            $"{rawHash} *fixture.bin{Environment.NewLine}" +
            $"{compressedHash} *fixture.bin (compressed){Environment.NewLine}" +
            $"{rawHash} *fixture.bin (compressed){Environment.NewLine}").ConfigureAwait(false);
        await ThrowsAsync<FirmwareValidationException>(
            () => FirmwareManifest.LoadAsync(manifestPath, CancellationToken.None),
            "conflicting manifest entries are rejected").ConfigureAwait(false);
    }

    private static void TestInfoParser()
    {
        var plain = InfoParser.Parse(
            "hostname: asa-device\r\nfirmware hash: 00112233445566778899aabbccddeeff\r\nuptime: 3s\r\n");
        True(InfoParser.TryGetFirmwareHash(plain, out var plainHash), "plain /info hash found");
        Equal("00112233445566778899aabbccddeeff", plainHash, "plain /info hash parsed");

        var json = InfoParser.Parse(
            """{"device":{"firmwareHash":"ffeeddccbbaa99887766554433221100"},"uptime":"4s"}""");
        True(InfoParser.TryGetFirmwareHash(json, out var jsonHash), "JSON /info hash found");
        Equal("ffeeddccbbaa99887766554433221100", jsonHash, "JSON /info hash parsed");

        var updateInfo = InfoParser.Parse(
            """{"hash":"0123456789abcdef0123456789abcdef","size":1234}""");
        True(
            InfoParser.TryGetDeviceFirmwareHash(updateInfo, out var updateHash),
            "/update/info generic hash found in device-info context");
        Equal(
            "0123456789abcdef0123456789abcdef",
            updateHash,
            "/update/info generic hash parsed");
        True(
            !InfoParser.TryGetFirmwareHash(updateInfo, out _),
            "generic hash remains rejected outside device-info context");

        var uppercase = InfoParser.Parse(
            "firmware MD5: FFEEDDCCBBAA99887766554433221100\r\n");
        True(InfoParser.TryGetFirmwareHash(uppercase, out var uppercaseHash), "uppercase hash accepted");
        Equal("ffeeddccbbaa99887766554433221100", uppercaseHash, "hash normalized to lowercase");

        string[] invalidHashValues =
        [
            "x00112233445566778899aabbccddeeff",
            "00112233445566778899aabbccddeeffx",
            "00112233445566778899aabbccddee",
            "00112233445566778899aabbccddeeff00",
            "00112233445566778899aabbccddeefg",
            "0011223344556677\u001b[31m8899aabbccddeeff",
        ];
        foreach (var invalidHash in invalidHashValues)
        {
            var invalid = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
            {
                ["firmware hash"] = invalidHash,
            };
            True(
                !InfoParser.TryGetFirmwareHash(invalid, out _),
                $"invalid firmware hash rejected: {ConsoleUi.Sanitize(invalidHash)}");
        }

        var misleadingKey = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
        {
            ["not firmware hash"] = "00112233445566778899aabbccddeeff",
        };
        True(!InfoParser.TryGetFirmwareHash(misleadingKey, out _), "misleading hash key rejected");
    }

    private static void TestConsoleHardening()
    {
        var sanitized = ConsoleUi.Sanitize("before\u001b[31mred\r\nnext\t\u0007\u009Bafter");
        Equal(
            "before\\x1B[31mred\\r\\nnext\\t\\x07\\x9Bafter",
            sanitized,
            "terminal controls rendered inert");

        var redirectedOutput = new StringWriter();
        var terminalError = new StringWriter();
        var stderrColorUi = new ConsoleUi(
            noColor: false,
            redirectedOutput,
            terminalError,
            stdoutRedirected: true,
            stderrRedirected: false,
            environmentAllowsColor: true);
        stderrColorUi.Info("server\u001b[31m-owned");
        stderrColorUi.Error("server\u001b[32m-owned");

        True(!redirectedOutput.ToString().Contains('\u001b'), "redirected stdout contains no ANSI");
        True(
            redirectedOutput.ToString().Contains("server\\x1B[31m-owned", StringComparison.Ordinal),
            "stdout server escape sanitized");
        True(
            terminalError.ToString().Contains("\u001b[1;31m", StringComparison.Ordinal),
            "terminal stderr retains trusted error styling");
        True(
            !terminalError.ToString().Contains("\u001b[32m-owned", StringComparison.Ordinal),
            "stderr server escape cannot inject styling");
        True(
            terminalError.ToString().Contains("server\\x1B[32m-owned", StringComparison.Ordinal),
            "stderr server escape rendered inert");

        var terminalOutput = new StringWriter();
        var redirectedError = new StringWriter();
        var stdoutColorUi = new ConsoleUi(
            noColor: false,
            terminalOutput,
            redirectedError,
            stdoutRedirected: false,
            stderrRedirected: true,
            environmentAllowsColor: true);
        stdoutColorUi.Info("normal");
        stdoutColorUi.Error("failure");
        True(
            terminalOutput.ToString().Contains("\u001b[36m", StringComparison.Ordinal),
            "terminal stdout retains trusted styling");
        True(!redirectedError.ToString().Contains('\u001b'), "redirected stderr contains no ANSI");
    }

    private static void TestOptions()
    {
        var parsed = UploaderOptions.Parse(
            ["firmware.bin", "--url", "http://127.0.0.1/update", "--initial-delay", "0", "--once"],
            "http://should-not-be-used.invalid/update");
        True(parsed.Error is null && parsed.Options is not null, "valid CLI parsed");
        Equal("127.0.0.1", parsed.Options!.UpdateApi.Host, "CLI URL overrides UPDATE_API");
        Equal(TimeSpan.Zero, parsed.Options.InitialPollDelay, "zero initial delay accepted");

        var invalid = UploaderOptions.Parse(
            ["firmware.bin", "--url", "ftp://127.0.0.1/update"],
            null);
        True(invalid.Error is not null, "non-HTTP endpoint rejected");

        var embeddedCredentials = UploaderOptions.Parse(
            ["firmware.bin", "--url", "http://user:password@127.0.0.1/update"],
            null);
        True(embeddedCredentials.Error is not null, "URL userinfo credentials rejected");

        const string primaryTokenName = "ASA_SELF_TEST_PRIMARY_TOKEN";
        const string fallbackTokenName = "ASA_SELF_TEST_FALLBACK_TOKEN";
        var oldPrimary = Environment.GetEnvironmentVariable(primaryTokenName);
        var oldFallback = Environment.GetEnvironmentVariable(fallbackTokenName);
        try
        {
            Environment.SetEnvironmentVariable(primaryTokenName, "update-token");
            Environment.SetEnvironmentVariable(fallbackTokenName, "legacy-token");
            Equal(
                "update-token",
                Program.FirstEnvironment(primaryTokenName, fallbackTokenName),
                "first bearer-token environment wins");
        }
        finally
        {
            Environment.SetEnvironmentVariable(primaryTokenName, oldPrimary);
            Environment.SetEnvironmentVariable(fallbackTokenName, oldFallback);
        }

        var watch = UploaderOptions.Parse(
            [
                "firmware.bin",
                "--url",
                "http://127.0.0.1/update",
                "--watch",
                "--watch-debounce",
                "1.5",
                "--watch-poll",
                "0.1",
            ],
            null);
        True(watch.Error is null && watch.Options?.Watch == true, "explicit watch mode parsed");
        Equal(TimeSpan.FromSeconds(1.5), watch.Options!.WatchDebounce, "watch debounce parsed");
        Equal(TimeSpan.FromSeconds(0.1), watch.Options.WatchPollInterval, "watch poll parsed");

        var conflictingMode = UploaderOptions.Parse(
            ["firmware.bin", "--url", "http://127.0.0.1/update", "--watch", "--once"],
            null);
        True(conflictingMode.Error is not null, "watch and once conflict rejected");

        var busyPoll = UploaderOptions.Parse(
            ["firmware.bin", "--url", "http://127.0.0.1/update", "--watch", "--watch-poll", "0.001"],
            null);
        True(busyPoll.Error is not null, "busy-loop watch polling rejected");

        var download = UploaderOptions.Parse(
            [
                "download",
                "backup.bin",
                "--url",
                "http://device.local/update",
                "--resume",
                "--bearer",
                "secret-token",
                "--header",
                "X-Site: lab",
            ],
            null);
        True(download.Error is null && download.Options is not null, "download CLI parsed");
        Equal(TransferMode.Download, download.Options!.Mode, "download mode selected");
        Equal(
            "/firmware/download",
            download.Options.EffectiveDownloadApi.AbsolutePath,
            "download endpoint derived from update URL");
        True(download.Options.Resume, "strict resume option parsed");
        Equal(2, download.Options.RequestHeaders.Count, "bearer and custom header retained");

        var conflictingAuth = UploaderOptions.Parse(
            [
                "download",
                "backup.bin",
                "--url",
                "http://127.0.0.1/update",
                "--bearer",
                "secret",
                "--header",
                "Authorization: Basic hidden",
            ],
            null);
        True(conflictingAuth.Error is not null, "conflicting authorization sources rejected");

        var managedHeader = UploaderOptions.Parse(
            [
                "download",
                "backup.bin",
                "--url",
                "http://127.0.0.1/update",
                "--header",
                "Range: bytes=0-",
            ],
            null);
        True(managedHeader.Error is not null, "tool-managed Range header rejected");
    }

    private static void TestWatchStateMachine()
    {
        var start = TimeSpan.Zero;
        TimeSpan At(double seconds) => start + TimeSpan.FromSeconds(seconds);
        var debounce = TimeSpan.FromSeconds(1);
        var machine = new WatchStateMachine();
        var first = ReadyBuild(firmwareLength: 100, manifestLength: 80, writeTick: 1);
        True(machine.Observe(first, start, forceEvent: true), "initial watch observation recorded");
        True(
            !machine.TryGetStable(0, includeCurrent: true, At(0.999), debounce, out _),
            "initial build is not stable before debounce");
        True(
            machine.TryGetStable(0, includeCurrent: true, At(1), debounce, out var firstStable),
            "initial build becomes stable at debounce boundary");
        True(
            !machine.TryGetStable(
                firstStable.Generation,
                includeCurrent: false,
                At(5),
                debounce,
                out _),
            "handled generation is not replayed");

        var second = ReadyBuild(firmwareLength: 101, manifestLength: 80, writeTick: 2);
        var newest = ReadyBuild(firmwareLength: 102, manifestLength: 81, writeTick: 3);
        True(machine.Observe(second, At(2), forceEvent: false), "first upload-time change recorded");
        True(machine.Observe(newest, At(2.2), forceEvent: false), "newest upload-time change coalesced");
        True(
            !machine.TryGetStable(
                firstStable.Generation,
                includeCurrent: false,
                At(3.1),
                debounce,
                out _),
            "coalesced build waits from newest change");
        True(
            machine.TryGetStable(
                firstStable.Generation,
                includeCurrent: false,
                At(3.2),
                debounce,
                out var newestStable),
            "newest coalesced build becomes stable");
        Equal(newest, newestStable.Observation, "coalescing returns only newest build state");

        True(machine.Observe(newest, At(4), forceEvent: true), "same-signature file event resets debounce");
        True(
            !machine.TryGetStable(
                newestStable.Generation,
                includeCurrent: false,
                At(4.9),
                debounce,
                out _),
            "same-signature event still debounced");
        True(
            machine.TryGetStable(
                newestStable.Generation,
                includeCurrent: false,
                At(5),
                debounce,
                out _),
            "same-signature event stabilizes without being lost");

        var gzipBlocked = newest with
        {
            Gzip = new BuildFileStamp(true, 32, 4),
        };
        machine.Observe(gzipBlocked, At(6), forceEvent: false);
        True(
            !machine.TryGetStable(0, includeCurrent: true, At(20), debounce, out _),
            "temporary gzip blocks a stable build");
        True(
            machine.GetStatus(0, includeCurrent: true, At(20), debounce)
                .Contains(".gz", StringComparison.Ordinal),
            "gzip blocker has a useful status");

        var headerBlocked = newest with
        {
            LocalHeader = new BuildFileStamp(true, 12, 5),
        };
        machine.Observe(headerBlocked, At(21), forceEvent: false);
        True(
            !machine.TryGetStable(0, includeCurrent: true, At(30), debounce, out _),
            "~local.h blocks a stable build");
        True(
            machine.GetStatus(0, includeCurrent: true, At(30), debounce)
                .Contains("~local.h", StringComparison.Ordinal),
            "~local.h blocker has a useful status");
    }

    private static void TestWatchStopController()
    {
        using (var active = new WatchStopController())
        {
            True(active.TryBeginOperation(), "watch upload begins before stop request");
            True(active.RequestStop(), "stop request accepts already in-flight upload");
            active.EndOperation();
            True(!active.TryBeginOperation(), "pending changes cannot replay after graceful stop");
        }

        using var stop = new WatchStopController();
        True(stop.RequestStop(), "first Ctrl+C requests graceful watcher stop");
        True(stop.IsStopRequested, "watcher stop state retained");
        True(stop.StopToken.IsCancellationRequested, "first Ctrl+C wakes stability wait");
        True(!stop.TryBeginOperation(), "stop request atomically blocks another upload");
        True(!stop.RequestStop(), "second Ctrl+C requests outer immediate cancellation");
    }

    private static async Task TestPreStoppedWatchRunnerAsync(string directory)
    {
        var options = new UploaderOptions
        {
            FirmwarePath = Path.Combine(directory, "not-yet-built.bin"),
            ManifestPath = Path.Combine(directory, "not-yet-built.bin.md5"),
            UpdateApi = new Uri("http://192.0.2.1/update"),
            Watch = true,
            WatchDebounce = TimeSpan.FromMilliseconds(50),
            WatchPollInterval = TimeSpan.FromMilliseconds(10),
            NoColor = true,
        };
        using var stop = new WatchStopController();
        stop.RequestStop();
        var result = await WatchUploader.RunAsync(
            options,
            new ConsoleUi(noColor: true),
            stop,
            CancellationToken.None).ConfigureAwait(false);
        Equal(ExitCode.Success, result, "pre-stopped watch exits cleanly without an upload");
    }

    private static BuildObservation ReadyBuild(long firmwareLength, long manifestLength, long writeTick) =>
        new(
            new BuildFileStamp(true, firmwareLength, writeTick),
            new BuildFileStamp(true, manifestLength, writeTick),
            default,
            default,
            null);

    private static void TestResponseValidation()
    {
        True(UploadClient.IsAcceptedResponse("ok!"), "exact ok response accepted");
        True(!UploadClient.IsAcceptedResponse("ok!\n"), "newline response rejected");
        True(!UploadClient.IsAcceptedResponse("OK!"), "case-changing response rejected");
    }

    private static async Task TestUploadAndVerificationAsync(string directory)
    {
        var firmware = Path.Combine(directory, "integration.bin");
        var rawBytes = Encoding.UTF8.GetBytes("integration raw image");
        var compressedBytes = Gzip(rawBytes);
        var compressedHash = Md5(compressedBytes);
        var rawHash = Md5(rawBytes);
        await File.WriteAllBytesAsync(firmware, compressedBytes).ConfigureAwait(false);
        await File.WriteAllTextAsync(
            firmware + ".md5",
            $"{rawHash} *integration.bin{Environment.NewLine}" +
            $"{compressedHash} *integration.bin (compressed){Environment.NewLine}").ConfigureAwait(false);

        await using var server = new FakeFirmwareServer(compressedHash, rawHash);
        var serverTask = server.RunAsync();
        var options = new UploaderOptions
        {
            FirmwarePath = firmware,
            ManifestPath = firmware + ".md5",
            UpdateApi = new Uri($"http://127.0.0.1:{server.Port}/update"),
            ConnectTimeout = TimeSpan.FromSeconds(2),
            UploadTimeout = TimeSpan.FromSeconds(5),
            RebootTimeout = TimeSpan.FromSeconds(5),
            PollInterval = TimeSpan.FromMilliseconds(50),
            InitialPollDelay = TimeSpan.Zero,
            NoColor = true,
        };

        var result = await UploaderApp.RunAsync(
            options,
            new ConsoleUi(noColor: true),
            CancellationToken.None).ConfigureAwait(false);
        await serverTask.WaitAsync(TimeSpan.FromSeconds(5)).ConfigureAwait(false);
        Equal(ExitCode.Success, result, "end-to-end upload result");
        True(server.SawMd5Field, "multipart MD5 field sent");
        True(server.SawFirmwareField, "multipart firmware field sent");
        True(server.SawCompressedHash, "multipart compressed hash sent");
        Equal("/update", server.UploadPath, "update request path");
        Equal("/update/info", server.InfoPath, "update info request path");
    }

    private static async Task TestVerifiedDownloadAsync(string directory)
    {
        var firmware = Encoding.UTF8.GetBytes("deterministic downloaded firmware image");
        var destination = Path.Combine(directory, "verified-download.bin");
        await using var server = new FakeDownloadServer(firmware, expectedRequests: 3);
        var serverTask = server.RunAsync();
        var options = DownloadOptions(destination, server.Port) with
        {
            RequestHeaders =
            [
                new RequestHeader("Authorization", "Bearer private-test-token"),
                new RequestHeader("X-Lab", "alpha"),
            ],
        };

        var result = await DownloadApp.RunAsync(
            options,
            new ConsoleUi(noColor: true),
            CancellationToken.None).ConfigureAwait(false);
        await serverTask.WaitAsync(TimeSpan.FromSeconds(5)).ConfigureAwait(false);

        Equal(ExitCode.Success, result, "verified download result");
        True(
            (await File.ReadAllBytesAsync(destination).ConfigureAwait(false)).SequenceEqual(firmware),
            "downloaded bytes atomically installed");
        True(!File.Exists(destination + ".part"), "successful download removes .part through rename");
        Equal("Bearer private-test-token", server.Authorization, "bearer header delivered");
        Equal("alpha", server.LabHeader, "private custom header delivered");
        True(server.SawHead, "download HEAD preflight issued");
        True(server.SawGet, "download GET issued");
    }

    private static async Task TestStrictResumeAsync(string directory)
    {
        var firmware = Encoding.UTF8.GetBytes("resume-fixture-with-a-deterministic-tail");
        var destination = Path.Combine(directory, "resumed-download.bin");
        var prefixLength = 11;
        await File.WriteAllBytesAsync(
            destination + ".part",
            firmware[..prefixLength]).ConfigureAwait(false);

        await using var server = new FakeDownloadServer(firmware, expectedRequests: 3);
        var serverTask = server.RunAsync();
        var result = await DownloadApp.RunAsync(
            DownloadOptions(destination, server.Port) with { Resume = true },
            new ConsoleUi(noColor: true),
            CancellationToken.None).ConfigureAwait(false);
        await serverTask.WaitAsync(TimeSpan.FromSeconds(5)).ConfigureAwait(false);

        Equal(ExitCode.Success, result, "strict resumed download result");
        Equal($"bytes={prefixLength}-", server.Range, "single strict Range request");
        True(
            (await File.ReadAllBytesAsync(destination).ConfigureAwait(false)).SequenceEqual(firmware),
            "resumed bytes reconstructed exact firmware");
    }

    private static async Task TestInfoFallbackDownloadAsync(string directory)
    {
        var firmware = Encoding.UTF8.GetBytes("legacy-info-fallback-firmware");
        var destination = Path.Combine(directory, "fallback-download.bin");
        await using var server = new FakeDownloadServer(
            firmware,
            fallbackToLegacyInfo: true,
            expectedRequests: 4);
        var serverTask = server.RunAsync();

        var result = await DownloadApp.RunAsync(
            DownloadOptions(destination, server.Port),
            QuietConsole(),
            CancellationToken.None).ConfigureAwait(false);
        await serverTask.WaitAsync(TimeSpan.FromSeconds(5)).ConfigureAwait(false);

        Equal(ExitCode.Success, result, "/info fallback download result");
        True(server.SawUpdateInfo, "/update/info attempted first");
        True(server.SawLegacyInfo, "/info fallback attempted after 404");
    }

    private static async Task TestDownloadHashMismatchAsync(string directory)
    {
        var firmware = Encoding.UTF8.GetBytes("hash-mismatch-download");
        var destination = Path.Combine(directory, "mismatch-download.bin");
        var original = Encoding.UTF8.GetBytes("do not replace me");
        await File.WriteAllBytesAsync(destination, original).ConfigureAwait(false);
        await using var server = new FakeDownloadServer(
            firmware,
            reportedHash: "00112233445566778899aabbccddeeff",
            expectedRequests: 2);
        var serverTask = server.RunAsync();

        var result = await DownloadApp.RunAsync(
            DownloadOptions(destination, server.Port),
            QuietConsole(),
            CancellationToken.None).ConfigureAwait(false);
        await serverTask.WaitAsync(TimeSpan.FromSeconds(5)).ConfigureAwait(false);

        Equal(ExitCode.DownloadIntegrity, result, "conflicting remote hash rejected");
        True(
            (await File.ReadAllBytesAsync(destination).ConfigureAwait(false)).SequenceEqual(original),
            "hash mismatch preserves prior destination");
    }

    private static async Task TestIgnoredRangeRejectedAsync(string directory)
    {
        var firmware = Encoding.UTF8.GetBytes("server-ignores-range-fixture");
        var destination = Path.Combine(directory, "ignored-range.bin");
        var partial = destination + ".part";
        await File.WriteAllBytesAsync(partial, firmware[..5]).ConfigureAwait(false);
        await using var server = new FakeDownloadServer(
            firmware,
            ignoreRange: true,
            expectedRequests: 3);
        var serverTask = server.RunAsync();

        var result = await DownloadApp.RunAsync(
            DownloadOptions(destination, server.Port) with { Resume = true },
            QuietConsole(),
            CancellationToken.None).ConfigureAwait(false);
        await serverTask.WaitAsync(TimeSpan.FromSeconds(5)).ConfigureAwait(false);

        Equal(ExitCode.DownloadIntegrity, result, "server ignoring Range is rejected");
        Equal(5L, new FileInfo(partial).Length, "ignored Range does not corrupt existing partial");
        True(!File.Exists(destination), "ignored Range does not create final destination");
    }

    private static UploaderOptions DownloadOptions(string destination, int port) =>
        new()
        {
            Mode = TransferMode.Download,
            DownloadPath = destination,
            UpdateApi = new Uri($"http://127.0.0.1:{port}/update"),
            ConnectTimeout = TimeSpan.FromSeconds(2),
            DownloadTimeout = TimeSpan.FromSeconds(5),
            NoColor = true,
        };

    private static ConsoleUi QuietConsole() =>
        new(
            noColor: true,
            new StringWriter(),
            new StringWriter(),
            stdoutRedirected: true,
            stderrRedirected: true,
            environmentAllowsColor: false);

    private static async Task TestConnectionFailureMappingAsync(string directory)
    {
        var firmware = Path.Combine(directory, "connection-failure.bin");
        await File.WriteAllBytesAsync(firmware, [1, 2, 3, 4]).ConfigureAwait(false);

        var portProbe = new TcpListener(IPAddress.Loopback, 0);
        portProbe.Start();
        var unusedPort = ((IPEndPoint)portProbe.LocalEndpoint).Port;
        portProbe.Stop();

        var endpointUri = new Uri($"http://127.0.0.1:{unusedPort}/update");
        var endpoint = new ResolvedEndpoint(endpointUri, endpointUri, null);
        var client = new UploadClient(TimeSpan.FromMilliseconds(250));
        await ThrowsAsync<UploadNetworkException>(
            () => client.UploadAsync(
                endpoint,
                firmware,
                "08d6c05a21512a79a1dfeb9d2a8f262f",
                TimeSpan.FromSeconds(2),
                new SynchronousProgress<(long Sent, long Total)>(_ => { }),
                CancellationToken.None),
            "connection cancellation maps to an upload/network failure").ConfigureAwait(false);

        try
        {
            await client.UploadAsync(
                endpoint,
                firmware,
                "08d6c05a21512a79a1dfeb9d2a8f262f",
                TimeSpan.FromSeconds(2),
                new SynchronousProgress<(long Sent, long Total)>(_ => { }),
                CancellationToken.None).ConfigureAwait(false);
            throw new InvalidOperationException("Expected the connection failure to be mapped.");
        }
        catch (UploadNetworkException exception)
        {
            True(exception.IsSafeToRetry, "dial failure remains safe for one DNS refresh retry");
        }
    }

    private static async Task TestCommittedUploadIsNotRetryableAsync(string directory)
    {
        var firmware = Path.Combine(directory, "committed-failure.bin");
        await File.WriteAllBytesAsync(firmware, new byte[64 * 1024]).ConfigureAwait(false);

        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var port = ((IPEndPoint)listener.LocalEndpoint).Port;
        var serverTask = Task.Run(async () =>
        {
            using var accepted = await listener.AcceptTcpClientAsync().ConfigureAwait(false);
            _ = await FakeFirmwareServer.ReadRequestAsync(accepted.GetStream()).ConfigureAwait(false);
            accepted.Client.LingerState = new LingerOption(enable: true, seconds: 0);
        });

        try
        {
            var uri = new Uri($"http://127.0.0.1:{port}/update");
            var client = new UploadClient(TimeSpan.FromSeconds(2));
            try
            {
                await client.UploadAsync(
                    new ResolvedEndpoint(uri, uri, null),
                    firmware,
                    Md5(await File.ReadAllBytesAsync(firmware).ConfigureAwait(false)),
                    TimeSpan.FromSeconds(5),
                    new SynchronousProgress<(long Sent, long Total)>(_ => { }),
                    CancellationToken.None).ConfigureAwait(false);
                throw new InvalidOperationException("Expected the reset upload to fail.");
            }
            catch (UploadNetworkException exception)
            {
                True(!exception.IsSafeToRetry, "started request body is never automatically replayed");
                True(
                    exception.Message.Contains("automatic replay was refused", StringComparison.Ordinal),
                    "ambiguous upload failure explains retry refusal");
            }

            await serverTask.WaitAsync(TimeSpan.FromSeconds(5)).ConfigureAwait(false);
        }
        finally
        {
            listener.Stop();
        }
    }

    private static async Task TestResponseSizeBoundAsync()
    {
        using var content = new ByteArrayContent(
            new byte[TransferLimits.MaximumResponseBytes + 1]);
        await ThrowsAsync<ResponseBodyTooLargeException>(
            () => UploadClient.ReadLimitedStringAsync(
                content,
                TransferLimits.MaximumResponseBytes,
                CancellationToken.None),
            "oversized HTTP response is rejected before buffering").ConfigureAwait(false);
    }

    private static async Task TestRedirectRefusedAsync(string directory)
    {
        var firmware = Path.Combine(directory, "redirect.bin");
        await File.WriteAllBytesAsync(firmware, [1, 2, 3, 4]).ConfigureAwait(false);

        var target = new TcpListener(IPAddress.Loopback, 0);
        target.Start();
        var targetPort = ((IPEndPoint)target.LocalEndpoint).Port;
        var redirect = new TcpListener(IPAddress.Loopback, 0);
        redirect.Start();
        var redirectPort = ((IPEndPoint)redirect.LocalEndpoint).Port;
        var serverTask = Task.Run(async () =>
        {
            using var accepted = await redirect.AcceptTcpClientAsync().ConfigureAwait(false);
            _ = await FakeFirmwareServer.ReadRequestAsync(accepted.GetStream()).ConfigureAwait(false);
            var response = Encoding.ASCII.GetBytes(
                "HTTP/1.1 307 Temporary Redirect\r\n" +
                $"Location: http://127.0.0.1:{targetPort}/update\r\n" +
                "Content-Length: 0\r\nConnection: close\r\n\r\n");
            await accepted.GetStream().WriteAsync(response).ConfigureAwait(false);
        });

        try
        {
            var uri = new Uri($"http://127.0.0.1:{redirectPort}/update");
            var client = new UploadClient(TimeSpan.FromSeconds(2));
            await ThrowsAsync<UploadServerException>(
                () => client.UploadAsync(
                    new ResolvedEndpoint(uri, uri, null),
                    firmware,
                    Md5([1, 2, 3, 4]),
                    TimeSpan.FromSeconds(5),
                    new SynchronousProgress<(long Sent, long Total)>(_ => { }),
                    CancellationToken.None),
                "HTTP redirect is returned to the caller instead of followed").ConfigureAwait(false);
            await serverTask.WaitAsync(TimeSpan.FromSeconds(5)).ConfigureAwait(false);
            await Task.Delay(50).ConfigureAwait(false);
            True(!target.Pending(), "redirect target received no firmware or credentials");
        }
        finally
        {
            redirect.Stop();
            target.Stop();
        }
    }

    private static async Task TestDownloadSizeBoundAsync()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var port = ((IPEndPoint)listener.LocalEndpoint).Port;
        var serverTask = Task.Run(async () =>
        {
            using var accepted = await listener.AcceptTcpClientAsync().ConfigureAwait(false);
            _ = await FakeFirmwareServer.ReadRequestAsync(accepted.GetStream()).ConfigureAwait(false);
            var response = Encoding.ASCII.GetBytes(
                "HTTP/1.1 200 OK\r\n" +
                $"Content-Length: {TransferLimits.MaximumFirmwareBytes + 1}\r\n" +
                "Accept-Ranges: bytes\r\nConnection: close\r\n\r\n");
            await accepted.GetStream().WriteAsync(response).ConfigureAwait(false);
        });

        try
        {
            var uri = new Uri($"http://127.0.0.1:{port}/firmware/download");
            var client = new DownloadClient(TimeSpan.FromSeconds(2));
            await ThrowsAsync<DownloadIntegrityException>(
                () => client.GetMetadataAsync(
                    new ResolvedEndpoint(uri, uri, null),
                    TimeSpan.FromSeconds(2),
                    CancellationToken.None),
                "oversized firmware download is rejected from HEAD metadata").ConfigureAwait(false);
            await serverTask.WaitAsync(TimeSpan.FromSeconds(5)).ConfigureAwait(false);
        }
        finally
        {
            listener.Stop();
        }
    }

    private static async Task TestSkipIdenticalUploadAsync(string directory)
    {
        var firmware = Path.Combine(directory, "skip-identical.bin");
        var installed = Encoding.UTF8.GetBytes("already installed raw fixture");
        var compressed = Gzip(installed);
        var compressedHash = Md5(compressed);
        var rawHash = Md5(installed);
        await File.WriteAllBytesAsync(firmware, compressed).ConfigureAwait(false);
        await File.WriteAllTextAsync(
            firmware + ".md5",
            $"{rawHash} *skip-identical.bin{Environment.NewLine}" +
            $"{compressedHash} *skip-identical.bin (compressed){Environment.NewLine}")
            .ConfigureAwait(false);

        await using var server = new FakeDownloadServer(installed, expectedRequests: 1);
        var serverTask = server.RunAsync();
        var options = new UploaderOptions
        {
            FirmwarePath = firmware,
            ManifestPath = firmware + ".md5",
            UpdateApi = new Uri($"http://127.0.0.1:{server.Port}/update"),
            ConnectTimeout = TimeSpan.FromSeconds(2),
            UploadTimeout = TimeSpan.FromSeconds(5),
            SkipIdentical = true,
            NoColor = true,
        };

        var result = await UploaderApp.RunAsync(
            options,
            new ConsoleUi(noColor: true),
            CancellationToken.None).ConfigureAwait(false);
        await serverTask.WaitAsync(TimeSpan.FromSeconds(5)).ConfigureAwait(false);
        Equal(ExitCode.Success, result, "identical installed firmware skips upload");
        True(server.SawUpdateInfo, "skip-identical uses device preflight hash");
        True(!server.SawGet, "skip-identical opens no firmware transfer");
    }

    private static string Md5(byte[] bytes) =>
        Convert.ToHexString(MD5.HashData(bytes)).ToLowerInvariant();

    private static byte[] Gzip(byte[] raw)
    {
        using var output = new MemoryStream();
        using (var gzip = new GZipStream(output, CompressionLevel.SmallestSize, leaveOpen: true))
        {
            gzip.Write(raw);
        }

        return output.ToArray();
    }

    private static void True(bool condition, string description)
    {
        assertions++;
        if (!condition)
        {
            throw new InvalidOperationException($"Assertion failed: {description}");
        }
    }

    private static void Equal<T>(T expected, T actual, string description)
    {
        assertions++;
        if (!EqualityComparer<T>.Default.Equals(expected, actual))
        {
            throw new InvalidOperationException(
                $"Assertion failed: {description}; expected {expected}, received {actual}.");
        }
    }

    private static async Task ThrowsAsync<TException>(Func<Task> action, string description)
        where TException : Exception
    {
        assertions++;
        try
        {
            await action().ConfigureAwait(false);
        }
        catch (TException)
        {
            return;
        }

        throw new InvalidOperationException($"Assertion failed: {description}");
    }
}

internal sealed class FakeFirmwareServer : IAsyncDisposable
{
    private readonly TcpListener listener;
    private readonly string expectedCompressedHash;
    private readonly string rawHash;

    public FakeFirmwareServer(string expectedCompressedHash, string rawHash)
    {
        this.expectedCompressedHash = expectedCompressedHash;
        this.rawHash = rawHash;
        listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        Port = ((IPEndPoint)listener.LocalEndpoint).Port;
    }

    public int Port { get; }
    public bool SawMd5Field { get; private set; }
    public bool SawFirmwareField { get; private set; }
    public bool SawCompressedHash { get; private set; }
    public string? UploadPath { get; private set; }
    public string? InfoPath { get; private set; }

    public async Task RunAsync()
    {
        using (var preflightClient = await listener.AcceptTcpClientAsync().ConfigureAwait(false))
        {
            var preflight = await ReadRequestAsync(preflightClient.GetStream()).ConfigureAwait(false);
            InfoPath = preflight.Path;
            await WriteResponseAsync(
                preflightClient.GetStream(),
                $$"""{"hash":"{{rawHash}}","size":1234}""").ConfigureAwait(false);
        }

        using (var uploadClient = await listener.AcceptTcpClientAsync().ConfigureAwait(false))
        {
            var upload = await ReadRequestAsync(uploadClient.GetStream()).ConfigureAwait(false);
            UploadPath = upload.Path;
            var body = Encoding.Latin1.GetString(upload.Body);
            SawMd5Field = body.Contains("name=MD5", StringComparison.Ordinal) ||
                          body.Contains("name=\"MD5\"", StringComparison.Ordinal);
            SawFirmwareField = body.Contains("name=firmware", StringComparison.Ordinal) ||
                               body.Contains("name=\"firmware\"", StringComparison.Ordinal);
            SawCompressedHash = body.Contains(expectedCompressedHash, StringComparison.Ordinal);
            await WriteResponseAsync(uploadClient.GetStream(), "ok!").ConfigureAwait(false);
        }

        using (var infoClient = await listener.AcceptTcpClientAsync().ConfigureAwait(false))
        {
            var info = await ReadRequestAsync(infoClient.GetStream()).ConfigureAwait(false);
            InfoPath = info.Path;
            await WriteResponseAsync(
                infoClient.GetStream(),
                $$"""{"hash":"{{rawHash}}","size":1234}""").ConfigureAwait(false);
        }
    }

    public ValueTask DisposeAsync()
    {
        listener.Stop();
        return ValueTask.CompletedTask;
    }

    internal static async Task<TestHttpRequest> ReadRequestAsync(NetworkStream stream)
    {
        var headerBytes = new List<byte>();
        var marker = new byte[] { 13, 10, 13, 10 };
        while (headerBytes.Count < 64 * 1024)
        {
            var buffer = new byte[1];
            var read = await stream.ReadAsync(buffer).ConfigureAwait(false);
            if (read == 0)
            {
                throw new IOException("Client disconnected before sending HTTP headers.");
            }

            headerBytes.Add(buffer[0]);
            if (headerBytes.Count >= marker.Length &&
                headerBytes.TakeLast(marker.Length).SequenceEqual(marker))
            {
                break;
            }
        }

        var headerText = Encoding.ASCII.GetString(headerBytes.ToArray());
        var lines = headerText.Split("\r\n", StringSplitOptions.RemoveEmptyEntries);
        var requestParts = lines[0].Split(' ', StringSplitOptions.RemoveEmptyEntries);
        var contentLength = 0;
        foreach (var line in lines.Skip(1))
        {
            var separator = line.IndexOf(':');
            if (separator > 0 &&
                line[..separator].Equals("Content-Length", StringComparison.OrdinalIgnoreCase))
            {
                contentLength = int.Parse(line[(separator + 1)..].Trim());
            }
        }

        var body = new byte[contentLength];
        var offset = 0;
        while (offset < body.Length)
        {
            var read = await stream.ReadAsync(body.AsMemory(offset)).ConfigureAwait(false);
            if (read == 0)
            {
                throw new IOException("Client disconnected before sending the complete HTTP body.");
            }

            offset += read;
        }

        var headers = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (var line in lines.Skip(1))
        {
            var separator = line.IndexOf(':');
            if (separator > 0)
            {
                headers[line[..separator].Trim()] = line[(separator + 1)..].Trim();
            }
        }

        return new TestHttpRequest(requestParts[0], requestParts[1], headers, body);
    }

    internal static async Task WriteResponseAsync(NetworkStream stream, string body)
    {
        var bodyBytes = Encoding.UTF8.GetBytes(body);
        var header = Encoding.ASCII.GetBytes(
            "HTTP/1.1 200 OK\r\n" +
            "Content-Type: text/plain; charset=utf-8\r\n" +
            $"Content-Length: {bodyBytes.Length}\r\n" +
            "Connection: close\r\n\r\n");
        await stream.WriteAsync(header).ConfigureAwait(false);
        await stream.WriteAsync(bodyBytes).ConfigureAwait(false);
        await stream.FlushAsync().ConfigureAwait(false);
    }
}

internal sealed class FakeDownloadServer : IAsyncDisposable
{
    private readonly TcpListener listener;
    private readonly byte[] firmware;
    private readonly string firmwareHash;
    private readonly string reportedHash;
    private readonly bool ignoreRange;
    private readonly bool fallbackToLegacyInfo;
    private readonly int expectedRequests;

    public FakeDownloadServer(
        byte[] firmware,
        string? reportedHash = null,
        bool ignoreRange = false,
        bool fallbackToLegacyInfo = false,
        int expectedRequests = 3)
    {
        this.firmware = firmware;
        firmwareHash = Convert.ToHexString(MD5.HashData(firmware)).ToLowerInvariant();
        this.reportedHash = reportedHash ?? firmwareHash;
        this.ignoreRange = ignoreRange;
        this.fallbackToLegacyInfo = fallbackToLegacyInfo;
        this.expectedRequests = expectedRequests;
        listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        Port = ((IPEndPoint)listener.LocalEndpoint).Port;
    }

    public int Port { get; }
    public bool SawHead { get; private set; }
    public bool SawGet { get; private set; }
    public string? Range { get; private set; }
    public string? Authorization { get; private set; }
    public string? LabHeader { get; private set; }
    public bool SawUpdateInfo { get; private set; }
    public bool SawLegacyInfo { get; private set; }

    public async Task RunAsync()
    {
        for (var count = 0; count < expectedRequests; count++)
        {
            using var client = await listener.AcceptTcpClientAsync().ConfigureAwait(false);
            var stream = client.GetStream();
            var request = await FakeFirmwareServer.ReadRequestAsync(stream).ConfigureAwait(false);
            request.Headers.TryGetValue("Authorization", out var authorization);
            request.Headers.TryGetValue("X-Lab", out var lab);
            Authorization ??= authorization;
            LabHeader ??= lab;

            if (request.Path == "/update/info")
            {
                SawUpdateInfo = true;
                if (fallbackToLegacyInfo)
                {
                    await WriteBytesAsync(
                        stream,
                        404,
                        "Not Found",
                        [],
                        new Dictionary<string, string>()).ConfigureAwait(false);
                    continue;
                }

                await FakeFirmwareServer.WriteResponseAsync(
                    stream,
                    $$"""{"hash":"{{reportedHash}}","size":{{firmware.Length}}}""").ConfigureAwait(false);
                continue;
            }

            if (request.Path == "/info")
            {
                SawLegacyInfo = true;
                await FakeFirmwareServer.WriteResponseAsync(
                    stream,
                    $"hostname: asa-device\r\nfirmware hash: {reportedHash}\r\n").ConfigureAwait(false);
                continue;
            }

            if (request.Path != "/firmware/download")
            {
                await WriteBytesAsync(
                    stream,
                    404,
                    "Not Found",
                    [],
                    new Dictionary<string, string>()).ConfigureAwait(false);
                continue;
            }

            var commonHeaders = new Dictionary<string, string>
            {
                ["Accept-Ranges"] = "bytes",
                ["Content-Type"] = "application/octet-stream",
                ["Content-MD5"] = Convert.ToBase64String(MD5.HashData(firmware)),
                ["X-Firmware-MD5"] = firmwareHash,
                ["ETag"] = $"\"{firmwareHash}\"",
            };

            if (request.Method == "HEAD")
            {
                SawHead = true;
                await WriteBytesAsync(
                    stream,
                    200,
                    "OK",
                    [],
                    commonHeaders,
                    declaredLength: firmware.Length).ConfigureAwait(false);
                continue;
            }

            SawGet = true;
            request.Headers.TryGetValue("Range", out var range);
            Range = range;
            if (range is not null && !ignoreRange)
            {
                var prefix = "bytes=";
                var dash = range.IndexOf('-');
                var start = long.Parse(range[prefix.Length..dash]);
                var body = firmware[(int)start..];
                var rangeHeaders = new Dictionary<string, string>(commonHeaders)
                {
                    ["Content-Range"] = $"bytes {start}-{firmware.Length - 1}/{firmware.Length}",
                };
                rangeHeaders.Remove("Content-MD5");
                rangeHeaders.Remove("X-Firmware-MD5");
                rangeHeaders.Remove("ETag");
                await WriteBytesAsync(
                    stream,
                    206,
                    "Partial Content",
                    body,
                    rangeHeaders).ConfigureAwait(false);
            }
            else
            {
                await WriteBytesAsync(stream, 200, "OK", firmware, commonHeaders)
                    .ConfigureAwait(false);
            }
        }
    }

    public ValueTask DisposeAsync()
    {
        listener.Stop();
        return ValueTask.CompletedTask;
    }

    private static async Task WriteBytesAsync(
        NetworkStream stream,
        int status,
        string reason,
        byte[] body,
        IReadOnlyDictionary<string, string> headers,
        int? declaredLength = null)
    {
        var header = new StringBuilder()
            .Append("HTTP/1.1 ")
            .Append(status)
            .Append(' ')
            .Append(reason)
            .Append("\r\n");
        foreach (var (name, value) in headers)
        {
            header.Append(name).Append(": ").Append(value).Append("\r\n");
        }

        header
            .Append("Content-Length: ")
            .Append(declaredLength ?? body.Length)
            .Append("\r\nConnection: close\r\n\r\n");
        await stream.WriteAsync(Encoding.ASCII.GetBytes(header.ToString())).ConfigureAwait(false);
        if (body.Length > 0)
        {
            await stream.WriteAsync(body).ConfigureAwait(false);
        }

        await stream.FlushAsync().ConfigureAwait(false);
    }
}

internal sealed record TestHttpRequest(
    string Method,
    string Path,
    IReadOnlyDictionary<string, string> Headers,
    byte[] Body);
