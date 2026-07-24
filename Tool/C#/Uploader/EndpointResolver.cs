using System.Net;
using System.Net.Sockets;

namespace AsaFirmwareTransfer;

internal sealed class EndpointResolver
{
    private readonly Uri original;
    private readonly TimeSpan timeout;
    private ResolvedEndpoint? cached;

    public EndpointResolver(Uri original, TimeSpan timeout)
    {
        this.original = original;
        this.timeout = timeout;
    }

    public bool UsesLocalDns =>
        original.Host.EndsWith(".local", StringComparison.OrdinalIgnoreCase);

    public async Task<ResolvedEndpoint> ResolveAsync(
        ConsoleUi console,
        bool forceRefresh,
        CancellationToken cancellationToken)
    {
        if (!forceRefresh && cached is not null)
        {
            return cached;
        }

        if (!UsesLocalDns || IPAddress.TryParse(original.Host, out _))
        {
            return cached = new ResolvedEndpoint(original, original, null);
        }

        console.Info($"{(forceRefresh ? "Re-resolving" : "Resolving")} {original.Host} once…");
        using var timeoutSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeoutSource.CancelAfter(timeout);

        IPAddress[] addresses;
        try
        {
            addresses = await Dns.GetHostAddressesAsync(original.DnsSafeHost, timeoutSource.Token)
                .ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            throw new HttpRequestException($"Timed out resolving {original.Host} after {timeout.TotalSeconds:0.#}s.");
        }
        catch (SocketException exception)
        {
            throw new HttpRequestException($"Could not resolve {original.Host}: {exception.Message}", exception);
        }

        var address =
            addresses.FirstOrDefault(candidate => candidate.AddressFamily == AddressFamily.InterNetwork) ??
            addresses.FirstOrDefault(candidate => candidate.AddressFamily == AddressFamily.InterNetworkV6);
        if (address is null)
        {
            throw new HttpRequestException($"{original.Host} did not resolve to an IPv4 or IPv6 address.");
        }

        var builder = new UriBuilder(original)
        {
            Host = address.ToString(),
        };
        var hostHeader = original.IsDefaultPort ? original.IdnHost : original.Authority;
        cached = new ResolvedEndpoint(original, builder.Uri, hostHeader);
        console.Success($"{original.Host} → {address}");
        return cached;
    }
}

internal sealed record ResolvedEndpoint(Uri Original, Uri Current, string? HostHeader)
{
    public ResolvedEndpoint Rebase(Uri target)
    {
        ArgumentNullException.ThrowIfNull(target);
        if (!Original.Scheme.Equals(target.Scheme, StringComparison.OrdinalIgnoreCase) ||
            !Original.Host.Equals(target.Host, StringComparison.OrdinalIgnoreCase) ||
            Original.Port != target.Port)
        {
            throw new InvalidOperationException(
                "A resolved endpoint can only be rebased within the same device origin.");
        }

        var builder = new UriBuilder(target)
        {
            Host = Current.Host,
        };
        var hostHeader = target.IsDefaultPort ? target.IdnHost : target.Authority;
        return new ResolvedEndpoint(target, builder.Uri, hostHeader);
    }

    public Uri UriAt(string absolutePath)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(absolutePath);
        var authority = new UriBuilder(Current.Scheme, Current.Host, Current.Port).Uri;
        return new Uri(authority, absolutePath.TrimStart('/'));
    }

    public Uri UpdateInfoUri => UriAt("/update/info");
    public Uri InfoUri => UriAt("/info");
}
