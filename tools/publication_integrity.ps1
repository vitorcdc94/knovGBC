# Original knovGBC publication helper, added 2026-10-10; MIT (root LICENSE).
# Dot-source this file. It performs no writes and has no encoding conversion.

function ConvertTo-PublicationLfBytes([byte[]]$Bytes) {
    # Latin-1 is used only as a reversible 1:1 mapping of byte values.
    # Replace CRLF pairs, preserving lone CR, BOMs and every other byte.
    $byteEncoding = [Text.Encoding]::GetEncoding(28591)
    $crlf = [string][char]13 + [char]10
    $normalized = $byteEncoding.GetString($Bytes).Replace($crlf, [string][char]10)
    return ,($byteEncoding.GetBytes($normalized))
}

function Get-PublicationFingerprint([byte[]]$Bytes, [switch]$Text) {
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        $result = [ordered]@{
            exported_sha256 = [BitConverter]::ToString($sha256.ComputeHash($Bytes)).Replace('-', '')
            bytes = $Bytes.Length
        }
        # Binary assets and NUL-containing inputs (including UTF-16) stay exact.
        if ($Text -and [Array]::IndexOf($Bytes, [byte]0) -lt 0) {
            $canonical = ConvertTo-PublicationLfBytes $Bytes
            $result['canonical_eol'] = 'crlf-to-lf-v1'
            $result['canonical_sha256'] = [BitConverter]::ToString($sha256.ComputeHash($canonical)).Replace('-', '')
            $result['canonical_bytes'] = $canonical.Length
        }
        return $result
    } finally { $sha256.Dispose() }
}
