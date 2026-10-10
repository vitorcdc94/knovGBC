[CmdletBinding()]
param([string]$Root)

# Original knovGBC source-handoff checker, added 2026-10-10.
# Read-only. This checks inventory and known markers, not legal clearance.
$ErrorActionPreference = 'Stop'
if (-not $Root) { $Root = Split-Path -Parent $PSScriptRoot }
$rootPath = (Resolve-Path -LiteralPath $Root).Path.TrimEnd('\', '/')
$manifest = Get-Content -Raw -LiteralPath (Join-Path $rootPath 'PUBLICATION_MANIFEST.json') | ConvertFrom-Json
$expected = @{}
$problems = New-Object 'System.Collections.Generic.List[string]'
$seen = @{}
$files = New-Object 'System.Collections.Generic.List[System.IO.FileInfo]'
$pending = New-Object 'System.Collections.Generic.Queue[string]'
$pending.Enqueue($rootPath)
while ($pending.Count -gt 0) {
    foreach ($item in Get-ChildItem -Force -LiteralPath $pending.Dequeue()) {
        $relative = $item.FullName.Substring($rootPath.Length + 1).Replace('\', '/')
        # The repository's own Git metadata is not a publication source asset.
        if ($relative -eq '.git') { continue }
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            $problems.Add("Unreviewed file/directory link: $relative")
        } elseif ($item.PSIsContainer) {
            if ($relative -match '(^|/)(\.git|\.aws|\.ssh|\.codex|\.agents)(/|$)') {
                $problems.Add("Private or nested metadata directory: $relative")
            } else { $pending.Enqueue($item.FullName) }
        } else { $files.Add($item) }
    }
}
foreach ($entry in $manifest.files) {
    $path = [string]$entry.path
    if (-not $path -or $path -match '(^/|^[A-Za-z]:|\\|(^|/)\.\.(/|$))') {
        $problems.Add("Invalid manifest-relative path: $path")
        continue
    }
    if ($expected.ContainsKey($path)) {
        $problems.Add("Duplicate manifest path: $path")
    } else { $expected[$path] = $entry }
}
$openBoot = @{
    'src/saturn/bootroms/dmg_boot.bin' = '6F64DA4CECD7E54E2F928EB3E3BA7810A7A567D0D247CC71737D1771E073A916'
    'src/saturn/bootroms/cgb_boot_fast.bin' = '734FC76858BC6D78E724DF75EFE90146C612817F05C5A837409F9EA34C70E26E'
}
$sourceImage = 'third_party/SameBoy/BootROMs/SameBoyLogo.png'
foreach ($file in $files) {
    $relative = $file.FullName.Substring($rootPath.Length + 1).Replace('\', '/')
    $seen[$relative] = $true
    if ($relative -eq 'PUBLICATION_MANIFEST.json') { continue }
    if (-not $expected.ContainsKey($relative)) {
        $problems.Add("Unlisted file: $relative")
    } else {
        $entry = $expected[$relative]
        $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        if ($hash -ne $entry.exported_sha256 -or $file.Length -ne $entry.bytes) {
            $problems.Add("Manifest hash/size mismatch: $relative")
        }
    }
    if ($relative -match '^(build|dist|tmp|toolchain|publication|\.toolchain-src)/') {
        $problems.Add("Generated/private staging file: $relative")
    }
    if ($relative -match '^roms/' -and $relative -ne 'roms/.gitkeep') {
        $problems.Add("Cartridge input is not a public source asset: $relative")
    }
    if ($file.Name -match '^\.env($|\.)|^sys_.*\.bin$' -or
        $file.Extension -match '^\.(gb|gbc|gba|sav|srm|cue|iso|elf|exe|dll|o|a|so|zip|7z|rar|pdf|pem|key|p12|pfx|mov|mp4|wav|mp3|adx|pcm|jpg|jpeg)$') {
        $problems.Add("Unexpected private/generated/media file: $relative")
    }
    $bytes = [IO.File]::ReadAllBytes($file.FullName)
    if ($bytes.Length -ge 4) {
        $magic = [BitConverter]::ToString($bytes, 0, 4)
        if ($magic -in @('7F-45-4C-46', 'FE-ED-FA-CE', 'CE-FA-ED-FE',
                        'FE-ED-FA-CF', 'CF-FA-ED-FE', 'CA-FE-BA-BE') -or
            ($bytes[0] -eq 0x4D -and $bytes[1] -eq 0x5A) -or
            ($bytes.Length -ge 8 -and [Text.Encoding]::ASCII.GetString($bytes, 0, 8) -eq ('!<arch>' + [char]10))) {
            $problems.Add("Compiled executable/archive signature: $relative")
        }
    }
    if ($openBoot.ContainsKey($relative)) {
        if ((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -ne $openBoot[$relative]) {
            $problems.Add("Reviewed open boot binary changed: $relative")
        }
        continue
    }
    if ($relative -eq $sourceImage) { continue }
    if ($file.Extension -eq '.bin' -or $file.Extension -eq '.png') {
        $problems.Add("Unreviewed binary/image: $relative")
        continue
    }
    $body = [IO.File]::ReadAllText($file.FullName)
    if ($body.IndexOf([char]0) -ge 0) { $problems.Add("Unexpected binary content in text input: $relative") }
    # Values are never printed; a failure reports only the filename.
    if ($body -match '(?m)-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----|\bAKIA[A-Z0-9]{16}\b|\bgh[pousr]_[A-Za-z0-9_]{30,}\b|\bgithub_pat_[A-Za-z0-9_]{30,}\b') {
        $problems.Add("Possible credential value: $relative")
    }
    if ($body -match '(?i)(?:C:|/mnt/[a-z]|/mnt/projectdrive)[\\/]Users[\\/]Admin[\\/]') {
        $problems.Add("Personal workstation path: $relative")
    }
}
foreach ($path in $expected.Keys) {
    if (-not $seen.ContainsKey($path)) { $problems.Add("Manifest file missing: $path") }
}
if ($problems.Count -gt 0) {
    foreach ($problem in $problems) { Write-Output "FAIL: $problem" }
    throw "Publication check failed with $($problems.Count) problem(s). No files changed."
}
Write-Output "PASS: $($expected.Count) manifest entries plus manifest; hashes, sizes and known publication markers checked."
Write-Output 'No files changed. This is not exhaustive secret detection, legal clearance, a compilation or a hardware test.'
