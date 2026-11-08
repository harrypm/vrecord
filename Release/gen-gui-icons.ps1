# gen-gui-icons.ps1 - draw the gtk stock icons gtkdialog references as simple
# 24x24 PNGs so the Windows GUI buttons have real icons (Adwaita on MSYS2 does
# not ship them and the native gtkdialog cannot resolve MSYS theme paths).
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$targets = @(
    'C:\Users\Harry\vrecord\Release\artifacts\vrecord_windows_2026-11-05\Resources\icons',
    'C:\Users\Harry\vrecord\Release\artifacts\vrecord_windows_portable\Resources\icons'
)
$fg   = [System.Drawing.Color]::FromArgb(216,216,216)  # light gray for dark theme
$green= [System.Drawing.Color]::FromArgb(76,201,76)
$red  = [System.Drawing.Color]::FromArgb(224,76,76)
$S=24
function New-Icon([string]$name, [scriptblock]$draw) {
    foreach ($t in $targets) {
        New-Item -ItemType Directory -Force -Path $t | Out-Null
        $bmp = New-Object System.Drawing.Bitmap($S, $S)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.SmoothingMode = 'AntiAlias'
        $pen = New-Object System.Drawing.Pen($fg, 2)
        $brush = New-Object System.Drawing.SolidBrush($fg)
        & $draw $g $pen $brush
        $g.Dispose()
        $bmp.Save("$t\$name.png", [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
    }
    Write-Output "icon: $name.png"
}
function Tri($g,$b,[object]$flat) {
    $pts = New-Object 'System.Drawing.PointF[]' ([int]($flat.Count/2))
    for ($i = 0; $i -lt $pts.Length; $i++) {
        $pts[$i] = New-Object System.Drawing.PointF([float]$flat[$i*2], [float]$flat[$i*2+1])
    }
    $g.FillPolygon($b, $pts)
}

New-Icon 'gtk-index' { param($g,$pen,$brush)         # Home: house
    Tri $g $brush @(12,3, 22,12, 2,12)
    $g.FillRectangle($brush, 5, 12, 14, 9)
}
New-Icon 'gtk-open' { param($g,$pen,$brush)          # folder
    $g.FillRectangle($brush, 2, 7, 9, 3)
    $g.FillRectangle($brush, 2, 9, 20, 9)
    Tri $g $brush @(2,18, 22,18, 20,10, 8,10)
}
New-Icon 'gtk-save' { param($g,$pen,$brush)          # floppy
    $g.FillRectangle($brush, 3, 3, 18, 18)
    $g.FillRectangle([System.Drawing.Brushes]::Black, 7, 3, 10, 7)
    $g.FillRectangle($brush, 8, 14, 8, 7)
    $g.FillRectangle([System.Drawing.Brushes]::Black, 13, 4, 3, 5)
}
New-Icon 'gtk-refresh' { param($g,$pen,$brush)       # circular arrow
    $g.DrawArc($pen, 4, 4, 16, 16, 220, 280)
    Tri $g $brush @(18,2, 22,8, 15,8)
}
New-Icon 'gtk-media-record' { param($g,$pen,$brush)
    $brush.Color = $red; $g.FillEllipse($brush, 4, 4, 16, 16)
}
New-Icon 'gtk-media-play' { param($g,$pen,$brush)
    Tri $g $brush @(7,4, 20,12, 7,20)
}
New-Icon 'gtk-media-stop' { param($g,$pen,$brush)
    $g.FillRectangle($brush, 5, 5, 14, 14)
}
New-Icon 'gtk-media-rewind' { param($g,$pen,$brush)
    Tri $g $brush @(12,5, 3,12, 12,19); Tri $g $brush @(22,5, 13,12, 22,19)
}
New-Icon 'gtk-media-forward' { param($g,$pen,$brush)
    Tri $g $brush @(2,5, 11,12, 2,19); Tri $g $brush @(12,5, 21,12, 12,19)
}
New-Icon 'gtk-media-previous' { param($g,$pen,$brush)
    Tri $g $brush @(19,5, 8,12, 19,19); $g.FillRectangle($brush, 4, 5, 3, 14)
}
New-Icon 'gtk-media-next' { param($g,$pen,$brush)
    Tri $g $brush @(5,5, 16,12, 5,19); $g.FillRectangle($brush, 17, 5, 3, 14)
}
New-Icon 'gtk-execute' { param($g,$pen,$brush)       # gear-ish: circle + spokes
    $g.DrawEllipse($pen, 7, 7, 10, 10)
    foreach ($a in 0..7) {
        $rad = $a * [math]::PI / 4
        $x1 = 12 + [math]::Cos($rad) * 6; $y1 = 12 + [math]::Sin($rad) * 6
        $x2 = 12 + [math]::Cos($rad) * 10; $y2 = 12 + [math]::Sin($rad) * 10
        $g.DrawLine($pen, [float]$x1, [float]$y1, [float]$x2, [float]$y2)
    }
}
New-Icon 'gtk-yes' { param($g,$pen,$brush)
    $brush.Color = $green; $g.FillEllipse($brush, 3, 3, 18, 18)
}
New-Icon 'gtk-no' { param($g,$pen,$brush)
    $brush.Color = $red; $g.FillEllipse($brush, 3, 3, 18, 18)
}
Write-Output "done"
