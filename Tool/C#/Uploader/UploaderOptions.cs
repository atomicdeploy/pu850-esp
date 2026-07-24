using System.Globalization;

namespace AsaFirmwareTransfer;

internal enum TransferMode
{
    Upload,
    Download,
}

internal enum ExitCode
{
    Success = 0,
    Usage = 2,
    FirmwareValidation = 3,
    NetworkOrUpload = 4,
    UnexpectedResponse = 5,
    RebootTimeout = 6,
    FirmwareVerification = 7,
    Unexpected = 8,
    DownloadIntegrity = 9,
    LocalFile = 10,
    Cancelled = 130,
}

internal sealed record RequestHeader(string Name, string Value);

internal sealed record UploaderOptions
{
    public TransferMode Mode { get; init; } = TransferMode.Upload;
    public string FirmwarePath { get; init; } = string.Empty;
    public string ManifestPath { get; init; } = string.Empty;
    public string DownloadPath { get; init; } = string.Empty;
    public required Uri UpdateApi { get; init; }
    public Uri? DownloadApi { get; init; }
    public IReadOnlyList<RequestHeader> RequestHeaders { get; init; } = [];
    public TimeSpan ConnectTimeout { get; init; } = TimeSpan.FromSeconds(5);
    public TimeSpan UploadTimeout { get; init; } = TimeSpan.FromMinutes(2);
    public TimeSpan DownloadTimeout { get; init; } = TimeSpan.FromMinutes(2);
    public TimeSpan RebootTimeout { get; init; } = TimeSpan.FromSeconds(45);
    public TimeSpan PollInterval { get; init; } = TimeSpan.FromSeconds(1);
    public TimeSpan InitialPollDelay { get; init; } = TimeSpan.FromSeconds(2);
    public TimeSpan WatchDebounce { get; init; } = TimeSpan.FromSeconds(1);
    public TimeSpan WatchPollInterval { get; init; } = TimeSpan.FromMilliseconds(200);
    public bool Watch { get; init; }
    public bool Resume { get; init; }
    public bool SkipIdentical { get; init; }
    public bool Verify { get; init; } = true;
    public string? BackupPath { get; init; }
    public bool NoColor { get; init; }
    public bool Verbose { get; init; }

    public Uri EffectiveDownloadApi => DownloadApi ?? DeriveDownloadApi(UpdateApi);

