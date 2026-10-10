[CmdletBinding()]
param(
    [ValidateSet('audiomonotest', 'mvpfastzoomaudiomono', 'audiotest', 'mvpfastzoomaudio', 'package', 'diagnostic', 'bootstrap', 'bootstrapfix', 'bootstrapstack', 'bootstrapquarantine', 'masteronlyprobe', 'masteronlyvideo', 'masteronlyvideofix', 'bootstraptrace', 'stage0', 'stage1', 'stage2', 'masteronly', 'emulatorprobe', 'mvp', 'mvpfast', 'mvpfastos', 'mvpfastprof', 'mvpfastmem', 'mvpfastppu', 'mvpfastppufull', 'mvpfastsm83', 'mvpfastslaveppu', 'mvpfastmastervdp2', 'mvpfastcache', 'mvpfastfetch', 'mvpfastldh', 'mvpfastanda', 'mvpfastrepro', 'mvpfastfps', 'mvpfastnext', 'mvpfastidle', 'mvpfastidlecache', 'mvpfastidleevent', 'mvpfastidleresume', 'mvpfastidlecount', 'mvpfastsum', 'mvpfastidlestat', 'mvpfasthramwait', 'mvpfastcpn8', 'mvpfastvdp2lines', 'mvpfastppueventcoalesce', 'mvpfastpputwolines', 'mvpfastppuadaptive', 'mvpfastppufourlines', 'mvpfastppueightlines', 'mvpfastvdp2dma', 'mvpfastvdp2direct', 'mvpfastsmpcgate', 'mvpfastindexed8', 'mvpfaststackpair', 'mvpfastdirtyregions', 'mvpfastsml', 'mvpfastzoom', 'mvpfastzoom3', 'mvpfastzoom60', 'mvpfastzoomtile', 'mvpfastvdp12', 'mvpfastjrz', 'mvpfastcb', 'mvpfastopprof', 'mvpfastf0tight', 'mvpfastblock', 'mvpfastblockc', 'mvpfastcold', 'mvpfastslaveppushadow', 'mvpfastsched', 'mvpfastcompact', 'zoomtest', 'fullratetest', 'bootroms', 'yaul', 'clean', 'rebuild')]
    [string]$Target = 'package'
)

$ErrorActionPreference = 'Stop'

# Yaul cannot build from a path containing spaces. This project stays in its
# original Windows folder and is bind-mounted at this stable WSL path.
$linuxCommand = @'
set -Eeuo pipefail
mkdir -p /mnt/projectdrive /mnt/knovGBC
if ! mountpoint -q /mnt/knovGBC; then
    mountpoint -q /mnt/projectdrive || mount -t drvfs C: /mnt/projectdrive -o metadata,uid=1000,gid=1000,umask=022,fmask=000,case=dir
    mount --bind "${KNOVGBC_LEGACY_SOURCE_PATH:?Set a space-free Linux source path or use tools/build.ps1}" /mnt/knovGBC
fi
'@

