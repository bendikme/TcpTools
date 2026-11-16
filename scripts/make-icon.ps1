# =============================================================================
#  make-icon.ps1  --  draws the TcpTools icon and writes
#     TcpTools\app.ico   (16 ... 256 px; 256 px as PNG, the rest as 32-bit bitmaps)
#     TcpTools\app.png   (256 px, for the DYNATEC Portal tile)
#
#  The icon: a blue rounded square with two opposite arrows (data out, data
#  back). Every size is drawn as vectors at its own resolution, not scaled
#  down from a big bitmap, so the small sizes stay crisp.
#
#  Usage:  powershell -ExecutionPolicy Bypass -File scripts\make-icon.ps1
# =============================================================================

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$root    = Split-Path $PSScriptRoot -Parent
$icoPath = Join-Path $root "TcpTools\app.ico"
$pngPath = Join-Path $root "TcpTools\app.png"

function New-RoundedRect([single]$x, [single]$y, [single]$w, [single]$h, [single]$r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = 2 * $r
    $p.AddArc($x,           $y,           $d, $d, 180, 90)
    $p.AddArc($x + $w - $d, $y,           $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d,   0, 90)
    $p.AddArc($x,           $y + $h - $d, $d, $d,  90, 90)
    $p.CloseFigure()
    return $p
}

# One arrow: a shaft from (x1,y) to (x2,y) with a chevron head at (x2,y).
function Add-Arrow($g, $pen, [single]$x1, [single]$x2, [single]$y, [single]$head) {
    $dir = if ($x2 -gt $x1) { 1 } else { -1 }
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddLine($x1, $y, $x2, $y)
    $path.StartFigure()
    $path.AddLine([single]($x2 - $dir * $head), [single]($y - $head), $x2, $y)
    $path.AddLine($x2, $y, [single]($x2 - $dir * $head), [single]($y + $head))
    $g.DrawPath($pen, $path)
    $path.Dispose()
}

function New-IconBitmap([int]$s) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode   = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)

    # Background: rounded square, blue gradient (lighter at the top).
    $inset  = [single]($s * 0.03)
    $side   = [single]($s - 2 * $inset)
    $radius = [single]($s * 0.22)
    $bg     = New-RoundedRect $inset $inset $side $side $radius
    $rect   = New-Object System.Drawing.RectangleF 0, 0, $s, $s
    $brush  = New-Object System.Drawing.Drawing2D.LinearGradientBrush $rect,
                  ([System.Drawing.Color]::FromArgb(255, 64, 150, 250)),
                  ([System.Drawing.Color]::FromArgb(255, 24,  78, 196)), 90.0
    $g.FillPath($brush, $bg)

    # Two arrows: out (white, to the right) and back (light cyan, to the left).
    $stroke = [single][Math]::Max(1.7, $s * 0.105)
    $head   = [single]($s * 0.145)
    $left   = [single]($s * 0.23)
    $right  = [single]($s * 0.77)
    $yTop   = [single]($s * 0.36)
    $yBot   = [single]($s * 0.64)

    $penOut = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 255, 255, 255)), $stroke
    $penIn  = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 170, 232, 255)), $stroke
    foreach ($pen in @($penOut, $penIn)) {
        $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
        $pen.EndCap   = [System.Drawing.Drawing2D.LineCap]::Round
        $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    }
    Add-Arrow $g $penOut $left  $right $yTop $head
    Add-Arrow $g $penIn  $right $left  $yBot $head

    $penOut.Dispose(); $penIn.Dispose(); $brush.Dispose(); $bg.Dispose(); $g.Dispose()
    return $bmp
}

function Get-PngBytes($bmp) {
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bytes = $ms.ToArray()
    $ms.Dispose()
    return ,$bytes
}

# Classic icon image: BITMAPINFOHEADER + 32-bit BGRA rows (bottom-up) + an
# all-zero 1-bit AND mask. Every icon reader understands it; PNG entries are
# only reliable for the 256 px image.
function Get-DibBytes($bmp) {
    $s    = $bmp.Width
    $rect = New-Object System.Drawing.Rectangle 0, 0, $s, $s
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                          [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $pixels = New-Object byte[] ($s * $s * 4)
    for ($y = 0; $y -lt $s; $y++) {
        $src = [IntPtr]::Add($data.Scan0, $y * $data.Stride)
        [System.Runtime.InteropServices.Marshal]::Copy($src, $pixels, ($s - 1 - $y) * $s * 4, $s * 4)
    }
    $bmp.UnlockBits($data)

    $maskStride = [int]([Math]::Ceiling($s / 32.0) * 4)
    $ms = New-Object System.IO.MemoryStream
    $w  = New-Object System.IO.BinaryWriter $ms
    $w.Write([uint32]40); $w.Write([int32]$s); $w.Write([int32]($s * 2))   # height counts XOR + AND
    $w.Write([uint16]1);  $w.Write([uint16]32); $w.Write([uint32]0)
    $w.Write([uint32]($pixels.Length + $maskStride * $s))
    $w.Write([int32]0); $w.Write([int32]0); $w.Write([uint32]0); $w.Write([uint32]0)
    $w.Write($pixels)
    $w.Write((New-Object byte[] ($maskStride * $s)))
    $w.Flush()
    $bytes = $ms.ToArray()
    $w.Dispose()
    return ,$bytes
}

# ---- Build the .ico (ICONDIR + ICONDIRENTRY[] + images) -----------------------
$sizes  = 16, 20, 24, 32, 40, 48, 64, 128, 256
$images = @()
foreach ($s in $sizes) {
    $bmp = New-IconBitmap $s
    if ($s -ge 256) { $images += ,(Get-PngBytes $bmp) } else { $images += ,(Get-DibBytes $bmp) }
    if ($s -eq 256) { $bmp.Save($pngPath, [System.Drawing.Imaging.ImageFormat]::Png) }
    $bmp.Dispose()
}

$out = New-Object System.IO.MemoryStream
$bw  = New-Object System.IO.BinaryWriter $out
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]
    $dim = if ($s -ge 256) { 0 } else { $s }          # 0 means 256 in an ICONDIRENTRY
    $bw.Write([byte]$dim); $bw.Write([byte]$dim)        # width, height
    $bw.Write([byte]0);    $bw.Write([byte]0)           # palette, reserved
    $bw.Write([uint16]1);  $bw.Write([uint16]32)        # planes, bits per pixel
    $bw.Write([uint32]$images[$i].Length)
    $bw.Write([uint32]$offset)
    $offset += $images[$i].Length
}
foreach ($img in $images) { $bw.Write($img) }
$bw.Flush()
[System.IO.File]::WriteAllBytes($icoPath, $out.ToArray())
$bw.Dispose()

"{0}  ({1} sizes, {2} bytes)" -f $icoPath, $sizes.Count, (Get-Item $icoPath).Length
"{0}  ({1} bytes)" -f $pngPath, (Get-Item $pngPath).Length
