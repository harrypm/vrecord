# build-launcher-icon.ps1 - assemble a multi-size ICO from vrecord-icon-256.png
# (PNG-in-ICO, Vista+) and compile the launcher with it via windres.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$src256 = 'C:\Users\Harry\vrecord-build\vrecord-icon-256.png'
$outIco = 'C:\Users\Harry\vrecord-build\vrecord.ico'
$sizes  = 16,24,32,48,64,128,256

$src = [System.Drawing.Image]::FromFile($src256)
$pngs = @()
foreach ($s in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap($s, $s)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = 'HighQualityBicubic'
    $g.DrawImage($src, 0, 0, $s, $s)
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $pngs += ,$ms.ToArray()
    $g.Dispose(); $bmp.Dispose(); $ms.Dispose()
}
$src.Dispose()

# ICO container
$msIco = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter($msIco)
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]
    $bw.Write([byte]($(if ($s -ge 256) { 0 } else { $s })))
    $bw.Write([byte]($(if ($s -ge 256) { 0 } else { $s })))
    $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([uint16]1); $bw.Write([uint16]32)
    $bw.Write([uint32]$pngs[$i].Length)
    $bw.Write([uint32]$offset)
    $offset += $pngs[$i].Length
}
foreach ($p in $pngs) { $bw.Write($p) }
$bw.Flush()
[System.IO.File]::WriteAllBytes($outIco, $msIco.ToArray())
$bw.Dispose()
Write-Output "ico: $((Get-Item $outIco).Length) bytes"

# resource + windres + link into the launcher
$srcDir = 'C:\Users\Harry\vrecord\Release'
Copy-Item $outIco "$srcDir\vrecord.ico" -Force
Set-Content -Path "$srcDir\appicon.rc" -Value '1 ICON "vrecord.ico"' -Encoding ASCII
& C:\msys64\ucrt64\bin\windres.exe "$srcDir\appicon.rc" -O coff -o "$srcDir\appicon.res"
if ($LASTEXITCODE -ne 0) { throw "windres failed" }
& C:\msys64\ucrt64\bin\gcc.exe -O2 -o "$srcDir\vrecord.exe" "$srcDir\vrecord-launcher.c" "$srcDir\appicon.res"
if ($LASTEXITCODE -ne 0) { throw "gcc failed" }
Write-Output "launcher with icon: $((Get-Item "$srcDir\vrecord.exe").Length) bytes"

# deploy: exe + window icon PNG into both portables
foreach ($d in 'C:\Users\Harry\vrecord\Release\artifacts\vrecord_windows_2026-11-05','C:\Users\Harry\vrecord\Release\artifacts\vrecord_windows_portable') {
    Copy-Item "$srcDir\vrecord.exe" "$d\vrecord.exe" -Force
    Copy-Item 'C:\Users\Harry\vrecord-build\vrecord-icon-256.png' "$d\Resources\vrecord-icon.png" -Force
}
Write-Output "deployed to both portables"
