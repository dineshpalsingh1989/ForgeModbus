param(
  [string]$Board = 'esp32:esp32:esp32',
  [string]$CoreVersion = '3.3.12',
  [switch]$Install
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$toolRoot = Join-Path $projectRoot '.toolchain'
$cli = Join-Path $toolRoot 'arduino-cli/arduino-cli.exe'
$config = Join-Path $toolRoot 'arduino-cli.yaml'
New-Item -ItemType Directory -Force $toolRoot | Out-Null

if ($Install) {
  if (!(Test-Path -LiteralPath $cli)) {
    $archive = Join-Path $toolRoot 'arduino-cli.zip'
    Invoke-WebRequest -Uri 'https://downloads.arduino.cc/arduino-cli/arduino-cli_latest_Windows_64bit.zip' -OutFile $archive -UseBasicParsing
    Expand-Archive -LiteralPath $archive -DestinationPath (Split-Path -Parent $cli) -Force
  }
}
if (!(Test-Path -LiteralPath $cli)) { throw 'Run tools/build-esp32.ps1 -Install first.' }
# Keep downloads, cores, tools, and sketchbook inside this workspace.
$dataPath = (Join-Path $toolRoot 'data').Replace('\', '/')
$downloadPath = (Join-Path $toolRoot 'downloads').Replace('\', '/')
$userPath = (Join-Path $toolRoot 'user').Replace('\', '/')
@"
board_manager:
  additional_urls:
    - https://espressif.github.io/arduino-esp32/package_esp32_index.json
directories:
  data: '$dataPath'
  downloads: '$downloadPath'
  user: '$userPath'
"@ | Set-Content -LiteralPath $config -Encoding UTF8
if ($Install) {
  & $cli core update-index --config-file $config
  if ($LASTEXITCODE -ne 0) { throw 'Board index download failed.' }
  & $cli core install "esp32:esp32@$CoreVersion" --config-file $config
  if ($LASTEXITCODE -ne 0) { throw 'ESP32 core installation failed.' }
}
# The Ethernet example uses the original ESP32's Ethernet MAC and clock pins.
$hasEmac = $Board -eq 'esp32:esp32:esp32'
$failed = @()
foreach ($example in Get-ChildItem -Directory (Join-Path $projectRoot 'examples')) {
  if ($example.Name -eq 'Ethernet' -and -not $hasEmac) { Write-Host "Skipping $($example.Name) on $Board"; continue }
  Write-Host "Compiling $($example.Name) for $Board"
  $buildPath = Join-Path $projectRoot "build/$($Board.Replace(':', '_'))/$($example.Name)"
  & $cli compile --config-file $config --fqbn $Board --library $projectRoot --warnings all --build-path $buildPath $example.FullName
  if ($LASTEXITCODE -ne 0) { $failed += $example.Name }
}
if ($failed) { throw "ESP32 compilation failed: $($failed -join ', ')" }
