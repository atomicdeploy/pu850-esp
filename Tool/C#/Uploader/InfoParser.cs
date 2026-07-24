using System.Text.Json;

namespace AsaFirmwareTransfer;

internal static class InfoParser
{
    public static IReadOnlyDictionary<string, string> Parse(string content)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        if (string.IsNullOrWhiteSpace(content))
        {
            return values;
        }

        var trimmed = content.TrimStart();
        if (trimmed.StartsWith('{'))
        {
            try
            {
                using var document = JsonDocument.Parse(content);
                FlattenJson(document.RootElement, prefix: null, values);
                return values;
            }
            catch (JsonException)
            {
                // Fall through to the firmware's traditional key: value parser.
            }
        }

        foreach (var line in content.Split(["\r\n", "\n", "\r"], StringSplitOptions.RemoveEmptyEntries))
        {
            var separator = line.IndexOf(':');
            if (separator <= 0)
            {
                continue;
            }

            var key = line[..separator].Trim();
            var value = line[(separator + 1)..].Trim();
            if (key.Length > 0 && value.Length > 0)
            {
                values[key] = value;
            }
        }

        return values;
    }

    public static bool TryGetFirmwareHash(
        IReadOnlyDictionary<string, string> values,
        out string firmwareHash)
        => TryGetFirmwareHash(values, allowGenericHash: false, out firmwareHash);

    public static bool TryGetDeviceFirmwareHash(
        IReadOnlyDictionary<string, string> values,
        out string firmwareHash)
        => TryGetFirmwareHash(values, allowGenericHash: true, out firmwareHash);

    private static bool TryGetFirmwareHash(
        IReadOnlyDictionary<string, string> values,
        bool allowGenericHash,
        out string firmwareHash)
    {
        foreach (var (key, value) in values)
        {
            var leafKey = key.Split('.').LastOrDefault() ?? key;
            var normalized = new string(leafKey.Where(char.IsLetterOrDigit).ToArray()).ToLowerInvariant();
            if (normalized is not ("firmwarehash" or "firmwaremd5") &&
                !(allowGenericHash && normalized == "hash"))
            {
                continue;
            }

            var candidate = value.Trim();
            if (candidate.Length != 32 || candidate.Any(character => !IsAsciiHex(character)))
            {
                continue;
            }

            firmwareHash = candidate.ToLowerInvariant();
            return true;
        }

        firmwareHash = string.Empty;
        return false;
    }

    private static bool IsAsciiHex(char value) =>
        value is >= '0' and <= '9' or >= 'a' and <= 'f' or >= 'A' and <= 'F';

    private static void FlattenJson(
        JsonElement element,
        string? prefix,
        IDictionary<string, string> destination)
    {
        switch (element.ValueKind)
        {
            case JsonValueKind.Object:
                foreach (var property in element.EnumerateObject())
                {
                    var key = prefix is null ? property.Name : $"{prefix}.{property.Name}";
                    FlattenJson(property.Value, key, destination);
                }
                break;
            case JsonValueKind.String:
                if (prefix is not null)
                {
                    destination[prefix] = element.GetString() ?? string.Empty;
                }
                break;
            case JsonValueKind.Number:
            case JsonValueKind.True:
            case JsonValueKind.False:
                if (prefix is not null)
                {
                    destination[prefix] = element.ToString();
                }
                break;
        }
    }
}
