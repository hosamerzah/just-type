param([ValidateSet('x64','x86')][string]$Architecture = 'x64', [switch]$Bootstrap, [switch]$UseLocalClang, [string]$BuildDirectory)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
Push-Location $taskRoot
try {
    if ($Bootstrap) { python scripts/bootstrap.py --arch $Architecture; if ($LASTEXITCODE) { throw 'Dependency bootstrap failed' } }
    python scripts/make_icon.py
    $taskBuild = "build-$Architecture"
    if ($UseLocalClang) {
        $taskBuild = "build-clang-$Architecture"
        if ($BuildDirectory) { $taskBuild = $BuildDirectory }
        $taskToolchain = (Join-Path $taskRoot '.deps/llvm-mingw-20261020-ucrt-x86_64/bin').Replace('\','/')
        if (-not (Test-Path "$taskToolchain/clang.exe")) { throw 'Local LLVM-MinGW toolchain not found; see README.' }
        $taskTarget = if ($Architecture -eq 'x64') { 'x86_64-w64-mingw32' } else { 'i686-w64-mingw32' }
        $taskNinja = (Get-Command ninja -ErrorAction SilentlyContinue).Source
        if (-not $taskNinja) {
            $taskVswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
            $taskVs = & $taskVswhere -latest -prerelease -products '*' -property installationPath
            $taskNinja = Join-Path $taskVs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
        }
        $taskNinja = $taskNinja.Replace('\','/')
        cmake -S . -B $taskBuild -G Ninja "-DCMAKE_MAKE_PROGRAM=$taskNinja" "-DCMAKE_C_COMPILER=$taskToolchain/$taskTarget-clang.exe" "-DCMAKE_CXX_COMPILER=$taskToolchain/$taskTarget-clang++.exe" "-DCMAKE_RC_COMPILER=$taskToolchain/$taskTarget-windres.exe" -DCMAKE_BUILD_TYPE=Release
    } else {
        if ($BuildDirectory) { $taskBuild = $BuildDirectory }
        $taskPlatform = if ($Architecture -eq 'x64') { 'x64' } else { 'Win32' }
        cmake -S . -B $taskBuild -A $taskPlatform
    }
    if ($LASTEXITCODE) { throw 'CMake configuration failed' }
    cmake --build $taskBuild --config Release --parallel 4
    if ($LASTEXITCODE) { throw 'Build failed' }
    ctest --test-dir $taskBuild -C Release --output-on-failure
    if ($LASTEXITCODE) { throw 'Tests failed' }
    cmake --install $taskBuild --config Release --prefix "dist/$Architecture"
    if ($LASTEXITCODE) { throw 'Staging failed' }
} finally { Pop-Location }
