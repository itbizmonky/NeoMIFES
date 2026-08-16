<#
.SYNOPSIS
    Creates a self-signed code-signing certificate for local NeoMIFES
    Release builds (WI-13, build_plan.md 6 "Authenticode 署名").

.DESCRIPTION
    This is NOT a substitute for a real Authenticode certificate from a
    trusted CA. A self-signed certificate signs a binary in a way that is
    cryptographically valid but is not trusted by Windows SmartScreen or
    any other machine by default (the certificate has no chain to a
    trusted root). Its purpose here is narrower: prove the SIGNING
    MECHANISM itself (signtool invocation, the packaging script's use of
    it) works end-to-end, before a real certificate is purchased. See
    docs/issues/authenticode_certificate_not_yet_acquired.md for the
    real-certificate follow-up.

    Idempotent: if a certificate with the expected subject already exists
    in the current user's certificate store, this script reuses it instead
    of creating a duplicate.

.NOTES
    Run once per development machine. The resulting .pfx (private key,
    password-protected) is written OUTSIDE the repository's tracked tree
    is not possible for a script that must live under tools/, so instead
    the .pfx is written under tools/ but excluded via .gitignore (*.pfx) -
    never commit it. The .cer (public key only, no secret) is safe to
    commit and is written alongside it for reference.
#>

$ErrorActionPreference = 'Stop'

$subjectName = 'CN=NeoMIFES Development Self-Signed - NOT FOR PRODUCTION'
$toolsDir    = $PSScriptRoot
$pfxPath     = Join-Path $toolsDir 'neomifes_dev_signing.pfx'
$cerPath     = Join-Path $toolsDir 'neomifes_dev_signing.cer'
# Password only protects the LOCAL .pfx file at rest; it is not a secret
# shared with anyone and is fine to keep in this script (this cert is
# explicitly not-for-production, see header comment).
$pfxPassword = ConvertTo-SecureString -String 'neomifes-dev-only' -Force -AsPlainText

$existing = Get-ChildItem -Path Cert:\CurrentUser\My -CodeSigningCert |
    Where-Object { $_.Subject -eq $subjectName } |
    Select-Object -First 1

if ($existing) {
    Write-Output "Reusing existing certificate: Thumbprint=$($existing.Thumbprint)"
    $cert = $existing
} else {
    $cert = New-SelfSignedCertificate `
        -Type CodeSigningCert `
        -Subject $subjectName `
        -CertStoreLocation Cert:\CurrentUser\My `
        -KeyUsage DigitalSignature `
        -KeyAlgorithm RSA `
        -KeyLength 2048 `
        -NotAfter (Get-Date).AddYears(3)
    Write-Output "Created new certificate: Thumbprint=$($cert.Thumbprint)"
}

Export-PfxCertificate -Cert $cert -FilePath $pfxPath -Password $pfxPassword | Out-Null
Export-Certificate -Cert $cert -FilePath $cerPath | Out-Null

Write-Output "Exported: $pfxPath (private key, NOT committed - see .gitignore)"
Write-Output "Exported: $cerPath (public key, safe to commit)"
Write-Output "Subject:  $subjectName"
Write-Output "Thumbprint: $($cert.Thumbprint)"
