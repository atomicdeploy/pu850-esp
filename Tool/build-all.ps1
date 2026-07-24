[CmdletBinding()]
param(
    [string] $Only = 'All',

    [switch] $Clean,

    [switch] $SkipRestore
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$toolRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$repoRoot = [IO.Path]::GetFullPath((Join-Path $toolRoot '..'))
$runningOnWindows = [Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT
$selected = [Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase
)
$requested = @(
    $Only.Split(',') |
        ForEach-Object { $_.Trim() } |
        Where-Object { $_.Length -gt 0 }
)
$allowed = @('All', 'Node', 'Go', 'CSharp', 'Cpp')
$invalid = @($requested | Where-Object { $_ -notin $allowed })
if ($invalid.Count -gt 0 -or $requested.Count -eq 0) {
    throw (
        "Invalid -Only value. Use a comma-separated subset of: {0}." -f
        ($allowed -join ', ')
    )
}

if ($requested -contains 'All') {
    foreach ($name in @('Node', 'Go', 'CSharp', 'Cpp')) {
        [void] $selected.Add($name)
    }
}
else {
    foreach ($name in $requested) {
        [void] $selected.Add($name)
    }
}

function Write-Step {
    param([Parameter(Mandatory)][string] $Message)

    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Assert-Executable {
    param(
        [Parameter(Mandatory)][string] $Name,
        [Parameter(Mandatory)][string] $InstallHint
    )

    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command '$Name' was not found. $InstallHint"
    }
}

function Invoke-Native {
    param(
        [Parameter(Mandatory)][string] $WorkingDirectory,
        [Parameter(Mandatory)][string] $FilePath,
        [Parameter()][string[]] $Arguments = @()
    )

    Push-Location -LiteralPath $WorkingDirectory
    try {
        & $FilePath @Arguments
        if ($LASTEXITCODE -ne 0) {
            throw "$FilePath $($Arguments -join ' ') failed with exit code $LASTEXITCODE."
        }
    }
    finally {
        Pop-Location
    }
}

function Remove-ScopedTree {
    param(
        [Parameter(Mandatory)][string] $Path,
        [Parameter(Mandatory)][string] $ExpectedParent
    )

    $candidate = [IO.Path]::GetFullPath($Path)
    $parent = [IO.Path]::GetFullPath($ExpectedParent)
    $prefix = $parent.TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar
    ) + [IO.Path]::DirectorySeparatorChar

    if (-not $candidate.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove a path outside '$parent': $candidate"
    }

    if (Test-Path -LiteralPath $candidate) {
        Remove-Item -LiteralPath $candidate -Recurse -Force
    }
}

$started = [Diagnostics.Stopwatch]::StartNew()
$completed = [Collections.Generic.List[string]]::new()

if ($selected.Contains('Node')) {
    Assert-Executable -Name 'npm' -InstallHint 'Install Node.js 20.19 or newer.'
    Write-Step 'Node: restore, syntax-check, and run loopback tests'
    if (-not $SkipRestore) {
        Invoke-Native -WorkingDirectory $toolRoot -FilePath 'npm' -Arguments @('ci')
    }
    Invoke-Native -WorkingDirectory $toolRoot -FilePath 'npm' -Arguments @('run', 'check')
    $completed.Add('Node') | Out-Null
}

if ($selected.Contains('Go')) {
    Assert-Executable -Name 'go' -InstallHint 'Install Go 1.22 or newer.'
    $goRoot = Join-Path $toolRoot 'Go'
    $goBinRoot = Join-Path $goRoot 'bin'
    $goOutputRoot = Join-Path $goBinRoot 'build-all'
    $goOutputName = if ($runningOnWindows) {
        'ASAFirmwareTransfer.exe'
    }
    else {
        'ASAFirmwareTransfer'
    }
    $goOutput = Join-Path $goOutputRoot $goOutputName

    if ($Clean) {
        Remove-ScopedTree -Path $goOutputRoot -ExpectedParent $goBinRoot
    }
    New-Item -ItemType Directory -Path $goOutputRoot -Force | Out-Null

    Write-Step 'Go: test ten passes, vet, and build with CGO disabled'
    Invoke-Native -WorkingDirectory $goRoot -FilePath 'go' -Arguments @(
        'test', '-count=10', './...'
    )
    Invoke-Native -WorkingDirectory $goRoot -FilePath 'go' -Arguments @(
        'vet', './...'
    )

    $previousCgo = $env:CGO_ENABLED
    try {
        $env:CGO_ENABLED = '0'
        Invoke-Native -WorkingDirectory $goRoot -FilePath 'go' -Arguments @(
            'build',
            '-buildvcs=false',
            '-trimpath',
            '-o',
            $goOutput,
            '.'
        )
    }
    finally {
        $env:CGO_ENABLED = $previousCgo
    }
    $completed.Add('Go') | Out-Null
}

if ($selected.Contains('CSharp')) {
    Assert-Executable -Name 'dotnet' -InstallHint 'Install the .NET 8 SDK.'
    $csharpRoot = Join-Path $toolRoot 'C#'
    $csharpProject = Join-Path (Join-Path $csharpRoot 'Uploader') 'Uploader.csproj'

    Write-Step '.NET: warning-as-error Release build and mock-device self-tests'
    if ($Clean) {
        Invoke-Native -WorkingDirectory $csharpRoot -FilePath 'dotnet' -Arguments @(
            'clean', $csharpProject, '-c', 'Release'
        )
    }
    if (-not $SkipRestore) {
        Invoke-Native -WorkingDirectory $csharpRoot -FilePath 'dotnet' -Arguments @(
            'restore', $csharpProject
        )
    }

    $buildArguments = @(
        'build', $csharpProject, '-c', 'Release', '--no-restore'
    )
    Invoke-Native -WorkingDirectory $csharpRoot -FilePath 'dotnet' -Arguments $buildArguments
    Invoke-Native -WorkingDirectory $csharpRoot -FilePath 'dotnet' -Arguments @(
        'run',
        '--project',
        $csharpProject,
        '-c',
        'Release',
        '--no-build',
        '--',
        '--self-test'
    )
    $completed.Add('CSharp') | Out-Null
}

if ($selected.Contains('Cpp')) {
    if (-not $runningOnWindows) {
        throw 'The native C++ client uses WinHTTP and its tests must run on Windows.'
    }

    Assert-Executable -Name 'cmake' -InstallHint (
        'Install CMake and Visual Studio with the Desktop development with C++ workload.'
    )
    Assert-Executable -Name 'python' -InstallHint (
        'Install Python 3 so the C++ loopback integration suite can run.'
    )
    $cppRoot = Join-Path $toolRoot 'C++'
    $cppBuildRoot = Join-Path $cppRoot 'build'
    $cppBuild = Join-Path $cppBuildRoot 'build-all'

    if ($Clean) {
        Remove-ScopedTree -Path $cppBuild -ExpectedParent $cppBuildRoot
    }

    Write-Step 'C++: configure x64, build native WinHTTP client, and run CTest'
    Invoke-Native -WorkingDirectory $repoRoot -FilePath 'cmake' -Arguments @(
        '-S', $cppRoot,
        '-B', $cppBuild,
        '-A', 'x64',
        '-DBUILD_TESTING=ON'
    )
    Invoke-Native -WorkingDirectory $repoRoot -FilePath 'cmake' -Arguments @(
        '--build', $cppBuild,
        '--config', 'Release',
        '--parallel'
    )
    Invoke-Native -WorkingDirectory $repoRoot -FilePath 'ctest' -Arguments @(
        '--test-dir', $cppBuild,
        '-C', 'Release',
        '--output-on-failure'
    )
    $completed.Add('Cpp') | Out-Null
}

$started.Stop()
$names = $completed -join ', '
$summary = (
    "All selected tooling checks passed ($names) in {0:n1}s. " +
    'No physical device was contacted.'
) -f $started.Elapsed.TotalSeconds
Write-Host $summary -ForegroundColor Green