    public static ParseResult Parse(
        string[] args,
        string? environmentUrl,
        string? environmentBearer = null)
    {
        ArgumentNullException.ThrowIfNull(args);

        if (args.Any(argument => argument is "-h" or "--help" or "/?"))
        {
            return ParseResult.Help();
        }

        if (args.Length == 1 && args[0] == "--self-test")
        {
            return ParseResult.SelfTest();
        }

        var mode = TransferMode.Upload;
        var explicitMode = false;
        var first = 0;
        if (args.Length > 0 && args[0].Equals("upload", StringComparison.OrdinalIgnoreCase))
        {
            explicitMode = true;
            first = 1;
        }
        else if (args.Length > 0 && args[0].Equals("download", StringComparison.OrdinalIgnoreCase))
        {
            mode = TransferMode.Download;
            explicitMode = true;
            first = 1;
        }

        string? positionalPath = null;
        string? manifestPath = null;
        string? downloadPath = null;
        string? cliUrl = null;
        string? downloadUrl = null;
        string? bearer = string.IsNullOrWhiteSpace(environmentBearer) ? null : environmentBearer;
        string? backupPath = null;
        var headers = new List<RequestHeader>();
        var connectTimeout = TimeSpan.FromSeconds(5);
        var uploadTimeout = TimeSpan.FromMinutes(2);
        var downloadTimeout = TimeSpan.FromMinutes(2);
        var rebootTimeout = TimeSpan.FromSeconds(45);
        var pollInterval = TimeSpan.FromSeconds(1);
        var initialPollDelay = TimeSpan.FromSeconds(2);
        var watchDebounce = TimeSpan.FromSeconds(1);
        var watchPollInterval = TimeSpan.FromMilliseconds(200);
        var watch = false;
        var once = false;
        var resume = false;
        var skipIdentical = false;
        var verify = true;
        var noColor = false;
        var verbose = false;

        for (var index = first; index < args.Length; index++)
        {
            var argument = args[index];
            switch (argument)
            {
                case "--once":
                    once = true;
                    break;
                case "--watch":
                    watch = true;
                    break;
                case "--resume":
                    resume = true;
                    break;
                case "--skip-identical":
                    skipIdentical = true;
                    break;
                case "--no-verify":
                    verify = false;
                    break;
                case "--no-color":
                    noColor = true;
                    break;
                case "-v":
                case "--verbose":
                    verbose = true;
                    break;
                case "-u":
                case "--url":
                    if (!TryTakeValue(args, ref index, argument, out cliUrl, out var urlError))
                    {
                        return ParseResult.Failure(urlError);
                    }
                    break;
                case "--download":
                    if (explicitMode && mode == TransferMode.Upload)
                    {
                        return ParseResult.Failure("--download cannot be combined with the upload command.");
                    }

                    mode = TransferMode.Download;
                    explicitMode = true;
                    if (!TryTakeValue(args, ref index, argument, out downloadPath, out var downloadError))
                    {
                        return ParseResult.Failure(downloadError);
                    }
                    break;
                case "--download-url":
                    if (!TryTakeValue(args, ref index, argument, out downloadUrl, out var downloadUrlError))
                    {
                        return ParseResult.Failure(downloadUrlError);
                    }
                    break;
                case "--backup":
                    if (!TryTakeValue(args, ref index, argument, out backupPath, out var backupError))
                    {
                        return ParseResult.Failure(backupError);
                    }
                    break;
                case "--bearer":
                    if (!TryTakeValue(args, ref index, argument, out bearer, out var bearerError))
                    {
                        return ParseResult.Failure(bearerError);
                    }
                    break;
                case "--header":
                    if (!TryTakeValue(args, ref index, argument, out var headerText, out var headerError))
                    {
                        return ParseResult.Failure(headerError);
                    }

                    if (!TryParseHeader(headerText!, out var header, out headerError))
                    {
                        return ParseResult.Failure(headerError);
                    }

                    headers.Add(header!);
                    break;
                case "--md5":
                    if (!TryTakeValue(args, ref index, argument, out manifestPath, out var manifestError))
                    {
                        return ParseResult.Failure(manifestError);
                    }
                    break;
                case "--connect-timeout":
                    if (!TryTakeSeconds(args, ref index, argument, allowZero: false, out connectTimeout, out var connectError))
                    {
                        return ParseResult.Failure(connectError);
                    }
                    break;
                case "--upload-timeout":
                    if (!TryTakeSeconds(args, ref index, argument, allowZero: false, out uploadTimeout, out var uploadError))
                    {
                        return ParseResult.Failure(uploadError);
                    }
                    break;
                case "--download-timeout":
                    if (!TryTakeSeconds(args, ref index, argument, allowZero: false, out downloadTimeout, out var transferError))
                    {
                        return ParseResult.Failure(transferError);
                    }
                    break;
                case "--reboot-timeout":
                    if (!TryTakeSeconds(args, ref index, argument, allowZero: false, out rebootTimeout, out var rebootError))
                    {
                        return ParseResult.Failure(rebootError);
                    }
                    break;
                case "--poll-interval":
                    if (!TryTakeSeconds(args, ref index, argument, allowZero: false, out pollInterval, out var pollError))
                    {
                        return ParseResult.Failure(pollError);
                    }
                    break;
                case "--initial-delay":
                    if (!TryTakeSeconds(args, ref index, argument, allowZero: true, out initialPollDelay, out var delayError))
                    {
                        return ParseResult.Failure(delayError);
                    }
                    break;
                case "--watch-debounce":
                    if (!TryTakeSeconds(args, ref index, argument, allowZero: false, out watchDebounce, out var debounceError))
                    {
                        return ParseResult.Failure(debounceError);
                    }
                    break;
                case "--watch-poll":
                    if (!TryTakeSeconds(args, ref index, argument, allowZero: false, out watchPollInterval, out var watchPollError))
                    {
                        return ParseResult.Failure(watchPollError);
                    }
                    break;
                default:
                    if (argument.StartsWith("--url=", StringComparison.Ordinal))
                    {
                        cliUrl = argument["--url=".Length..];
                    }
                    else if (argument.StartsWith("--download-url=", StringComparison.Ordinal))
                    {
                        downloadUrl = argument["--download-url=".Length..];
                    }
                    else if (argument.StartsWith("--md5=", StringComparison.Ordinal))
                    {
                        manifestPath = argument["--md5=".Length..];
                    }
                    else if (argument.StartsWith("-", StringComparison.Ordinal))
                    {
                        return ParseResult.Failure($"Unknown option: {argument}");
                    }
                    else if (positionalPath is null)
                    {
                        positionalPath = argument;
                    }
                    else
                    {
                        return ParseResult.Failure($"Unexpected argument: {argument}");
                    }
                    break;
            }
        }

        if (watch && once)
        {
            return ParseResult.Failure("--watch and --once cannot be used together.");
        }

        if (mode == TransferMode.Download && (watch || once || skipIdentical || backupPath is not null))
        {
            return ParseResult.Failure(
                "Download mode cannot be combined with upload/watch/backup/skip-identical options.");
        }

        if (mode == TransferMode.Upload && resume)
        {
            return ParseResult.Failure("--resume is only valid in download mode.");
        }

        if (watchPollInterval < TimeSpan.FromMilliseconds(10))
        {
            return ParseResult.Failure("--watch-poll must be at least 0.01 seconds.");
        }

        if (!string.IsNullOrEmpty(bearer))
        {
            if (bearer.Any(char.IsControl))
            {
                return ParseResult.Failure("The bearer token contains an invalid control character.");
            }

            if (headers.Any(item => item.Name.Equals("Authorization", StringComparison.OrdinalIgnoreCase)))
            {
                return ParseResult.Failure("--bearer and an Authorization --header cannot be combined.");
            }

            headers.Add(new RequestHeader("Authorization", $"Bearer {bearer}"));
        }

        Uri? explicitDownloadApi = null;
        if (!string.IsNullOrWhiteSpace(downloadUrl) &&
            !TryParseHttpUri(downloadUrl, out explicitDownloadApi))
        {
            return ParseResult.Failure("The download endpoint must be an absolute HTTP or HTTPS URL.");
        }

        var urlText = string.IsNullOrWhiteSpace(cliUrl) ? environmentUrl : cliUrl;
        Uri? updateApi = null;
        if (!string.IsNullOrWhiteSpace(urlText) && !TryParseHttpUri(urlText, out updateApi))
        {
            return ParseResult.Failure("The update endpoint must be an absolute HTTP or HTTPS URL.");
        }

        if (updateApi is null && explicitDownloadApi is not null)
        {
            updateApi = new Uri(explicitDownloadApi, "/update");
        }

        if (updateApi is null)
        {
            return ParseResult.Failure(
                "Please provide the device update endpoint with --url or UPDATE_API.");
        }

        var actualPath = downloadPath ?? positionalPath;
        if (string.IsNullOrWhiteSpace(actualPath))
        {
            return ParseResult.Failure(
                mode == TransferMode.Upload
                    ? "Please provide the compressed firmware file path."
                    : "Please provide the destination firmware file path.");
        }

        var fullPath = Path.GetFullPath(actualPath);
        var firmwarePath = mode == TransferMode.Upload ? fullPath : string.Empty;
        var fullDownloadPath = mode == TransferMode.Download ? fullPath : string.Empty;
        var fullManifestPath = mode == TransferMode.Upload
            ? Path.GetFullPath(string.IsNullOrWhiteSpace(manifestPath) ? fullPath + ".md5" : manifestPath)
            : string.Empty;

        return ParseResult.Success(new UploaderOptions
        {
            Mode = mode,
            FirmwarePath = firmwarePath,
            ManifestPath = fullManifestPath,
            DownloadPath = fullDownloadPath,
            UpdateApi = updateApi,
            DownloadApi = explicitDownloadApi,
            RequestHeaders = headers,
            ConnectTimeout = connectTimeout,
            UploadTimeout = uploadTimeout,
            DownloadTimeout = downloadTimeout,
            RebootTimeout = rebootTimeout,
            PollInterval = pollInterval,
            InitialPollDelay = initialPollDelay,
            WatchDebounce = watchDebounce,
            WatchPollInterval = watchPollInterval,
            Watch = watch,
            Resume = resume,
            SkipIdentical = skipIdentical,
            Verify = verify,
            BackupPath = string.IsNullOrWhiteSpace(backupPath) ? null : Path.GetFullPath(backupPath),
            NoColor = noColor,
            Verbose = verbose,
        });
    }

