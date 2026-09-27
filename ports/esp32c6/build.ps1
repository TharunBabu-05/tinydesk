# build.ps1 - build (and optionally flash) the tinydesk firmware on Windows.
#
#   .\build.ps1                 build
#   .\build.ps1 -Port COM3      build and flash (close PuTTY first!)
#
# Uses the ESP-IDF 5.3.1 installation registered with the Espressif
# installer (C:\Espressif). Pass -IdfId if you have several.
param(
    [string]$Port = "",
    [string]$IdfId = ""
)
$ErrorActionPreference = "Stop"

if (-not $env:IDF_PATH) {
    if ($IdfId -eq "") {
        $IdfId = (Get-Content C:\Espressif\esp_idf.json | ConvertFrom-Json).idfSelectedId
    }
    . C:\Espressif\Initialize-Idf.ps1 -IdfId $IdfId | Out-Null
}

Set-Location $PSScriptRoot
if (-not (Test-Path sdkconfig)) { idf.py set-target esp32c6 }
idf.py build
if ($Port -ne "") { idf.py -p $Port flash }
