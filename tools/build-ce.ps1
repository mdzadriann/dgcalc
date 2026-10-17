$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path $PSScriptRoot -Parent
$cedevHome = $env:CEDEV_HOME

if ([string]::IsNullOrWhiteSpace($cedevHome)) {
    throw 'CEDEV_HOME must contain the CEdev v15.0 installation path.'
}

$binDir = Join-Path $cedevHome 'bin'
$config = Join-Path $binDir 'cedev-config.exe'
$make = Join-Path $binDir 'make.exe'

if (-not (Test-Path -LiteralPath $config) -or -not (Test-Path -LiteralPath $make)) {
    throw "CEDEV_HOME is not a CEdev installation: $cedevHome"
}

$version = (& $config --version).Trim()
if ($version -ne 'v15.0') {
    throw "CEdev v15.0 is required; found $version"
}

$previousPath = $env:Path
try {
    $env:Path = "$binDir;$previousPath"
    Push-Location $repoRoot
    try {
        & $make
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }
    finally {
        Pop-Location
    }
}
finally {
    $env:Path = $previousPath
}

$program = Join-Path $repoRoot 'bin\DGCALC.8xp'
if (-not (Test-Path -LiteralPath $program)) {
    throw "CEdev completed without producing $program"
}

Get-Item -LiteralPath $program | Select-Object FullName, Length
