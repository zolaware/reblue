# Imports the Visual Studio developer environment into the job. The presets
# drive clang through Ninja, which has no developer environment of its own.
#
# In: GITHUB_ENV
$ErrorActionPreference = 'Stop'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$install = & $vswhere -latest -products * `
  -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
  -property installationPath
if (-not $install) { throw "no Visual Studio install carrying the x64 C++ toolset" }

Import-Module (Join-Path $install 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $install -SkipAutomaticLocation `
  -DevCmdArguments '-arch=x64 -host_arch=x64'

# The presets name bare 'clang'/'clang++', so PATH alone picks the compiler -
# and the image's standalone LLVM (Program Files\LLVM) sits ahead of the VS
# tools. That LLVM lags this VS install's C++ STL, which static_asserts
# (STL1000) against clang releases older than the one Microsoft ships beside it.
# Put this install's own clang first: compiler and STL can then never drift.
$bundled = Join-Path $install 'VC\Tools\Llvm\x64\bin'
foreach ($driver in 'clang.exe', 'clang++.exe') {
  if (-not (Test-Path (Join-Path $bundled $driver))) {
    throw "Visual Studio at $install has no $driver in $bundled; the presets need " +
      'both clang drivers from one install that matches its STL'
  }
}
$env:PATH = "$bundled;$env:PATH"
if ($env:GITHUB_PATH) {
  # Belt and braces: the runner prepends GITHUB_PATH entries for later steps,
  # so the choice survives even if the exported PATH above is re-based.
  $bundled | Out-File $env:GITHUB_PATH -Encoding utf8 -Append
}

foreach ($item in Get-ChildItem env:) {
  if ($item.Name -match '^(GITHUB_|RUNNER_|ACTIONS_)') { continue }
  if ($item.Value -match '[\r\n]') { continue }
  "$($item.Name)=$($item.Value)" | Out-File $env:GITHUB_ENV -Encoding utf8 -Append
}

Write-Host "Visual Studio: $install"
Write-Host "WindowsSdkDir: $env:WindowsSdkDir $env:WindowsSDKVersion"
& (Join-Path $bundled 'clang.exe') --version | Select-Object -First 1
