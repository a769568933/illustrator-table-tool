param(
  [Parameter(Mandatory = $true)][string]$SdkRoot,
  [ValidateSet('Release','Debug')][string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$resolvedSdk = (Resolve-Path -LiteralPath $SdkRoot).Path
if (-not (Test-Path -LiteralPath (Join-Path $resolvedSdk 'illustratorapi/illustrator/AIPlugin.h'))) {
  throw 'Illustrator 2024 C++ SDK is missing or the SDK root is incorrect.'
}
& cmake -S $projectRoot -B (Join-Path $projectRoot 'build/native') -G 'Visual Studio 17 2022' -A x64 -T v142 "-DILLUSTRATOR_SDK_ROOT=$resolvedSdk"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& cmake --build (Join-Path $projectRoot 'build/native') --config $Configuration --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Native plugin compilation failed.' }

