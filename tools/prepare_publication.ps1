[CmdletBinding()]
param(
    [string]$Name = 'knovGBC-source-2026-10-10',
    [switch]$NoZip
)

# Copy-only source export. Never deletes, moves, resets, commits or uploads.
# knovGBC source export correction dated 2026-10-10: reviewed build templates,
# executable-signature exclusion and repeatable privacy/license transforms.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'publication_integrity.ps1')
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if ($Name -notmatch '^knovGBC-[A-Za-z0-9_-]+$') {
    throw 'Use a simple knovGBC-prefixed output name without paths.'
}
$stageParent = Join-Path $repo 'publication'
$destination = Join-Path $stageParent $Name
$zipPath = "$destination.zip"
if ((Test-Path -LiteralPath $destination) -or (Test-Path -LiteralPath $zipPath)) {
    throw 'Output already exists. Choose a new -Name; existing output is never overwritten.'
}
$utf8 = New-Object System.Text.UTF8Encoding($false)
$entries = New-Object 'System.Collections.Generic.List[object]'
$omissions = New-Object 'System.Collections.Generic.List[string]'
$codeExtensions = @('.c', '.h', '.cc', '.cpp', '.cxx', '.hpp', '.s', '.sx', '.asm', '.inc',
    '.ld', '.x', '.specs', '.mk', '.in', '.sh', '.ps1', '.py', '.bat', '.cmd',
    '.md', '.txt', '.rst', '.html', '.css', '.js', '.json', '.yml', '.yaml',
    '.xml', '.toml', '.cfg', '.conf', '.cmake', '.pl', '.awk', '.m', '.mm',
    '.swift', '.glsl', '.vert', '.frag', '.qrc', '.ui', '.rc', '.def', '.pro')
$namedInputs = @('Makefile', 'makefile', 'LICENSE', 'LICENSE.txt', 'COPYING',
    'COPYING.LESSER', 'AUTHORS', 'CONTRIBUTORS', 'README', 'VERSION', 'version.all',
    'linker', '.gitignore', '.gitattributes', '.editorconfig', '.clang-format',
    '.astylerc', 'CMakeLists.txt', 'CHANGES', 'ChangeLog', 'TODO', 'INSTALL', '.gitkeep')
$rootInputs = @('README.md', 'LICENSE', 'Makefile', '.gitignore', '.gitattributes')
# Exact tracked source templates; the vendor-root build/ is generated output.
$libYaulBuildInputs = @(
    'third_party/libyaul/libbcl/build/build.bcl.mk',
    'third_party/libyaul/libfiber/build/build.fiber.mk',
    'third_party/libyaul/libmenu/build/build.menu.mk',
    'third_party/libyaul/libmic3d/build/build.mic3d.mk',
    'third_party/libyaul/libtga/build/build.tga.mk',
    'third_party/libyaul/libyaul/build/build.post.bin.mk',
    'third_party/libyaul/libyaul/build/build.post.iso-cue.mk',
    'third_party/libyaul/libyaul/build/build.post.ss.mk',
    'third_party/libyaul/libyaul/build/build.pre.mk',
    'third_party/libyaul/libyaul/build/build.yaul.mk',
    'third_party/libyaul/libyaul/build/ip.bin.mk')
$libYaulBuildDirectories = @($libYaulBuildInputs | ForEach-Object {
    $_.Substring(0, $_.LastIndexOf('/'))
} | Sort-Object -Unique)
$reviewedNamedInputs = @('third_party/libyaul/libyaul/common/wrap-error')
$openBoot = @{
    'src/saturn/bootroms/dmg_boot.bin' = '6F64DA4CECD7E54E2F928EB3E3BA7810A7A567D0D247CC71737D1771E073A916'
    'src/saturn/bootroms/cgb_boot_fast.bin' = '734FC76858BC6D78E724DF75EFE90146C612817F05C5A837409F9EA34C70E26E'
}
$modifiedPico = @('apu.c', 'apu.h', 'config.h', 'fabric.c', 'fabric.h', 'mi.h',
    'microcode.c', 'microcode.h', 'microcode_dispatch.c', 'ppu.c', 'ppu.h', 'types.h')
