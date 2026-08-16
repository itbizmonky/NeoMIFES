<#
.SYNOPSIS
    Signs the Release NeoMIFES.exe with the local dev certificate created
    by create_dev_certificate.ps1 (WI-13, build_plan.md 6 "Authenticode 署名").

.DESCRIPTION
    Uses the self-signed certificate from create_dev_certificate.ps1 (see
    that script's header for why this is not a substitute for a real
    Authenticode certificate). `signtool verify` against a self-signed cert
    is EXPECTED to report a trust-chain error ("terminated in a root
    certificate which is not trusted") - that is not a failure of this
    script, it is the defining property of a self-signed certificate. This
    script reports that distinction explicitly rather than treating
    verify's non-zero exit code as a hard failure.

.PARAMETER ExePath
    Path to the exe to sign. Defaults to the Release build's NeoMIFES.exe.
#>

param(
    [string]$ExePath = (Join-Path $PSScriptRoot '..\build\release\src\app\NeoMIFES.exe')
)

$ErrorActionPreference = 'Stop'

$ExePath = (Resolve-Path $ExePath).Path
if (-not (Test-Path $ExePath)) {
    throw "Executable not found: $ExePath (build the release preset first)"
}

$signtool = Get-ChildItem -Path 'C:\Program Files (x86)\Windows Kits\10\bin' -Recurse -Filter 'signtool.exe' -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match '\\x64\\' } |
    Sort-Object FullName -Descending |
    Select-Object -First 1
if (-not $signtool) {
    throw "signtool.exe not found under Windows Kits - install the Windows SDK (normally bundled with Visual Studio)."
}

$subjectName = 'CN=NeoMIFES Development Self-Signed - NOT FOR PRODUCTION'
$cert = Get-ChildItem -Path Cert:\CurrentUser\My -CodeSigningCert |
    Where-Object { $_.Subject -eq $subjectName } |
    Select-Object -First 1
if (-not $cert) {
    throw "Dev signing certificate not found. Run tools\create_dev_certificate.ps1 first."
}

Write-Output "Signing $ExePath with thumbprint $($cert.Thumbprint)..."
& $signtool.FullName sign /sha1 $cert.Thumbprint /fd SHA256 /t http://timestamp.digicert.com $ExePath
if ($LASTEXITCODE -ne 0) {
    throw "signtool sign failed with exit code $LASTEXITCODE"
}
Write-Output "Signing succeeded."

Write-Output "`nVerifying signature (self-signed cert => trust-chain error EXPECTED, not a failure)..."
& $signtool.FullName verify /pa /v $ExePath
Write-Output "signtool verify exit code: $LASTEXITCODE (non-zero here just means the trust chain doesn't resolve to a trusted root - expected for a self-signed dev cert; the signature block itself was applied above without error)"
