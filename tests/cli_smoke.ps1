# CLI smoke test for RemotePicoScope.
#
# Launches the app (if not already running), drives it entirely over the CLI
# in demo mode, and asserts on the JSON responses. Exercises every command so
# a protocol regression is caught without hardware.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tests\cli_smoke.ps1
#   powershell ... -File tests\cli_smoke.ps1 -Cli path\to\picoscope-cli.exe -App path\to\RemotePicoScope.exe
#
# Exit code 0 = all checks passed, 1 = a check failed.

param(
    [string]$Cli = "build\Release\picoscope-cli.exe",
    [string]$App = "build\Release\RemotePicoScope.exe"
)

$ErrorActionPreference = "Stop"
$script:fails = 0
$script:launched = $false

function Invoke-Cli {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$CmdArgs)
    # Capture stdout; the client prints one JSON line.
    return (& $Cli @CmdArgs 2>&1 | Out-String).Trim()
}

function Check {
    param([string]$Name, [string]$Response, [string]$MustContain)
    # Literal substring match — avoids -like treating JSON brackets ([, ]) as
    # wildcard character classes.
    if ($Response.Contains($MustContain)) {
        Write-Host "  PASS  $Name" -ForegroundColor Green
    } else {
        Write-Host "  FAIL  $Name" -ForegroundColor Red
        Write-Host "        expected to contain: $MustContain"
        Write-Host "        got: $Response"
        $script:fails++
    }
}

# --- Ensure the app is running ---
if (-not (Get-Process RemotePicoScope -ErrorAction SilentlyContinue)) {
    if (-not (Test-Path $App)) { Write-Host "App not found: $App" -ForegroundColor Red; exit 1 }
    Write-Host "Launching $App ..."
    Start-Process -FilePath (Resolve-Path $App)
    $script:launched = $true
    Start-Sleep -Seconds 3
}

Write-Host "Running CLI smoke tests..."

# --- Lifecycle / state ---
Check "version"        (Invoke-Cli --version)                       "protocol"
Check "connect --demo" (Invoke-Cli connect --demo)                  '"signal_source":"dummy"'
Start-Sleep -Milliseconds 800
Check "list-devices"   (Invoke-Cli list-devices)                    '"devices"'

$state = Invoke-Cli get-state
Check "get-state ok"       $state '"status":"ok"'
Check "get-state digital"  $state '"digital"'
Check "get-state math"     $state '"math"'
Check "get-state cursors"  $state '"cursors"'
Check "get-state siggen"   $state '"siggen"'
Check "get-state device"   $state '"device"'
Check "get-state hoffset"  $state '"horizontal_offset"'
Check "get-state trigstat" $state '"status"'

# --- Settings ---
Check "set-channel gnd/bw" (Invoke-Cli set-channel --ch B --enable --coupling gnd --bwlimit on) '"status":"ok"'
Check "set-timebase"       (Invoke-Cli set-timebase --value 0.001 --offset 0.0005)              '"horizontal_offset":0.0005'
Check "set-trigger single" (Invoke-Cli set-trigger --source A --level 0.5 --mode single)        '"status":"ok"'
Check "set-digital all"    (Invoke-Cli set-digital --ch all --enable)                           '"status":"ok"'
Check "set-cursor"         (Invoke-Cli set-cursor --enable --x1 -1.5 --x2 2.5)                  '"dt_s"'
Check "get-cursors"        (Invoke-Cli get-cursors)                                             '"freq_hz"'
Check "cursor source"      (Invoke-Cli set-cursor --source B)                                   '"source":"B"'
Check "cursor volts"       (Invoke-Cli get-cursors)                                             '"dv_v"'

# --- Wave data (needs an acquisition; ensure running) ---
Invoke-Cli run | Out-Null
Invoke-Cli set-timebase --value 0.001 --offset 0 | Out-Null
Invoke-Cli set-math --enable --op add --src1 A --src2 B | Out-Null
Start-Sleep -Milliseconds 600