$libraryExtensions = @('picogb_mvp_main.c', 'picogb_sm83_compact.c', 'picogb_sm83_compact.h',
    'picogb_sm83_exec_sh2.sx', 'picogb_sm83_sh2.c', 'picogb_sm83_sh2.h',
    'picogb_block_cache.c', 'picogb_block_cache.h', 'picogb_block_executor.c', 'picogb_block_executor.h',
    'picogb_event_scheduler.h', 'picogb_hram_wait.c', 'picogb_hram_wait.h',
    'picogb_idle_spin.c', 'picogb_idle_spin.h', 'picogb_idle_countdown.c',
    'picogb_idle_stat.c', 'picogb_sum_loop.c', 'picogb_slave_ppu_shadow.c',
    'picogb_slave_ppu_shadow.h', 'picogb_vdp12_frame.c', 'picogb_vdp12_frame.h',
    'picogb_ppu_tile_copy.c', 'picogb_ppu_tile_copy.h', 'picogb_ppu_tile_copy_sh2.sx')

function Relative-Path([string]$Path) {
    return $Path.Substring($repo.Length + 1).Replace('\', '/')
}
function Include-Directory([string]$Relative) {
    if ($Relative -match '(^|/)(\.git|build|\.build|bin|obj|__pycache__|autom4te\.cache|\.deps|\.libs)(/|$)' -and
        $Relative -notin $libYaulBuildDirectories) { return $false }
    if ($Relative -match '^tools/tests/picogb_build(/|$)') { return $false }
    if ($Relative -match '^third_party/.*/\.github(/|$)') { return $false }
    if ($Relative -match '^third_party/SameBoy/(iOS|HexFiend)(/|$)') { return $false }
    if ($Relative -match '^third_party/libyaul/(\.images|libyaul/ip/blobs)(/|$)') { return $false }
    if ($Relative -match '^third_party/Peanut-GB/(screencaps|examples/mini_fb)(/|$)') { return $false }
    if ($Relative -match '^third_party/SCSP_poneSound/' -and $Relative -ne 'third_party/SCSP_poneSound/PROJ') { return $false }
    return $true
}
function Test-CompiledBinary([System.IO.FileInfo]$Item) {
    # Inspect magic before using source names/extensions. An extensionless ELF,
    # or an executable renamed to .c/Makefile, is still a compiled binary.
    $stream = [IO.File]::OpenRead($Item.FullName)
    try {
        $header = New-Object byte[] 8
        $count = $stream.Read($header, 0, $header.Length)
    } finally { $stream.Dispose() }
    if ($count -ge 2 -and $header[0] -eq 0x4D -and $header[1] -eq 0x5A) { return $true } # DOS/PE
    if ($count -ge 4) {
        $magic = [BitConverter]::ToString($header, 0, 4)
        if ($magic -in @('7F-45-4C-46', # ELF
            'FE-ED-FA-CE', 'CE-FA-ED-FE', 'FE-ED-FA-CF', 'CF-FA-ED-FE', # Mach-O
            'CA-FE-BA-BE', 'BE-BA-FE-CA', 'CA-FE-BA-BF', 'BF-BA-FE-CA')) { return $true }
    }
    if ($count -eq 8 -and [Text.Encoding]::ASCII.GetString($header) -eq "!<arch>`n") { return $true }
    return $false
}
function Include-File([string]$Relative, [System.IO.FileInfo]$Item) {
    if (Test-CompiledBinary $Item) { return $false }
    if ($openBoot.ContainsKey($Relative)) { return $true }
    if ($Relative -eq 'third_party/SameBoy/BootROMs/SameBoyLogo.png') { return $true }
    if ($Relative -eq 'third_party/SCSP_poneSound/PROJ/srcs') { return $false }
    if ($Relative -match '^third_party/libyaul/(?:.*/)?build/' -and $Relative -notin $libYaulBuildInputs) { return $false }
    if ($Relative -match '^roms/' -and $Relative -ne 'roms/.gitkeep') { return $false }
    if ($Relative -match '^third_party/Peanut-GB/test/(cpu_instrs|instr_timing|dmg-acid2\.gb)\.h$') { return $false }
    if ($Item.Name -match '^(config\.(log|status)|\.env.*)$') { return $false }
    if ($Item.Extension.ToLowerInvariant() -notin $codeExtensions -and $Item.Name -notin $namedInputs -and
        $Relative -notin $reviewedNamedInputs) { return $false }
    return $true
}
function Export-File([System.IO.FileInfo]$Item) {
    $relative = Relative-Path $Item.FullName
    if (($Item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Unreviewed file link: $relative" }
    if (-not (Include-File $relative $Item)) { $omissions.Add($relative); return }
    $hash = (Get-FileHash -LiteralPath $Item.FullName -Algorithm SHA256).Hash
    if ($openBoot.ContainsKey($relative) -and $hash -ne $openBoot[$relative]) { throw "Open boot hash mismatch: $relative" }
    $target = Join-Path $destination $relative
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
    Copy-Item -LiteralPath $Item.FullName -Destination $target
    $change = 'byte-identical'
    $notice = $null
    if ($relative.StartsWith('third_party/PicoGB/') -and $Item.Name -in $modifiedPico) {
        $notice = "/* knovGBC modification notice added 2026-10-10.`n * This file differs from upstream PicoGB commit`n * 325a12a25e2d1f4799424cc18c0e69a7810695b0.`n * The preserved local port changes predate the 2026-10-07 inventory;`n * their exact historical edit dates are unknown. See KNOVGBC_CHANGES.md.`n * PicoGB and this modified library remain under GNU LGPL v2.1.`n * This notice does not replace upstream license or copyright notices.`n */`n"
    } elseif (($relative.StartsWith('src/saturn/') -and $Item.Name -in $libraryExtensions) -or
        $relative -in @('third_party/PicoGB/ppu/ppu.RGB1555.c', 'third_party/PicoGB/ppu/ppu.VDP2_INDEX8.c',
            'tools/tests/fixtures/audio056/apu.c', 'tools/tests/fixtures/audio056/apu.h')) {
        $notice = "/* knovGBC PicoGB library extension/snapshot; notice added 2026-10-10.`n * This source extends/adapts the PicoGB library based on upstream commit`n * 325a12a25e2d1f4799424cc18c0e69a7810695b0.`n * The preserved local code predates the 2026-10-07 inventory;`n * its exact historical edit dates are unknown.`n * Distributed with the modified library under GNU LGPL v2.1.`n * See third_party/PicoGB/KNOVGBC_CHANGES.md and LICENSE.txt in that directory.`n * Original upstream license and copyright notices remain applicable.`n */`n"
    }
    if ($notice) {
        if ([IO.File]::ReadAllText($target) -match '^/\* knovGBC ') {
            $change = 'byte-identical; existing LGPL inventory comment retained'
        } else {
            # Mechanical comment addition in the export only; local source stays intact.
            $prefix = $utf8.GetBytes($notice)
            $body = [IO.File]::ReadAllBytes($target)
            $combined = New-Object byte[] ($prefix.Length + $body.Length)
            [Array]::Copy($prefix, 0, $combined, 0, $prefix.Length)
            [Array]::Copy($body, 0, $combined, $prefix.Length, $body.Length)
            [IO.File]::WriteAllBytes($target, $combined)
            $change = 'LGPL inventory comment prefixed; original body intact'
        }
    }
    if ($relative -eq 'tools/build_project.ps1') {
        $body = [IO.File]::ReadAllText($target)
        $lines = $body -split '\r?\n'
        $legacyPathCount = 0
        $safePathCount = 0
        $safeMount = '    mount --bind "${KNOVGBC_LEGACY_SOURCE_PATH:?Set a space-free Linux source path or use tools/build.ps1}" /mnt/knovGBC'
        for ($i = 0; $i -lt $lines.Length; $i++) {
            if ($lines[$i] -match '^    mount --bind /mnt/projectdrive/Users/') {
                $lines[$i] = $safeMount
                $legacyPathCount++
            } elseif ($lines[$i] -ceq $safeMount) { $safePathCount++ }
        }
        if (($legacyPathCount + $safePathCount) -ne 1) { throw 'Legacy private path pattern changed; review before exporting.' }
        if ($legacyPathCount -eq 1) {
            [IO.File]::WriteAllText($target, ($lines -join "`n"), $utf8)
            $change = 'legacy workstation path replaced by KNOVGBC_LEGACY_SOURCE_PATH; LF normalized'
        } else { $change = 'byte-identical; KNOVGBC_LEGACY_SOURCE_PATH already used' }
    }
    if ($relative -eq 'third_party/gnuboy/.gitignore') {
        $body = [IO.File]::ReadAllText($target)
        if ($body -notmatch '(?m)^!sys/nix/config\.h\.in\s*$') {
            $body += "`n# knovGBC source archive, 2026-10-10: retain the build input template.`n!sys/nix/config.h.in`n"
            [IO.File]::WriteAllText($target, $body, $utf8)
            $change = 'append source-template exception to upstream ignore rules'
        } else { $change = 'byte-identical; existing source-template exception retained' }
    }
    if (-not $openBoot.ContainsKey($relative) -and $relative -ne 'third_party/SameBoy/BootROMs/SameBoyLogo.png') {
        $body = [IO.File]::ReadAllText($target)
        if ($body -match '(?m)-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----|\bAKIA[A-Z0-9]{16}\b|\bgh[pousr]_[A-Za-z0-9]{30,}\b|\bgithub_pat_[A-Za-z0-9_]{30,}\b') {
            throw "Possible credential marker in $relative; no publication archive produced. Review this file locally."
        }
        if ($body -match '(?i)(?:C:|/mnt/[a-z]|/mnt/projectdrive)[\\/]Users[\\/]Admin[\\/]') {
            throw "Unreviewed workstation path in $relative; review before publication."
        }
    }
    $fingerprint = Get-PublicationFingerprint -Bytes ([IO.File]::ReadAllBytes($target)) -Text:($Item.Extension -notin @('.bin', '.png'))
    $entry = [ordered]@{
        path = $relative; original_sha256 = $hash
        exported_sha256 = $fingerprint.exported_sha256
        bytes = $fingerprint.bytes; transformation = $change
    }
    foreach ($key in @('canonical_eol', 'canonical_sha256', 'canonical_bytes')) {
        if ($fingerprint.Contains($key)) { $entry[$key] = $fingerprint[$key] }
    }
    $entries.Add([pscustomobject]$entry)
}
function Export-Directory([string]$Directory) {
    foreach ($item in Get-ChildItem -LiteralPath $Directory -Force) {
        $relative = Relative-Path $item.FullName
        if ($item.PSIsContainer) {
            if (-not (Include-Directory $relative)) { $omissions.Add("$relative/"); continue }
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Unreviewed directory link: $relative" }
            Export-Directory $item.FullName
        } else { Export-File $item }
    }
}

[IO.Directory]::CreateDirectory($destination) | Out-Null
foreach ($file in $rootInputs) { Export-File (Get-Item -LiteralPath (Join-Path $repo $file)) }
foreach ($directory in @('src', 'tools', 'docs', '.github', 'roms',
    'third_party/PicoGB', 'third_party/SameBoy', 'third_party/libyaul',
    'third_party/Peanut-GB', 'third_party/gnuboy', 'third_party/SCSP_poneSound')) {
    Export-Directory (Join-Path $repo $directory)
}
$manifest = [ordered]@{
    project = 'knovGBC'; record_date = '2026-10-10'; source_history = 'No root Git repository found during preparation'
    original_project_preserved = $true; uploaded = $false; files = @($entries | Sort-Object path)
    fingerprint_policy = 'Raw source/export SHA-256 and sizes are retained. Explicit text fingerprints additionally compare CRLF-to-LF bytes; binary and NUL-containing inputs require exact bytes.'
    withheld_scopes = @('build/', 'dist/', 'toolchain/', 'tmp/', 'unselected root downloads/logs',
        'third_party/crosstool-ng-1.28.0/', 'third_party/libyaul-build-scripts/',
        'third_party/rgbds-1.0.3/', 'third_party/saroo-reference/',
        'commercial ROMs and generated game-containing artifacts')
    omitted_within_selected_scopes = @($omissions | Sort-Object)
    limitations = 'Extension/path allowlist and credential marker scan are not a legal clearance or exhaustive security audit. See docs/PUBLICATION.md and docs/THIRD_PARTY.md.'
}
[IO.File]::WriteAllText((Join-Path $destination 'PUBLICATION_MANIFEST.json'),
    ($manifest | ConvertTo-Json -Depth 7) + "`n", $utf8)

if (-not $NoZip) {
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $stream = [IO.File]::Open($zipPath, [IO.FileMode]::CreateNew)
    $archive = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Create, $false)
    try {
        foreach ($file in Get-ChildItem -LiteralPath $destination -Force -Recurse -File) {
            $relative = $file.FullName.Substring($destination.Length + 1).Replace('\', '/')
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName,
                "knovGBC/$relative", [IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }
    } finally { $archive.Dispose(); $stream.Dispose() }
}
Write-Output "Prepared $($entries.Count) source/input files plus manifest: $destination"
if (-not $NoZip) { Write-Output "ZIP: $zipPath" }
Write-Output 'Original files preserved. Nothing committed or uploaded. Review docs/PUBLICATION.md before publication.'