    public static Uri DeriveDownloadApi(Uri endpoint) =>
        new(endpoint, "/firmware/download");

    public static void WriteHelp(TextWriter writer)
    {
        writer.WriteLine("ASA Firmware Transfer");
        writer.WriteLine();
        writer.WriteLine("Usage:");
        writer.WriteLine("  AsaFirmwareTransfer upload <firmware.bin> --url http://device/update [options]");
        writer.WriteLine("  AsaFirmwareTransfer <firmware.bin> --url http://device/update [options]");
        writer.WriteLine("  AsaFirmwareTransfer download <output.bin> --url http://device/update [options]");
        writer.WriteLine();
        writer.WriteLine("UPDATE_API supplies the update URL; a CLI URL takes precedence.");
        writer.WriteLine("UPDATE_BEARER_TOKEN supplies the preferred optional bearer token.");
        writer.WriteLine("UPDATE_TOKEN and the legacy ASA/FIRMWARE bearer variables remain compatible.");
        writer.WriteLine();
        writer.WriteLine("Transfer options:");
        writer.WriteLine("  -u, --url URL              Device firmware update endpoint");
        writer.WriteLine("      --download-url URL     Override derived /firmware/download endpoint");
        writer.WriteLine("      --download FILE        Download mode (alternative to download command)");
        writer.WriteLine("      --resume               Resume one existing .part with a strict Range request");
        writer.WriteLine("      --bearer TOKEN         Add Authorization: Bearer TOKEN (never logged)");
        writer.WriteLine("      --header NAME:VALUE    Add a private request header; repeat as needed");
        writer.WriteLine("      --connect-timeout SEC  DNS/connect timeout (default: 5)");
        writer.WriteLine("      --download-timeout SEC Whole download timeout (default: 120)");
        writer.WriteLine("      --no-verify            Disable remote-hash verification (not recommended)");
        writer.WriteLine();
        writer.WriteLine("Upload options:");
        writer.WriteLine("      --md5 FILE             Manifest path (default: <firmware>.md5)");
        writer.WriteLine("      --backup FILE          Verified firmware backup before upload");
        writer.WriteLine("      --skip-identical       Exit successfully when the device already has this hash");
        writer.WriteLine("      --upload-timeout SEC   Whole upload timeout (default: 120)");
        writer.WriteLine("      --reboot-timeout SEC   Post-upload verification deadline (default: 45)");
        writer.WriteLine("      --poll-interval SEC    Verification retry interval (default: 1)");
        writer.WriteLine("      --initial-delay SEC    Delay before post-upload polling (default: 2)");
        writer.WriteLine("      --watch                Upload current stable build, then watch continuously");
        writer.WriteLine("      --watch-debounce SEC   Required unchanged time in watch mode (default: 1)");
        writer.WriteLine("      --watch-poll SEC       Watch fallback polling interval (default: 0.2)");
        writer.WriteLine("      --once                 Explicit one-shot compatibility mode");
        writer.WriteLine();
        writer.WriteLine("General:");
        writer.WriteLine("      --no-color             Disable ANSI color and animated progress");
        writer.WriteLine("  -v, --verbose              Show retry details");
        writer.WriteLine("  -h, --help                 Show this help");
        writer.WriteLine();
        writer.WriteLine("Exit codes: 0 success, 2 usage, 3 firmware/manifest validation,");
        writer.WriteLine("4 network/upload, 5 unexpected API response, 6 reboot timeout,");
        writer.WriteLine("7 remote firmware verification, 8 unexpected, 9 download integrity,");
        writer.WriteLine("10 local destination failure, 130 cancelled.");
    }

