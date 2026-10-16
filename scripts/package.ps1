param(
  [Parameter(Mandatory=$true)][string]$PluginPath,
  [string]$OutputDirectory = './dist/package'
)
$ErrorActionPreference = 'Stop'
python "$PSScriptRoot/package_plugin.py" --plugin $PluginPath --output $OutputDirectory
if ($LASTEXITCODE -ne 0) { throw 'Native plugin validation or packaging failed.' }
