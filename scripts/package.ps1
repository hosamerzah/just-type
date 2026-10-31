param([string]$CertificateThumbprint, [switch]$UnsignedPreview, [string]$BuildDirectory = 'build-clang-x64')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
Push-Location $taskRoot
try {
    if (-not $CertificateThumbprint -and -not $UnsignedPreview) { throw 'A trusted code-signing certificate is required. Use -UnsignedPreview only for a clearly labeled development build.' }
    if ($CertificateThumbprint -and $UnsignedPreview) { throw 'Choose signed release or unsigned preview, not both.' }
    python scripts/make_payload.py --check-only
    if ($LASTEXITCODE) { throw 'Staged build validation failed' }
    $taskBuildInfo = Get-Content dist/x64/stage-build.json -Raw | ConvertFrom-Json
    function Sign-Artifact([string]$Artifact) {
        $taskCert = Get-Item "Cert:/CurrentUser/My/$CertificateThumbprint" -ErrorAction Stop
        if (-not $taskCert.HasPrivateKey -or $taskCert.NotAfter -lt (Get-Date)) { throw 'Signing certificate is expired or has no private key.' }
        $taskSigntool = (Get-Command signtool.exe -ErrorAction SilentlyContinue).Source
        if (-not $taskSigntool) {
            $taskSdk = "${env:ProgramFiles(x86)}/Windows Kits/10/bin"
            $taskSigntool = Get-ChildItem $taskSdk -Filter signtool.exe -Recurse | Where-Object FullName -Match '\\x64\\' | Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
        }
        if (-not $taskSigntool) { throw 'Install Windows SDK signing tools to sign the release.' }
        & $taskSigntool sign /sha1 $CertificateThumbprint /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $Artifact
        if ($LASTEXITCODE) { throw "Signing failed: $Artifact" }
        & $taskSigntool verify /pa $Artifact
        if ($LASTEXITCODE) { throw "Signature verification failed: $Artifact" }
    }
    if ($CertificateThumbprint) {
        Sign-Artifact 'dist/x64/AutoKeyboard.exe'
        Sign-Artifact 'dist/x64/AutoKeyboardControl.exe'
        Sign-Artifact 'dist/x64/AutoKeyboardIME.dll'
        Sign-Artifact 'dist/x86/AutoKeyboardIME.dll'
        Sign-Artifact 'dist/x64/AutoKeyboardPdfWorker.exe'
        Sign-Artifact 'dist/x64/AutoKeyboardTypingTest.exe'
        Sign-Artifact 'dist/x64/AutoKeyboardNotepadDiagnostic.exe'
        Sign-Artifact 'dist/x86/AutoKeyboardTypingTest.exe'
        Sign-Artifact 'dist/x86/AutoKeyboardControl.exe'
        python scripts/build_identity.py --stage dist/x64 --arch x64 --version $taskBuildInfo.version --expected-source $taskBuildInfo.source
        if ($LASTEXITCODE) { throw 'Signed x64 stage validation failed' }
        python scripts/build_identity.py --stage dist/x86 --arch x86 --version $taskBuildInfo.version --expected-source $taskBuildInfo.source
        if ($LASTEXITCODE) { throw 'Signed x86 stage validation failed' }
    }
    python scripts/make_payload.py
    if ($LASTEXITCODE) { throw 'Installer payload failed' }
    $taskRc = (Join-Path $taskRoot 'build-package/setup.rc').Replace('\','/')
    cmake -S . -B $BuildDirectory "-DAK_SETUP_RC=$taskRc"
    if ($LASTEXITCODE) { throw 'Installer configuration failed' }
    cmake --build $BuildDirectory --config Release --target AutoKeyboardSetup --parallel 4
    if ($LASTEXITCODE) { throw 'Installer build failed' }
    $taskSetup = Join-Path $BuildDirectory 'AutoKeyboardSetup.exe'
    if (-not (Test-Path $taskSetup)) { $taskSetup = Join-Path $BuildDirectory 'Release/AutoKeyboardSetup.exe' }
    $taskProcess = Start-Process -FilePath $taskSetup -ArgumentList '--verify-payload' -WindowStyle Hidden -Wait -PassThru
    if ($taskProcess.ExitCode) { throw 'Installer payload verification failed' }
    if ($CertificateThumbprint) { Sign-Artifact $taskSetup }
    New-Item -ItemType Directory -Force dist | Out-Null
    $taskName = if ($UnsignedPreview) { "AutoKeyboard-$($taskBuildInfo.version)-unsigned-preview-setup.exe" } else { "AutoKeyboard-$($taskBuildInfo.version)-setup.exe" }
    Copy-Item -LiteralPath $taskSetup -Destination (Join-Path 'dist' $taskName)
    Get-FileHash -Algorithm SHA256 (Join-Path 'dist' $taskName)
} finally { Pop-Location }
