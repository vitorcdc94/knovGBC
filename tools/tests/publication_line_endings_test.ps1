[CmdletBinding()]
param()

# Synthetic publication-tool tests only; no ROM, SDK, commit or network access.
# Original knovGBC helper, added 2026-10-10; MIT (root LICENSE).
$ErrorActionPreference = 'Stop'
$releaseRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$verifier = Join-Path $releaseRoot 'tools/verify_publication.ps1'
. (Join-Path $releaseRoot 'tools/publication_integrity.ps1')
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('knovGBC-eol-test-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($testRoot) | Out-Null
$fixture = Join-Path $testRoot 'case'
[IO.Directory]::CreateDirectory($fixture) | Out-Null
$utf8 = New-Object Text.UTF8Encoding($false)
$ascii = [Text.Encoding]::ASCII
$lf = [string][char]10
$cr = [string][char]13
$assertions = 0

function Assert-True([bool]$Condition, [string]$Label) {
    if (-not $Condition) { throw "Assertion failed: $Label" }
    $script:assertions++
}
function Write-Fixture([byte[]]$Bytes, [switch]$Legacy, [switch]$Boot) {
    $relative = 'sample.txt'
    if ($Boot) { $relative = 'src/saturn/bootroms/dmg_boot.bin' }
    $target = Join-Path $fixture $relative
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
    [IO.File]::WriteAllBytes($target, $Bytes)
    $fingerprint = Get-PublicationFingerprint -Bytes $Bytes -Text:(-not $Boot)
    $entry = [ordered]@{ path = $relative }
    foreach ($key in $fingerprint.Keys) {
        if (-not $Legacy -or $key -notlike 'canonical_*') { $entry[$key] = $fingerprint[$key] }
    }
    [IO.File]::WriteAllText((Join-Path $fixture 'PUBLICATION_MANIFEST.json'),
        (@{ files = @($entry) } | ConvertTo-Json -Depth 5), $utf8)
    return $target
}
function Assert-Check([bool]$ShouldPass, [string]$Label, [string]$FailureMarker = '') {
    $output = @()
    $passed = $true
    try { & $verifier -Root $fixture | ForEach-Object { $output += $_ } }
    catch { $passed = $false; $output += $_.Exception.Message }
    Assert-True ($passed -eq $ShouldPass) $Label
    if (-not $ShouldPass -and $FailureMarker) {
        Assert-True (($output -join $lf).Contains($FailureMarker)) "$Label failure reason"
    }
}

try {
    $mixed = $ascii.GetBytes('alpha' + $cr + $lf + 'beta' + $lf + 'gamma' + $cr + 'delta')
    $expectedLf = $ascii.GetBytes('alpha' + $lf + 'beta' + $lf + 'gamma' + $cr + 'delta')
    $canonical = ConvertTo-PublicationLfBytes $mixed
    Assert-True ([BitConverter]::ToString($canonical) -ceq [BitConverter]::ToString($expectedLf)) 'Only CRLF pairs change; lone CR is preserved'
    $fingerprint = Get-PublicationFingerprint -Bytes $mixed -Text
    Assert-True ($fingerprint.bytes -eq $mixed.Length) 'Raw byte count retained'
    Assert-True ($fingerprint.canonical_bytes -eq $expectedLf.Length) 'Canonical byte count recorded'
    $target = Write-Fixture $mixed
    Assert-Check $true 'Mixed-EOL original passes'
    [IO.File]::WriteAllBytes($target, $expectedLf)
    Assert-Check $true 'LF checkout passes'
    [IO.File]::WriteAllBytes($target, $ascii.GetBytes(($ascii.GetString($expectedLf)).Replace($lf, $cr + $lf)))
    Assert-Check $true 'CRLF checkout passes'
    [IO.File]::WriteAllBytes($target, $ascii.GetBytes('alpha' + $lf + 'Beta' + $lf + 'gamma' + $cr + 'delta'))
    Assert-Check $false 'Same-size content edit rejected' 'Manifest hash/size mismatch'
    [IO.File]::WriteAllBytes($target, $ascii.GetBytes($ascii.GetString($expectedLf) + ' '))
    Assert-Check $false 'Whitespace addition rejected' 'Manifest hash/size mismatch'
    [IO.File]::WriteAllBytes($target, $ascii.GetBytes($ascii.GetString($expectedLf).Replace($cr, '')))
    Assert-Check $false 'Lone CR removal rejected' 'Manifest hash/size mismatch'
    [IO.File]::WriteAllBytes($target, $ascii.GetBytes($ascii.GetString($expectedLf) + $lf))
    Assert-Check $false 'Added newline rejected' 'Manifest hash/size mismatch'
    [IO.File]::WriteAllBytes($target, $mixed)
    [IO.File]::Delete($target)
    Assert-Check $false 'Missing input rejected' 'Manifest file missing'
    $target = Write-Fixture $mixed
    $unexpected = Join-Path $fixture 'unexpected.txt'
    [IO.File]::WriteAllText($unexpected, 'Synthetic unexpected input.', $utf8)
    Assert-Check $false 'Unlisted input rejected' 'Unlisted file'
    [IO.File]::Delete($unexpected)
    $forbidden = Join-Path $fixture 'synthetic.gb'
    [IO.File]::WriteAllText($forbidden, 'Not a cartridge ROM; extension rejection test.', $utf8)
    Assert-Check $false 'Cartridge extension rejected' 'Unexpected private/generated/media file'
    [IO.File]::Delete($forbidden)
    [IO.File]::WriteAllBytes($target, [byte[]](0x7F, 0x45, 0x4C, 0x46))
    Assert-Check $false 'Disguised ELF rejected' 'Compiled executable/archive signature'
    $target = Write-Fixture $mixed -Legacy
    Assert-Check $true 'Legacy raw-only manifest passes exact original'
    [IO.File]::WriteAllBytes($target, $expectedLf)
    Assert-Check $false 'Legacy manifest never guesses an EOL fingerprint' 'Manifest hash/size mismatch'
    $unicode = $utf8.GetBytes([string][char]0x00E9 + $cr + $lf + [char]0x03BB)
    $target = Write-Fixture $unicode
    [IO.File]::WriteAllBytes($target, (ConvertTo-PublicationLfBytes $unicode))
    Assert-Check $true 'Non-ASCII UTF-8 bytes preserved'
    [byte[]]$withBom = @(0xEF, 0xBB, 0xBF) + $unicode
    [IO.File]::WriteAllBytes($target, $withBom)
    Assert-Check $false 'Added UTF-8 BOM rejected' 'Manifest hash/size mismatch'
    $withBomTarget = Write-Fixture $withBom
    [IO.File]::WriteAllBytes($withBomTarget, $unicode)
    Assert-Check $false 'Removed UTF-8 BOM rejected' 'Manifest hash/size mismatch'
    $utf16 = [Text.Encoding]::Unicode.GetBytes('alpha' + $cr + $lf)
    $strict = Get-PublicationFingerprint -Bytes $utf16 -Text
    Assert-True (-not $strict.Contains('canonical_eol')) 'NUL-containing UTF-16 input is raw-only'
    [byte[]]$fakeBinary = 0x41, 0x0D, 0x0A, 0x42
    $strict = Get-PublicationFingerprint -Bytes $fakeBinary
    Assert-True (-not $strict.Contains('canonical_eol')) 'Binary input is raw-only even without NUL'
    $target = Write-Fixture ([byte[]]@())
    Assert-Check $true 'Empty text input passes'
    $bootBytes = [IO.File]::ReadAllBytes((Join-Path $releaseRoot 'src/saturn/bootroms/dmg_boot.bin'))
    [IO.File]::Delete($target)
    $bootTarget = Write-Fixture $bootBytes -Boot
    Assert-Check $true 'Reviewed open boot binary passes exact bytes'
    $bootBytes[0] = $bootBytes[0] -bxor 1
    [IO.File]::WriteAllBytes($bootTarget, $bootBytes)
    Assert-Check $false 'Changed open boot binary rejected' 'Reviewed open boot binary changed'
    Write-Output "PASS: $assertions publication line-ending assertions; text edits and binary changes rejected."
    Write-Output 'No source assets changed. Tests use isolated temporary fixtures, not ROMs or the SDK.'
} finally {
    # Only the explicitly created, unique fixture directory can be removed.
    $resolvedTestRoot = [IO.Path]::GetFullPath($testRoot)
    $tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $resolvedTestRoot.StartsWith($tempBase, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolvedTestRoot) -notmatch '^knovGBC-eol-test-[0-9a-f]{32}$') {
        throw 'Refusing cleanup outside the explicitly created test directory.'
    }
    [IO.Directory]::Delete($resolvedTestRoot, $true)
}
