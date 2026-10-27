# Run this once with administrator rights to install usbipd-win and share
# only the USB Camera with hardware id 1bcf:2281. It does not attach it yet.
#Requires -RunAsAdministrator
$ErrorActionPreference = 'Stop'
$cameraSetupDirectory = Join-Path $PSScriptRoot 'artifacts'
New-Item -ItemType Directory -Path $cameraSetupDirectory -Force | Out-Null
$cameraResultPath = Join-Path $cameraSetupDirectory 'usb_setup_result.json'
try {
    $cameraUsbipdPath = 'C:\Program Files\usbipd-win\usbipd.exe'
    if (-not (Test-Path -LiteralPath $cameraUsbipdPath)) {
        $cameraMsiPath = Join-Path $env:TEMP 'usbipd-win_5.3.0_x64.msi'
        if (-not (Test-Path -LiteralPath $cameraMsiPath)) {
            Invoke-WebRequest -Uri 'https://github.com/dorssel/usbipd-win/releases/download/v5.3.0/usbipd-win_5.3.0_x64.msi' -OutFile $cameraMsiPath
        }
        $cameraMsiHash = (Get-FileHash -LiteralPath $cameraMsiPath -Algorithm SHA256).Hash
        if ($cameraMsiHash -ne '1C984914AEC944DE19B64EFF232421439629699F8138E3DDC29301175BC6D938') {
            throw 'The usbipd-win installer SHA256 does not match the reviewed winget manifest.'
        }
        if ((Get-AuthenticodeSignature -LiteralPath $cameraMsiPath).Status -ne 'Valid') {
            throw 'The usbipd-win installer signature is not valid.'
        }
        $cameraMsiLog = Join-Path $cameraSetupDirectory 'usbipd_install.log'
        $cameraInstallProcess = Start-Process -FilePath 'msiexec.exe' -ArgumentList @('/i', ('"' + $cameraMsiPath + '"'), '/qn', '/norestart', '/L*v', ('"' + $cameraMsiLog + '"')) -Wait -PassThru -WindowStyle Hidden
        if ($cameraInstallProcess.ExitCode -notin @(0, 3010)) {
            throw ('usbipd-win installation failed: ' + $cameraInstallProcess.ExitCode)
        }
        if ($cameraInstallProcess.ExitCode -eq 3010) {
            throw 'usbipd-win was installed, but Windows requests a restart. Restart before sharing the camera.'
        }
    }
    # --hardware-id refuses to choose if more than one matching device exists.
    & $cameraUsbipdPath bind --hardware-id '1bcf:2281'
    if ($LASTEXITCODE -ne 0) {
        throw 'Could not share the unique 1bcf:2281 camera. Inspect usbipd list and use its exact BUSID.'
    }
    @{ status = 'shared'; hardware_id = '1bcf:2281'; checked_at = (Get-Date).ToString('o') } | ConvertTo-Json | Set-Content -LiteralPath $cameraResultPath -Encoding utf8
} catch {
    @{ status = 'error'; message = $_.Exception.Message; checked_at = (Get-Date).ToString('o') } | ConvertTo-Json | Set-Content -LiteralPath $cameraResultPath -Encoding utf8
    exit 1
}
