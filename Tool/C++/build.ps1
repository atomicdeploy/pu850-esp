[CmdletBinding()]
param(
    [ValidateSet('x64', 'Win32', 'all')]
    [string]$Architecture = 'all',
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$source = $PSScriptRoot
$architectures = if ($Architecture -eq 'all') { @('x64', 'Win32') } else { @($Architecture) }

foreach ($target in $architectures) {
    $build = Join-Path $source "build\$target"
    if ($Clean -and (Test-Path -LiteralPath $build)) {
        $resolved = [System.IO.Path]::GetFullPath($build)
        $expectedRoot = [System.IO.Path]::GetFullPath((Join-Path $source 'build'))
        if (-not $resolved.StartsWith($expectedRoot + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove unexpected build path: $resolved"
        }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }

    cmake -S $source -B $build -A $target -DBUILD_TESTING=ON
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed for $target." }

    cmake --build $build --config $Configuration --parallel
    if ($LASTEXITCODE -ne 0) { throw "CMake build failed for $target." }

    ctest --test-dir $build -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "CTest failed for $target." }

    $artifact = Join-Path $build "$Configuration\FirmwareTransferCpp.exe"
    if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) {
        throw "Expected artifact was not created: $artifact"
    }

    $destination = Join-Path $source "dist\$target"
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    Copy-Item -LiteralPath $artifact -Destination (Join-Path $destination 'FirmwareTransferCpp.exe') -Force
    Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $destination 'FirmwareTransferCpp.exe')
}
