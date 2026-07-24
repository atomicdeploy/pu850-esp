using System.Text;

namespace AsaFirmwareTransfer;

internal static class Program
{
    public static async Task<int> Main(string[] args)
    {
        try
        {
            Console.OutputEncoding = new UTF8Encoding(encoderShouldEmitUTF8Identifier: false);
        }
        catch
        {
            // Some redirected hosts do not permit changing the encoding.
        }

        var parsed = UploaderOptions.Parse(
            args,
            Environment.GetEnvironmentVariable("UPDATE_API"),
            FirstEnvironment(
                "UPDATE_BEARER_TOKEN",
                "UPDATE_TOKEN",
                "ASA_FIRMWARE_BEARER",
                "ASA_FIRMWARE_BEARER_TOKEN",
                "FIRMWARE_BEARER_TOKEN"));
        if (parsed.ShowHelp)
        {
            UploaderOptions.WriteHelp(Console.Out);
            return (int)ExitCode.Success;
        }

        if (parsed.Error is not null)
        {
            Console.Error.WriteLine(ConsoleUi.Sanitize(parsed.Error));
            Console.Error.WriteLine();
            UploaderOptions.WriteHelp(Console.Error);
            return (int)ExitCode.Usage;
        }

        if (parsed.RunSelfTests)
        {
            return await SelfTests.RunAsync().ConfigureAwait(false);
        }

        var options = parsed.Options!;
        var console = new ConsoleUi(options.NoColor);

        using var shutdown = new CancellationTokenSource();
        using var watchStop = options.Watch ? new WatchStopController() : null;
        ConsoleCancelEventHandler cancelHandler = (_, eventArgs) =>
        {
            eventArgs.Cancel = true;
            if (watchStop is not null && watchStop.RequestStop())
            {
                console.Warning(
                    "Stop requested. The watcher will stop after any in-flight upload; " +
                    "press Ctrl+C again to cancel immediately.");
            }
            else
            {
                shutdown.Cancel();
            }
        };

        Console.CancelKeyPress += cancelHandler;
        try
        {
            var result = options.Mode == TransferMode.Download
                ? await DownloadApp.RunAsync(options, console, shutdown.Token).ConfigureAwait(false)
                : options.Watch
                    ? await WatchUploader.RunAsync(
                        options,
                        console,
                        watchStop!,
                        shutdown.Token).ConfigureAwait(false)
                    : await UploaderApp.RunAsync(options, console, shutdown.Token).ConfigureAwait(false);
            return (int)result;
        }
        catch (OperationCanceledException) when (shutdown.IsCancellationRequested)
        {
            console.ClearProgress();
            console.Warning("Firmware transfer cancelled.");
            return (int)ExitCode.Cancelled;
        }
        catch (Exception exception)
        {
            console.ClearProgress();
            console.Error($"Unexpected failure: {exception.Message}");
            if (options.Verbose)
            {
                Console.Error.WriteLine(ConsoleUi.Sanitize(exception.ToString()));
            }

            return (int)ExitCode.Unexpected;
        }
        finally
        {
            Console.CancelKeyPress -= cancelHandler;
        }
    }

    internal static string? FirstEnvironment(params string[] names)
    {
        foreach (var name in names)
        {
            var value = Environment.GetEnvironmentVariable(name);
            if (!string.IsNullOrWhiteSpace(value))
            {
                return value;
            }
        }

        return null;
    }
}