    private static bool TryParseHttpUri(string text, out Uri? uri)
    {
        if (Uri.TryCreate(text, UriKind.Absolute, out uri) &&
            uri.Scheme is "http" or "https" &&
            !string.IsNullOrWhiteSpace(uri.Host) &&
            string.IsNullOrEmpty(uri.UserInfo) &&
            string.IsNullOrEmpty(uri.Fragment))
        {
            return true;
        }

        uri = null;
        return false;
    }

    private static bool TryParseHeader(
        string text,
        out RequestHeader? header,
        out string error)
    {
        var separator = text.IndexOf(':');
        var name = separator > 0 ? text[..separator].Trim() : string.Empty;
        var value = separator > 0 ? text[(separator + 1)..].Trim() : string.Empty;
        if (name.Length == 0 || value.Length == 0 ||
            name.Any(character =>
                !(char.IsAsciiLetterOrDigit(character) || character is '!' or '#' or '$' or '%' or '&' or
                    '\'' or '*' or '+' or '-' or '.' or '^' or '_' or '`' or '|' or '~')) ||
            value.Any(char.IsControl))
        {
            header = null;
            error = "--header requires a valid NAME:VALUE without control characters.";
            return false;
        }

        string[] managedHeaders =
        [
            "Host", "Content-Length", "Transfer-Encoding", "Range", "Content-MD5", "Expect",
        ];
        if (managedHeaders.Contains(name, StringComparer.OrdinalIgnoreCase))
        {
            header = null;
            error = $"Header {name} is managed by ASA Firmware Transfer and cannot be overridden.";
            return false;
        }

        header = new RequestHeader(name, value);
        error = string.Empty;
        return true;
    }

