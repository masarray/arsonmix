# Requires Visual Studio 2022 with Desktop development with C++, CMake 3.22+, JUCE 8/9,
# and Microsoft Edge WebView2 Runtime (the SDK is handled by recent JUCE/CMake toolchains).
param([Parameter(Mandatory=$true)][string]$JuceRoot)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
cmake -S $root -B "$root/build" -G "Visual Studio 17 2022" -A x64 "-DJUCE_ROOT=$JuceRoot"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build "$root/build" --config Release --target ARMixConsole --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host 'Build complete. Locate ARMixConsole.exe inside the build/ARMixConsole_artefacts/Release folder.'
