# autotest-visual.ps1 - launch GUI and capture screenshots at each navigation step
param(
    [string]$ExePath = 'C:\Users\Harry\vrecord\Release\artifacts\vrecord_windows_2026-11-05\vrecord.exe',
    [string]$ShotDir = 'C:\Users\Harry\vrecord-smoke-out'
)
$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class U2 {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
    public static void Click(int x, int y) {
        SetCursorPos(x, y); System.Threading.Thread.Sleep(80);
        mouse_event(0x0002,0,0,0,UIntPtr.Zero); System.Threading.Thread.Sleep(50);
        mouse_event(0x0004,0,0,0,UIntPtr.Zero);
    }
}
"@
function Shot([string]$name, [int]$wx, [int]$wy, [int]$ww, [int]$wh) {
    $bmp = New-Object System.Drawing.Bitmap($ww, $wh)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($wx, $wy, 0, 0, $bmp.Size)
    $bmp.Save("$ShotDir\$name.png", [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    Write-Output "shot: $name.png"
}

# kill + relaunch
Get-Process gtkdialog -ErrorAction SilentlyContinue | Stop-Process -Force
Get-Process bash -ErrorAction SilentlyContinue | Where-Object { $_.Path -like 'C:\msys64*' } | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep 1
Start-Process -FilePath $ExePath | Out-Null
$gd = $null
$deadline = (Get-Date).AddSeconds(60)
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 500
    $gd = Get-Process gtkdialog -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($gd -and $gd.MainWindowHandle -ne 0) { break }
}
if (-not $gd) { Write-Output "FATAL: no gtkdialog"; return }
Start-Sleep -Seconds 8

$r = New-Object U2+RECT
[U2]::GetWindowRect($gd.MainWindowHandle, [ref]$r) | Out-Null
$W = $r.R - $r.L; $H = $r.B - $r.T
Write-Output "window W=$W H=$H responding=$($gd.Responding)"
Shot "01-main" $r.L $r.T $W $H

# Edit Settings big button (main view, middle of 6 buttons)
[U2]::Click([int]($r.L + $W*0.55), [int]($r.T + $H*0.17)); Start-Sleep -Seconds 3
Shot "02-inputsettings" $r.L $r.T $W $H

# outer notebook: Config tab (4th of 6)
[U2]::Click([int]($r.L + $W*0.199), [int]($r.T + $H*0.057)); Start-Sleep -Seconds 3
Shot "03-config" $r.L $r.T $W $H

# inner prefs notebook: Themes tab (last, ~0.50)
[U2]::Click([int]($r.L + $W*0.50), [int]($r.T + $H*0.105)); Start-Sleep -Seconds 3
Shot "04-themes" $r.L $r.T $W $H

# Decklink tab (1st)
[U2]::Click([int]($r.L + $W*0.043), [int]($r.T + $H*0.057)); Start-Sleep -Seconds 3
Shot "05-decklink" $r.L $r.T $W $H

$gd.Refresh()
Write-Output "final responding=$($gd.Responding) cpu=$([math]::Round($gd.CPU,1))s"
Write-Output "GUI left OPEN (pid $($gd.Id))"
