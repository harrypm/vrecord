# autotest-gui.ps1 - automated freeze diagnosis for vrecord Windows GUI (exe launcher)
param(
    [string]$ExePath    = 'C:\Users\Harry\vrecord\Release\artifacts\vrecord_windows_2026-11-05\vrecord.exe',
    [string]$ActionLog  = 'C:\Users\Harry\vrecord-smoke-out\gtkdialog-actions.log',
    [string]$OutFile    = 'C:\Users\Harry\vrecord-smoke-out\autotest-report.txt'
)
$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class User32 {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
    public const uint LEFTDOWN = 0x0002, LEFTUP = 0x0004;
    public static void Click(int x, int y) {
        SetCursorPos(x, y);
        System.Threading.Thread.Sleep(60);
        mouse_event(LEFTDOWN, 0, 0, 0, UIntPtr.Zero);
        System.Threading.Thread.Sleep(40);
        mouse_event(LEFTUP, 0, 0, 0, UIntPtr.Zero);
    }
}
"@

New-Item -ItemType Directory -Force -Path (Split-Path $OutFile) | Out-Null
Remove-Item $ActionLog -ErrorAction SilentlyContinue
Remove-Item $OutFile  -ErrorAction SilentlyContinue

function Log($msg) {
    $line = "{0} {1}" -f (Get-Date -Format 'HH:mm:ss.fff'), $msg
    $line | Tee-Object -FilePath $OutFile -Append | Out-Null
}

# ---- kill leftovers (rule: always kill before starting runs) ----
Get-Process gtkdialog -ErrorAction SilentlyContinue | Stop-Process -Force
Get-Process bash -ErrorAction SilentlyContinue | Where-Object { $_.Path -like 'C:\msys64*' } | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

# ---- launch via native exe ----
$env:GTKDIALOG_ACTION_LOG = $ActionLog
Log "Launching $ExePath"
$proc = Start-Process -FilePath $ExePath -PassThru
$gd = $null
$deadline = (Get-Date).AddSeconds(60)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 500
    $gd = Get-Process gtkdialog -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($gd -and $gd.MainWindowHandle -ne 0) { break }
}
if (-not $gd) { Log "FATAL: gtkdialog never started"; exit 1 }
Log "gtkdialog pid=$($gd.Id) hwnd=$($gd.MainWindowHandle)"

# ---- Responding sampler ----
$script:freezes = New-Object System.Collections.ArrayList
function Sample-Responding([int]$seconds, [string]$phase) {
    $badSince = $null; $maxBad = [TimeSpan]::Zero; $transitions = 0
    $end = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $end) {
        Start-Sleep -Milliseconds 150
        $p = Get-Process -Id $gd.Id -ErrorAction SilentlyContinue
        if (-not $p) { Log "$phase : PROCESS GONE"; break }
        if (-not $p.Responding) {
            if (-not $badSince) { $badSince = Get-Date; $transitions++ }
        } else {
            if ($badSince) {
                $d = (Get-Date) - $badSince
                [void]$script:freezes.Add("$phase freeze $([math]::Round($d.TotalMilliseconds))ms from $($badSince.ToString('HH:mm:ss.fff'))")
                if ($d -gt $maxBad) { $maxBad = $d }
                $badSince = $null
            }
        }
    }
    if ($badSince) {
        $d = (Get-Date) - $badSince
        [void]$script:freezes.Add("$phase freeze ONGOING $($d.TotalMilliseconds)ms+ from $($badSince.ToString('HH:mm:ss.fff'))")
    }
    Log "$phase : transitions=$transitions maxFreeze=$([math]::Round($maxBad.TotalMilliseconds))ms"
}

# ---- idle baseline ----
Sample-Responding 10 "IDLE-baseline"