    private static bool TryTakeValue(
        IReadOnlyList<string> args,
        ref int index,
        string option,
        out string? value,
        out string error)
    {
        if (index + 1 >= args.Count || string.IsNullOrWhiteSpace(args[index + 1]))
        {
            value = null;
            error = $"{option} requires a value.";
            return false;
        }

        value = args[++index];
        error = string.Empty;
        return true;
    }

    private static bool TryTakeSeconds(
        IReadOnlyList<string> args,
        ref int index,
        string option,
        bool allowZero,
        out TimeSpan value,
        out string error)
    {
        if (!TryTakeValue(args, ref index, option, out var text, out error))
        {
            value = default;
            return false;
        }

        if (!double.TryParse(text, NumberStyles.AllowDecimalPoint, CultureInfo.InvariantCulture, out var seconds) ||
            !double.IsFinite(seconds) ||
            seconds < 0 ||
            (!allowZero && seconds == 0))
        {
            value = default;
            error = $"{option} must be a {(allowZero ? "non-negative" : "positive")} number of seconds.";
            return false;
        }

        try
        {
            value = TimeSpan.FromSeconds(seconds);
            if (!allowZero && value <= TimeSpan.Zero)
            {
                error = $"{option} is below the supported timer resolution.";
                return false;
            }

            return true;
        }
        catch (OverflowException)
        {
            value = default;
            error = $"{option} is too large.";
            return false;
        }
    }
}

internal sealed record ParseResult(
    UploaderOptions? Options,
    string? Error,
    bool ShowHelp,
    bool RunSelfTests)
{
    public static ParseResult Success(UploaderOptions options) => new(options, null, false, false);
    public static ParseResult Failure(string error) => new(null, error, false, false);
    public static ParseResult Help() => new(null, null, true, false);
    public static ParseResult SelfTest() => new(null, null, false, true);
}
