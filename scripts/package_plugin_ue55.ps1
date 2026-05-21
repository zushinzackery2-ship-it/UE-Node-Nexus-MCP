$ErrorActionPreference = "Stop"

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$PluginSource = Join-Path $ProjectRoot "bin\UeNodeNexusBridge"
$DistDir = Join-Path $ProjectRoot "bin\dist"
$ZipPath = Join-Path $DistDir "UeNodeNexusBridge-UE5.5-Win64.zip"

if (!(Test-Path (Join-Path $PluginSource "UeNodeNexusBridge.uplugin")))
{
    throw "Packaged plugin not found: $PluginSource"
}

New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

if (Test-Path $ZipPath)
{
    Remove-Item -LiteralPath $ZipPath -Force
}

Compress-Archive -Path $PluginSource -DestinationPath $ZipPath -CompressionLevel Optimal

Get-Item $ZipPath | Select-Object FullName, Length, LastWriteTime
