param([string]$Zig = 'zig')
$ErrorActionPreference = 'Stop'
$outputDir = Join-Path $PSScriptRoot 'dist'
$profileDir = Join-Path $outputDir 'launcher-compat'
$dll = Join-Path $outputDir 'ACZeroMods-v59.dll'
$dllHash = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant()
New-Item -ItemType Directory -Force -Path $profileDir | Out-Null
$profile = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'compat\portable_v59_profile.h'))
$profile = [regex]::Replace($profile, 'dllHash\[\]="[0-9a-f]{64}"', ('dllHash[]="' + $dllHash + '"'))
[IO.File]::WriteAllText((Join-Path $profileDir 'portable_v59_profile.h'), $profile)
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $PSScriptRoot '.zig-cache-global'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $PSScriptRoot '.zig-cache'
& $Zig c++ -target x86_64-windows-gnu -O2 -static -municode '-Wl,--subsystem,windows' '-DACZERO_PORTABLE_V59' -I $profileDir (Join-Path $PSScriptRoot 'src\portable_launcher.cpp') -luser32 -lgdi32 -lshell32 -lbcrypt -lkernel32 -lversion -o (Join-Path $outputDir 'ACZeroMods.exe')
if ($LASTEXITCODE -ne 0) { throw "Launcher build failed: $LASTEXITCODE" }
Get-FileHash -LiteralPath (Join-Path $outputDir 'ACZeroMods.exe') -Algorithm SHA256
