param(
    [string]$Compiler = "gcc",
    [string]$Rom = "roms/tetris.gb"
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$build = Join-Path $PSScriptRoot "picogb_build"
New-Item -ItemType Directory -Force -Path $build | Out-Null

$sources = @(
    (Join-Path $repo "tools/tests/picogb_smoke.c"),
    (Join-Path $repo "third_party/PicoGB/mi.c"),
    (Join-Path $repo "third_party/PicoGB/microcode_dispatch.c"),
    (Join-Path $repo "third_party/PicoGB/microcode.c"),
    (Join-Path $repo "third_party/PicoGB/ppu.c"),
    (Join-Path $repo "third_party/PicoGB/fabric.c")
)
$output = Join-Path $build "picogb_smoke.exe"

& $Compiler -std=gnu11 -O2 -Wall -Wextra `
    -DPICOGB_CUSTOM=1 `
    "-I$(Join-Path $repo 'tools/tests/picogb_include')" `
    "-I$(Join-Path $repo 'third_party/PicoGB')" `
    @sources -o $output
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $output (Join-Path $repo $Rom)
exit $LASTEXITCODE
