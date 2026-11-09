# build-launcher-icon.ps1 - refresh the repo-owned launcher icon file from a
# single source PNG. This is a maintenance script, not the authoritative build
# path for app metadata.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$src256 = 'C:\Users\Harry\vrecord-build\vrecord-icon-256.png'
$outIco = 'C:\Users\Harry\vrecord\Release\vrecord.ico'
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
# compile a one-off launcher with the repo-owned resource files
# resource + windres + link into the launcher
$srcDir = 'C:\Users\Harry\vrecord\Release'
& C:\msys64\ucrt64\bin\windres.exe "$srcDir\appicon.rc" -O coff -o "$srcDir\appicon.res"
if ($LASTEXITCODE -ne 0) { throw "windres failed" }
& C:\msys64\ucrt64\bin\gcc.exe -O2 -mwindows -o "$srcDir\Vrecord.exe" "$srcDir\vrecord-launcher.c" "$srcDir\appicon.res"
if ($LASTEXITCODE -ne 0) { throw "gcc failed" }
Remove-Item "$srcDir\appicon.res" -Force -ErrorAction SilentlyContinue
Write-Output "launcher with icon: $((Get-Item "$srcDir\Vrecord.exe").Length) bytes"

Write-Output "repo launcher icon refreshed"
