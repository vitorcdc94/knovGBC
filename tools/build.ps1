[CmdletBinding()]
param(
    [string]$Rom,
    [switch]$Probe,
    [switch]$Check,
    [switch]$ElfOnly,
    [Alias('Profile')]
    [ValidateSet('mvpfastzoomaudiomono', 'mvpfastzoomaudio', 'mvpfastzoomtile', 'mvpfastsml')]
    [string]$BuildProfile = 'mvpfastzoomaudiomono',
    [string]$Distribution,
    [string]$Toolchain
)

$ErrorActionPreference = 'Stop'
if ($Rom -and $Probe) { throw '-Rom and -Probe are mutually exclusive.' }
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$wsl = Join-Path $env:SystemRoot 'System32\wsl.exe'
if (-not (Test-Path -LiteralPath $wsl)) { throw 'WSL is required. See docs/BUILDING.md.' }
$wslOptions = @()
if ($Distribution) { $wslOptions += @('--distribution', $Distribution) }

function ConvertTo-WslPath([string]$Path) {
    if ($Path.StartsWith('/')) { return $Path }
    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $converted = & $wsl @wslOptions -- wslpath -u $resolved
    if ($LASTEXITCODE -ne 0) { throw "Could not convert path for WSL: $resolved" }
    return ($converted -join "`n").Trim()
}

$linuxRepo = ConvertTo-WslPath $repo
$buildOptions = @('--profile', $BuildProfile)
if ($Rom) { $buildOptions += @('--rom', (ConvertTo-WslPath $Rom)) }
if ($Probe) { $buildOptions += '--probe' }
if ($Check) { $buildOptions += '--check' }
if ($ElfOnly) { $buildOptions += '--elf-only' }
$command = @('--')
if ($Toolchain) { $command += @('env', "YAUL_INSTALL_ROOT=$(ConvertTo-WslPath $Toolchain)") }
$command += @('bash', "$linuxRepo/tools/build.sh") + $buildOptions
# No root account, fixed username, mount, dependency install, or distro default.
& $wsl @wslOptions @command
exit $LASTEXITCODE