Check "capture analog"     (Invoke-Cli capture --ch A --samples 5)        '"source":"A","data"'
Check "capture digital-all"(Invoke-Cli capture --ch D --samples 5)        '"source":"D","data"'
Check "capture lane"       (Invoke-Cli capture --ch D0 --samples 5)       '"source":"D0","data"'
Check "capture math"       (Invoke-Cli capture --ch MATH --samples 5)     '"source":"MATH","data"'
# NOTE: quote comma-separated source lists — PowerShell otherwise parses the
# comma as an array operator and splits them into separate arguments.
Check "capture multi"      (Invoke-Cli capture --ch "A,B,D0" --samples 4) '"channels"'

Invoke-Cli set-math --enable --op fft --src1 A | Out-Null
Start-Sleep -Milliseconds 600
Check "capture fft"        (Invoke-Cli capture --ch FFT --samples 5)      '"data_db"'
Check "fft+time rejected"  (Invoke-Cli capture --ch "A,FFT")              '"status":"error"'

# --- Measurement ---
Check "measure freq"       (Invoke-Cli measure --ch A --type frequency)   '"value"'

# --- Phase 1: probe / invert / label, autoscale, setup save/recall ---
Check "set probe/inv/lbl"  (Invoke-Cli set-channel --ch A --enable --probe 10 --invert on --label CLK) '"status":"ok"'
Check "invalid probe"      (Invoke-Cli set-channel --ch A --probe 5)      '"status":"error"'
$stateA = Invoke-Cli get-state
Check "state has probe"    $stateA '"probe":10'
Check "state has invert"   $stateA '"invert":true'
Check "state has label"    $stateA '"label":"CLK"'
Check "autoscale"          (Invoke-Cli autoscale)                         '"channels_detected"'

$setupFile = Join-Path $env:TEMP "smoke_setup.json"
Check "save-setup"         (Invoke-Cli save-setup --file $setupFile)      '"file"'
Invoke-Cli set-channel --ch A --label CHANGED --probe 1 | Out-Null
Check "recall-setup"       (Invoke-Cli recall-setup --file $setupFile)    '"file"'
Check "recall restored"    (Invoke-Cli get-state)                         '"label":"CLK"'

# --- Phase 2: digital thresholds, VCD, serial decode ---
Invoke-Cli set-digital --ch all --enable | Out-Null
Check "digital threshold"  (Invoke-Cli set-digital --threshold 3.3 --group 0)  '"threshold":[3.3'
Check "state has thr"      (Invoke-Cli get-state)                         '"threshold"'

$vcdFile = Join-Path $env:TEMP "smoke.vcd"
Check "vcd export"         (Invoke-Cli capture --ch D --file $vcdFile --samples 200) '"format":"vcd"'
Check "vcd file content"   (Get-Content $vcdFile -Raw)                    '$var wire 1'

# Decode needs a full frame in the window; 2 ms/div gives a wide capture.
Invoke-Cli set-timebase --value 0.002 | Out-Null
Start-Sleep -Milliseconds 700
Check "set-decode uart"    (Invoke-Cli set-decode --protocol uart --lane 0 --baud 9600 --enable) '"protocol":"uart"'
Check "decode uart 0x48"   (Invoke-Cli get-decode)                        '"text":"0x48"'
Check "set-decode i2c"     (Invoke-Cli set-decode --protocol i2c --scl 1 --sda 2) '"protocol":"i2c"'
Check "decode i2c addr"    (Invoke-Cli get-decode)                        'ADDR 0x50 W ACK'
Check "set-decode spi"     (Invoke-Cli set-decode --protocol spi --clk 3 --mosi 4 --cs 5) '"protocol":"spi"'
Check "decode spi 0x3C"    (Invoke-Cli get-decode)                        '"text":"0x3C"'
# get-decode must exit 0 despite frames containing an "error" field
& $Cli get-decode | Out-Null
if ($LASTEXITCODE -eq 0) { Write-Host "  PASS  decode exit code 0" -ForegroundColor Green }
else { Write-Host "  FAIL  decode exit code ($LASTEXITCODE)" -ForegroundColor Red; $script:fails++ }

