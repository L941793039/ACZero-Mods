param([string]$Zig = 'zig')
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
$compiler = $Zig
$outputDir = Join-Path $projectRoot 'dist'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $projectRoot '.zig-cache-global'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $projectRoot '.zig-cache'
$hookRoot = Join-Path $projectRoot 'vendor\minhook'
$buildOptions = @()
$dllName = 'ACZeroMods-v59.dll'
& $compiler c++ -target x86_64-windows-gnu -O2 -shared -static @buildOptions -I "$hookRoot\include" "$projectRoot\src\aczero_mods.cpp" "$projectRoot\src\normal_aim.S" "$projectRoot\src\normal_speed.S" "$projectRoot\src\missile_range.S" "$projectRoot\src\target_range.S" "$projectRoot\src\target_distance.S" "$projectRoot\src\target_direction.S" "$hookRoot\src\buffer.c" "$hookRoot\src\hook.c" "$hookRoot\src\trampoline.c" "$hookRoot\src\hde\hde64.c" -luser32 -lkernel32 -o "$outputDir\$dllName"
if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
Get-FileHash -LiteralPath "$outputDir\$dllName" -Algorithm SHA256
