[CmdletBinding()]
param(
    [switch] $SkipTests
)

$ErrorActionPreference = 'Stop'
$previousCgo = $env:CGO_ENABLED
$previousOs = $env:GOOS
$previousArch = $env:GOARCH

function Invoke-GoCommand {
    param([Parameter(Mandatory)][string[]] $Arguments)

    & go @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "go $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
    }
}

try {
    if (-not $SkipTests) {
        Invoke-GoCommand @('test', '-count=10', './...')
        Invoke-GoCommand @('vet', './...')
    }

    $env:CGO_ENABLED = '0'
    $targets = @(
        @{
            Os = 'windows'
            Arch = 'amd64'
            Output = 'bin/win-x64/ASAFirmwareTransfer.exe'
        },
        @{
            Os = 'windows'
            Arch = '386'
            Output = 'bin/win-x86/ASAFirmwareTransfer.exe'
        },
        @{
            Os = 'linux'
            Arch = 'amd64'
            Output = 'bin/linux-x64/ASAFirmwareTransfer'
        }
    )

    foreach ($target in $targets) {
        $env:GOOS = $target.Os
        $env:GOARCH = $target.Arch
        $parent = Split-Path -Parent $target.Output
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
        Invoke-GoCommand @(
            'build',
            '-buildvcs=false',
            '-trimpath',
            '-ldflags',
            '-s -w',
            '-o',
            $target.Output,
            '.'
        )
    }

    Get-ChildItem -LiteralPath 'bin' -Recurse -File |
        Sort-Object FullName |
        ForEach-Object {
            $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash
            Write-Output "$($_.FullName)|$($_.Length)|$hash"
        }
}
finally {
    $env:CGO_ENABLED = $previousCgo
    $env:GOOS = $previousOs
    $env:GOARCH = $previousArch
}