# --- Probe scaling correctness (regression: transform must apply exactly once,
#     at data production — never as a repeated post-pass on a stale buffer) ---
Invoke-Cli set-channel --ch A --enable --probe 1 --invert off --offset 0 | Out-Null
Start-Sleep -Milliseconds 400
$vpp1 = (Invoke-Cli measure --ch A --type vpp | ConvertFrom-Json).value
Invoke-Cli set-channel --ch A --probe 10 | Out-Null
Start-Sleep -Milliseconds 400
$vpp10 = (Invoke-Cli measure --ch A --type vpp | ConvertFrom-Json).value
$ratio = if ($vpp1 -gt 0) { $vpp10 / $vpp1 } else { 0 }
if ($ratio -gt 9.5 -and $ratio -lt 10.5) { Write-Host "  PASS  probe scales exactly 10x (ratio=$([math]::Round($ratio,2)))" -ForegroundColor Green }
else { Write-Host "  FAIL  probe ratio $ratio (expected ~10)" -ForegroundColor Red; $script:fails++ }
# Offset must not change amplitude
Invoke-Cli set-channel --ch A --offset 1.5 | Out-Null
Start-Sleep -Milliseconds 400
$vppOff = (Invoke-Cli measure --ch A --type vpp | ConvertFrom-Json).value
if ([math]::Abs($vppOff - $vpp10) -lt 0.5) { Write-Host "  PASS  offset is amplitude-neutral" -ForegroundColor Green }
else { Write-Host "  FAIL  offset changed vpp: $vpp10 -> $vppOff" -ForegroundColor Red; $script:fails++ }
Invoke-Cli set-channel --ch A --probe 1 --offset 0 | Out-Null

# --- Record length: raised cap + memory estimate + auto mode ---
Check "reclen estimate"    (Invoke-Cli set-record-length --value 100000000) '"bytes_estimate"'
Check "reclen 512M cap"    (Invoke-Cli set-record-length --value 999999999) '"record_length":512000000'
Check "reclen fixed mode"  (Invoke-Cli get-state)                           '"record_mode":"fixed"'
Check "reclen auto"        (Invoke-Cli set-record-length --auto)            '"record_mode":"auto"'
Check "state reclen est"   (Invoke-Cli get-state)                           '"record_bytes_estimate"'
Invoke-Cli set-record-length --value 10000 | Out-Null

# --- Bus grouping ---
Check "set-bus"           (Invoke-Cli set-bus --index 0 --name CNT --lanes "6,7,8,9" --display hex --enable) '"name":"CNT"'
Check "state has bus"     (Invoke-Cli get-state)                         '"name":"CNT"'
Invoke-Cli set-timebase --value 0.005 | Out-Null
Start-Sleep -Milliseconds 700
Check "capture bus"       (Invoke-Cli capture --ch BUS0)                 '"transitions"'
Check "bus unconfigured"  (Invoke-Cli capture --ch BUS1)                 '"status":"error"'

# --- Recording (requires hardware; in demo it must fail cleanly) ---
Check "recording no hw"    (Invoke-Cli start-recording --file "$env:TEMP\x.bin" --rate 1000000) '"status":"error"'
Check "rec status idle"    (Invoke-Cli recording-status)                 '"active":false'
Check "state recording"    (Invoke-Cli get-state)                        '"recording"'

# --- Digital / pattern trigger ---
Check "trigger digital"   (Invoke-Cli set-trigger --type digital --dsource 0 --edge rising) '"status":"ok"'
Check "state trig digital" (Invoke-Cli get-state)                        '"type":"digital"'
Check "trigger pattern"   (Invoke-Cli set-trigger --type pattern --pattern "D1=1,D2=0") '"status":"ok"'
Check "state trig pattern" (Invoke-Cli get-state)                        '"pattern":"D1=1,D2=0"'
Invoke-Cli set-trigger --type edge --source A | Out-Null

# --- Cleanup ---
if ($script:launched) {
    Stop-Process -Name RemotePicoScope -Force -ErrorAction SilentlyContinue
}

Write-Host ""
if ($script:fails -eq 0) {
    Write-Host "All smoke tests passed." -ForegroundColor Green
    exit 0
} else {
    Write-Host "$($script:fails) smoke test(s) failed." -ForegroundColor Red
    exit 1
}