switch ($Target) {
    'mvpfastzoomaudiomono' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastzoomaudiomono"
    }
    'audiomonotest' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/tests/picogb_057_audio_test.sh"
    }
    'mvpfastzoomaudio' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastzoomaudio"
    }
    'audiotest' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/tests/picogb_056_audio_test.sh"
    }
    'zoomtest' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/tests/vdp2_zoom_test.sh"
    }
    'fullratetest' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/tests/picogb_054_no_frameskip_parity.sh"
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/tests/picogb_055_tile_dirty_differential.sh"
    }
    'bootroms' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_bootroms_wsl.sh"
    }
    'yaul' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_yaul_wsl.sh"
    }
    'clean' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh clean"
    }
    'rebuild' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh clean"
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh package"
    }
    'diagnostic' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh diagnostic"
    }
    'bootstrap' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh bootstrap"
    }
    'bootstrapfix' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh bootstrapfix"
    }
    'bootstrapstack' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh bootstrapstack"
    }
    'bootstrapquarantine' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh bootstrapquarantine"
    }
    'masteronlyprobe' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh masteronlyprobe"
    }
    'masteronlyvideo' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh masteronlyvideo"
    }
    'masteronlyvideofix' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh masteronlyvideofix"
    }
    'bootstraptrace' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh bootstraptrace"
    }
    'stage0' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh stage0"
    }
    'stage1' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh stage1"
    }
    'stage2' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh stage2"
    }
    'masteronly' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh masteronly"
    }
    'emulatorprobe' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh emulatorprobe"
    }
    'mvp' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvp"
    }
    'mvpfast' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfast"
    }
    'mvpfastos' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastos"
    }
    'mvpfastprof' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastprof"
    }
    'mvpfastmem' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastmem"
    }
    'mvpfastppu' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastppu"
    }
    'mvpfastppufull' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastppufull"
    }
    'mvpfastsm83' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastsm83"
    }
    'mvpfastslaveppu' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastslaveppu"
    }
    'mvpfastmastervdp2' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastmastervdp2"
    }
    'mvpfastcache' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastcache"
    }
    'mvpfastfetch' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastfetch"
    }
    'mvpfastldh' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastldh"
    }
    'mvpfastanda' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastanda"
    }
    'mvpfastrepro' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastrepro"
    }
    'mvpfastfps' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastfps"
    }
    'mvpfastnext' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastnext"
    }
    'mvpfastidle' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastidle"
    }
    'mvpfastidlecache' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastidlecache"
    }
    'mvpfastidleevent' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastidleevent"
    }
    'mvpfastidleresume' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastidleresume"
    }
    'mvpfastidlecount' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastidlecount"
    }
    'mvpfastsum' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastsum"
    }
    'mvpfastidlestat' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastidlestat"
    }
    'mvpfasthramwait' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfasthramwait"
    }
    'mvpfastcpn8' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastcpn8"
    }
    'mvpfastvdp2lines' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastvdp2lines"
    }
    'mvpfastppueventcoalesce' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastppueventcoalesce"
    }
    'mvpfastpputwolines' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastpputwolines"
    }
    'mvpfastppuadaptive' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastppuadaptive"
    }
    'mvpfastppufourlines' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastppufourlines"
    }
    'mvpfastppueightlines' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastppueightlines"
    }
    'mvpfastvdp2dma' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastvdp2dma"
    }
    'mvpfastvdp2direct' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastvdp2direct"
    }
    'mvpfastsmpcgate' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastsmpcgate"
    }
    'mvpfastindexed8' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastindexed8"
    }
    'mvpfaststackpair' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfaststackpair"
    }
    'mvpfastdirtyregions' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastdirtyregions"
    }
    'mvpfastsml' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastsml"
    }
    'mvpfastzoom' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastzoom"
    }
    'mvpfastzoom3' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastzoom3"
    }
    'mvpfastzoom60' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastzoom60"
    }
    'mvpfastzoomtile' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastzoomtile"
    }
    'mvpfastvdp12' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastvdp12"
    }
    'mvpfastjrz' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastjrz"
    }
    'mvpfastcb' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastcb"
    }
    'mvpfastopprof' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastopprof"
    }
    'mvpfastf0tight' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastf0tight"
    }
    'mvpfastblock' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastblock"
    }
    'mvpfastblockc' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastblockc"
    }
    'mvpfastcold' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastcold"
    }
    'mvpfastslaveppushadow' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastslaveppushadow"
    }
    'mvpfastsched' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastsched"
    }
    'mvpfastcompact' {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh mvpfastcompact"
    }
    default {
        $linuxCommand += "`nrunuser -u knov -- /bin/bash /mnt/knovGBC/tools/build_project_wsl.sh package"
    }
}

$wsl = Join-Path $env:SystemRoot 'System32\wsl.exe'
# Windows PowerShell can retain CRLF inside the Bash here-string.
$linuxCommand = $linuxCommand.Replace("`r`n", "`n")
& $wsl @('-d', 'Ubuntu', '-u', 'root', '--', 'bash', '-lc', $linuxCommand)
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
