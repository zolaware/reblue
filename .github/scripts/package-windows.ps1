# Stages the shipping file list into one zip.
#
# Symbols go into a second zip. They exist so a crash log symbolizes, and
# nobody needs them to play.
#
# In: PRESET, OUT_FILE
$ErrorActionPreference = 'Stop'

if (-not $env:OUT_FILE) { throw "OUT_FILE not set" }
$buildDir = "out/build/$env:PRESET"

# Written by CMake alongside the header the self-installer compiles in, so the
# shipped-file list has one owner. It sits outside the build's own output dir
# because it is not one of the files it names.
$listPath = Join-Path $buildDir 'packaging/program_files.txt'
if (-not (Test-Path $listPath)) { throw "missing $listPath - configure the build first" }
$files = @(Get-Content $listPath | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })
$symbols = @($files | Where-Object { $_ -like '*.exe' } |
    ForEach-Object { [IO.Path]::ChangeExtension($_, '.pdb') })

function New-Package($relPaths, $zipPath) {
    $staging = Join-Path ([System.IO.Path]::GetTempPath()) "reblue-pkg"
    Remove-Item -Recurse -Force $staging -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $staging | Out-Null

    foreach ($rel in $relPaths) {
        $src = Join-Path $buildDir $rel
        if (-not (Test-Path $src)) { throw "missing required file: $src" }
        $dst = Join-Path $staging $rel
        New-Item -ItemType Directory -Force -Path (Split-Path $dst) | Out-Null
        Copy-Item $src $dst -Force
    }

    New-Item -ItemType Directory -Force -Path (Split-Path $zipPath) | Out-Null
    Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $zipPath -CompressionLevel Optimal
    Remove-Item -Recurse -Force $staging

    "Packaged $zipPath ({0:N1} MB)" -f ((Get-Item $zipPath).Length / 1MB) | Write-Host
}

New-Package $files (Join-Path "dist" $env:OUT_FILE)

# Derived rather than passed in. The nightly runs this script from the built
# branch against the workflow file on main, so a second input would arrive
# empty until both sides land.
$symbolsName = [IO.Path]::GetFileNameWithoutExtension($env:OUT_FILE) + "-symbols.zip"
New-Package $symbols (Join-Path "dist/symbols" $symbolsName)