# ---- window rect for geometry clicks ----
$rect = New-Object User32+RECT
[User32]::GetWindowRect($gd.MainWindowHandle, [ref]$rect) | Out-Null
$W = $rect.R - $rect.L; $H = $rect.B - $rect.T
Log "window rect L=$($rect.L) T=$($rect.T) W=$W H=$H"

# ---- UIA discovery (GTK3 exposes ATK; may or may not bridge to UIA) ----
$uiaTargets = @{}
try {
    $root = [System.Windows.Automation.AutomationElement]::RootElement
    $cond = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::ProcessIdProperty, $gd.Id)
    $win = $root.FindFirst([System.Windows.Automation.TreeScope]::Children, $cond)
    if ($win) {
        $all = $win.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
        Log "UIA: found window with $($all.Count) elements"
        foreach ($e in $all) {
            $n = $e.Current.Name; $t = $e.Current.ControlType.ProgrammaticName
            if ($n -and $n.Trim()) { Log "UIA: [$t] '$n'"; $uiaTargets[$n] = $e }
        }
    } else { Log "UIA: no window found for pid" }
} catch { Log "UIA: discovery failed: $_" }

function Click-Point([int]$x, [int]$y, [string]$label) {
    Log "CLICK $label at ($x,$y)"
    [User32]::Click($x, $y)
    Start-Sleep -Milliseconds 300
    Sample-Responding 6 "POST-$label"
}
function Click-Element($e, [string]$label) {
    try {
        $pt = $e.GetClickablePoint()
        Click-Point ([int]$pt.X) ([int]$pt.Y) $label
        return
    } catch {
        try {
            $r = $e.Current.BoundingRectangle
            Click-Point ([int]($r.X + $r.Width/2)) ([int]($r.Y + $r.Height/2)) "$label-rect"
            return
        } catch { Log "CLICK $label : no clickable point ($_)"; }
    }
}

# ---- scripted interaction: safe controls only (no Record/Passthrough/Refresh/Cancel) ----
if ($uiaTargets.ContainsKey('Save Settings')) { Click-Element $uiaTargets['Save Settings'] 'SaveSettings' }
else { Click-Point ([int]($rect.L + $W*0.155)) ([int]($rect.T + $H*0.96)) 'Settings-bottombar-geom' }

if ($uiaTargets.ContainsKey('Edit Settings')) { Click-Element $uiaTargets['Edit Settings'] 'EditSettings-bigbtn' }
else { Click-Point ([int]($rect.L + $W*0.55)) ([int]($rect.T + $H*0.17)) 'EditSettings-bigbtn-geom' }

Click-Point ([int]($rect.L + $W*0.06)) ([int]($rect.T + $H*0.30)) 'notebook-tab1'
Click-Point ([int]($rect.L + $W*0.25)) ([int]($rect.T + $H*0.30)) 'notebook-tab2'

if ($uiaTargets.ContainsKey('Home')) { Click-Element $uiaTargets['Home'] 'Home-bottombar' }
else { Click-Point ([int]($rect.L + $W*0.055)) ([int]($rect.T + $H*0.96)) 'Home-bottombar-geom' }

Click-Point ([int]($rect.L + $W*0.30)) ([int]($rect.T + $H*0.60)) 'mid-canvas-click'

# ---- final state ----
$fin = Get-Process -Id $gd.Id -ErrorAction SilentlyContinue
if ($fin) { Log "final Responding=$($fin.Responding) CPU=$([math]::Round($fin.CPU,1))s WS=$([math]::Round($fin.WorkingSet64/1MB))MB" }
Log "=== FREEZE SUMMARY ($($script:freezes.Count)) ==="
foreach ($f in $script:freezes) { Log $f }

Log "=== ACTION LOG (full) ==="
if (Test-Path $ActionLog) {
    Get-Content $ActionLog | ForEach-Object { Log "ACT $_" }
} else {
    Log "ACTION LOG MISSING"
}
Log "GUI left OPEN for user inspection (pid $($gd.Id))"
"REPORT: $OutFile"
