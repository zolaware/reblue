# Downloads the win-amd64 slice of a rexglue-sdk release into .\sdk\win-amd64.
#
# In: SDK_REPO, SDK_TAG, GH_TOKEN, ASSETS_REPO, ASSETS_TOKEN
$ErrorActionPreference = 'Stop'

New-Item -ItemType Directory -Force -Path sdk-dl | Out-Null
gh release download $env:SDK_TAG --repo $env:SDK_REPO --pattern "*win-amd64*.zip" --dir sdk-dl

$zip = Get-ChildItem sdk-dl -Filter *.zip | Select-Object -First 1
if (-not $zip) { throw "$env:SDK_REPO $env:SDK_TAG publishes no win-amd64 asset" }

Expand-Archive -Path $zip.FullName -DestinationPath sdk
if (-not (Test-Path sdk/win-amd64)) { throw "SDK archive has no win-amd64/ directory" }
Write-Host "Extracted $($zip.Name) to sdk/win-amd64"

# Same check as fetch-deps.sh, for the same reason: find_package(rexglue 0.10.0
# CONFIG) is not EXACT, so an SDK that renamed rex::runtime/rex::o1heap passes the
# version gate and fails later in the link step. See fetch-deps.sh for the names
# each side of that rename.
$cmakeDir = Join-Path (Join-Path (Join-Path sdk 'win-amd64') 'lib\cmake') 'rexglue'
if (-not (Test-Path $cmakeDir)) { throw "$env:SDK_REPO $env:SDK_TAG has no win-amd64/lib/cmake/rexglue, so its exported targets cannot be checked" }
foreach ($target in 'rex::runtime', 'rex::o1heap') {
    $definition = "add_library\($target (SHARED|STATIC|INTERFACE) IMPORTED\)"
    if (-not (Select-String -Path (Join-Path $cmakeDir '*.cmake') -Pattern $definition -Quiet)) {
        $exported = Select-String -Path (Join-Path $cmakeDir '*.cmake') -Pattern 'add_library\(rex::[a-z0-9_-]+ ' -AllMatches |
            ForEach-Object { $_.Matches.Value } | Sort-Object -Unique
        throw "$env:SDK_REPO $env:SDK_TAG does not export $target, which this tree links; the SDK changed its exported targets. Last release that still exports them: nightly-20261002-bd833a2a. Pin that in the workflow's sdk_ref input, or move this tree to the new names. This SDK exports: $($exported -join ' ')"
    }
}
Write-Host "$env:SDK_TAG exports rex::runtime and rex::o1heap"

if (Test-Path assets) { Remove-Item -Recurse -Force assets }
git clone --depth 1 "https://x-access-token:$($env:ASSETS_TOKEN)@github.com/$($env:ASSETS_REPO).git" assets
if ($LASTEXITCODE -ne 0) { throw "Cloning $env:ASSETS_REPO failed" }
git -C assets remote set-url origin "https://github.com/$($env:ASSETS_REPO).git"
Write-Host "Cloned $env:ASSETS_REPO"
