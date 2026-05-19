$ErrorActionPreference = "Stop"

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$PluginSource = Join-Path $ProjectRoot "bin\UeNodeNexusBridge"
$DistDir = Join-Path $ProjectRoot "bin\dist"
$ZipPath = Join-Path $DistDir "UeNodeNexusBridge-UE5.5-Win64.zip"
$MirrorPath = "G:\vdio\UEPlugins\MCP\UeNodeNexusBridge-UE5.5-Win64.zip"

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

$MirrorDir = Split-Path -Parent $MirrorPath
New-Item -ItemType Directory -Force -Path $MirrorDir | Out-Null
Copy-Item -LiteralPath $ZipPath -Destination $MirrorPath -Force

Get-Item $ZipPath, $MirrorPath | Select-Object FullName, Length, LastWriteTime
