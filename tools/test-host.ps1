$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path $PSScriptRoot -Parent
$buildDir = Join-Path $repoRoot 'build\host'
$output = Join-Path $buildDir 'dgcalc-tests.exe'
$compiler = $env:HOST_CC
$compilerPrefix = @()

if ([string]::IsNullOrWhiteSpace($compiler)) {
    throw 'HOST_CC must name a C11 compiler or contain its absolute path.'
}

if (-not (Test-Path -LiteralPath $compiler)) {
    $command = Get-Command $compiler -ErrorAction SilentlyContinue
    if (-not $command) {
        throw "HOST_CC was not found: $compiler"
    }
    $compiler = $command.Source
}

if ([IO.Path]::GetFileName($compiler) -ieq 'zig.exe') {
    $compilerPrefix = @('cc')
}

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

if ($compilerPrefix -contains 'cc') {
    $env:ZIG_GLOBAL_CACHE_DIR = Join-Path $buildDir 'zig-global-cache'
    $env:ZIG_LOCAL_CACHE_DIR = Join-Path $buildDir 'zig-local-cache'
}

$sources = @()
$sourceRoot = Join-Path $repoRoot 'src'
if (Test-Path -LiteralPath $sourceRoot) {
    $ceRoot = Join-Path $sourceRoot 'platform\ce'
    $sources += Get-ChildItem -Path $sourceRoot -Filter '*.c' -Recurse |
        Where-Object {
            $_.FullName -ne (Join-Path $sourceRoot 'main.c') -and
            -not $_.FullName.StartsWith($ceRoot, [StringComparison]::OrdinalIgnoreCase)
        } |
        Select-Object -ExpandProperty FullName
}
$sources += Get-ChildItem -Path (Join-Path $repoRoot 'tests') -Filter '*.c' -Recurse |
    Select-Object -ExpandProperty FullName

$arguments = @(
    '-std=c11',
    '-Wall',
    '-Wextra',
    '-Werror',
    "-I$(Join-Path $repoRoot 'src')",
    "-I$(Join-Path $repoRoot 'tests')"
) + $sources + @('-o', $output)

& $compiler @compilerPrefix @arguments
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $output
exit $LASTEXITCODE
