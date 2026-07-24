[CmdletBinding()]
param(
    [switch] $Clean
)

$ErrorActionPreference = 'Stop'
$projectDirectory = Join-Path $PSScriptRoot 'Uploader'
$project = Join-Path $projectDirectory 'Uploader.csproj'
$publishRoot = Join-Path $projectDirectory 'publish'

if ($Clean) {
    foreach ($candidate in @(
        (Join-Path $projectDirectory 'bin'),
        (Join-Path $projectDirectory 'obj'),
        $publishRoot
    )) {
        $resolvedParent = [IO.Path]::GetFullPath($projectDirectory)
        $resolvedCandidate = [IO.Path]::GetFullPath($candidate)
        if (-not $resolvedCandidate.StartsWith(
            $resolvedParent + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase
        )) {
            throw "Refusing to remove path outside the project: $resolvedCandidate"
        }

        if (Test-Path -LiteralPath $resolvedCandidate) {
            Remove-Item -LiteralPath $resolvedCandidate -Recurse -Force
        }
    }
}

dotnet build $project -c Release
if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }

dotnet run --project $project -c Release --no-build -- --self-test
if ($LASTEXITCODE -ne 0) { throw 'Self-tests failed.' }

dotnet publish $project -c Release -r win-x64 --self-contained true `
    -p:PublishSingleFile=true -p:PublishTrimmed=true `
    -o (Join-Path $publishRoot 'win-x64')
if ($LASTEXITCODE -ne 0) { throw 'win-x64 publish failed.' }

dotnet publish $project -c Release --self-contained false `
    -p:UseAppHost=false -o (Join-Path $publishRoot 'portable')
if ($LASTEXITCODE -ne 0) { throw 'portable publish failed.' }

dotnet publish $project -c Release -r linux-x64 --self-contained true `
    -p:PublishSingleFile=true -p:PublishTrimmed=true `
    -o (Join-Path $publishRoot 'linux-x64')
if ($LASTEXITCODE -ne 0) { throw 'linux-x64 publish failed.' }

$artifacts = @(
    (Join-Path $publishRoot 'win-x64\AsaFirmwareTransfer.exe'),
    (Join-Path $publishRoot 'portable\AsaFirmwareTransfer.dll'),
    (Join-Path $publishRoot 'linux-x64\AsaFirmwareTransfer')
)
$hashLines = foreach ($artifact in $artifacts) {
    if (-not (Test-Path -LiteralPath $artifact)) {
        throw "Expected artifact was not produced: $artifact"
    }

    $hash = (Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $($artifact.Substring($publishRoot.Length + 1).Replace('\', '/'))"
}

$hashFile = Join-Path $publishRoot 'SHA256SUMS.txt'
$hashLines | Set-Content -LiteralPath $hashFile -Encoding ascii
$hashLines | ForEach-Object { Write-Host $_ }
Write-Host "Published hash manifest: $hashFile"
