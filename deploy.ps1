# Deploy the built dinput8.dll into the Centurion client.
#
# The log is ARCHIVED, never deleted: clearing it on deploy once destroyed a
# reproduction the user had just captured. Old logs cost nothing to keep.
param([switch]$KeepLog)

$src    = Join-Path $PSScriptRoot "out\dinput8.dll"
$dstDir = "C:\Projects\Gamedev\wow\clients\centurion"
$dst    = Join-Path $dstDir "dinput8.dll"
$log    = Join-Path $dstDir "AnimSpeedFix.log"

$proc = Get-Process Wow -ErrorAction SilentlyContinue
if ($proc) {
    "client still running (pids $($proc.Id -join ', ')) - dinput8.dll is locked, nothing copied"
    exit 1
}

Copy-Item $src $dst -Force
$ok = (Get-FileHash $src).Hash -eq (Get-FileHash $dst).Hash

if ((Test-Path $log) -and -not $KeepLog) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    Move-Item -LiteralPath $log -Destination (Join-Path $dstDir "AnimSpeedFix-$stamp.log") -Force
    "previous log archived as AnimSpeedFix-$stamp.log"
}

"deployed (hash match: $ok)"
