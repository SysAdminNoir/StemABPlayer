# Renders the app icon ("Monogram key": the wordmark's a/b on a graphite hardware key) to PNGs
# with GDI+ and the bundled Archivo ExtraBold. Output: resources/icon/icon-<size>.png
# Run from anywhere:  pwsh tools/make-icon.ps1
Add-Type -AssemblyName System.Drawing

$root  = Split-Path -Parent $PSScriptRoot
$font  = Join-Path $root 'resources/fonts/Archivo-ExtraBold.ttf'
$out   = Join-Path $root 'resources/icon'
New-Item -ItemType Directory -Force $out | Out-Null

$pfc = New-Object System.Drawing.Text.PrivateFontCollection
$pfc.AddFontFile($font)
$family = $pfc.Families[0]

function RoundRect([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = 2 * $r
    $p.AddArc($x, $y, $d, $d, 180, 90)
    $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure()
    return $p
}

function Render([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.TextRenderingHint = 'AntiAliasGridFit'
    $g.PixelOffsetMode = 'HighQuality'
    $g.Clear([System.Drawing.Color]::Transparent)
    $k = $size / 256.0
    $g.ScaleTransform($k, $k)

    # key shadow, face (vertical gradient), rim
    $g.FillPath((New-Object System.Drawing.SolidBrush ([System.Drawing.ColorTranslator]::FromHtml('#121315'))), (RoundRect 8 14 240 234 44))
    $face = RoundRect 8 8 240 232 44
    $grad = New-Object System.Drawing.Drawing2D.LinearGradientBrush ([System.Drawing.PointF]::new(0, 8)), ([System.Drawing.PointF]::new(0, 240)),
            ([System.Drawing.ColorTranslator]::FromHtml('#45484E')), ([System.Drawing.ColorTranslator]::FromHtml('#2E3035'))
    $g.FillPath($grad, $face)
    $rimPen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(178, 106, 110, 116)), ([Math]::Max(1.0, 1.0 / $k))
    $g.DrawPath($rimPen, (RoundRect 8.5 8.5 239 231 43.5))

    # "a" ivory + "/b" red, baseline at y = 160, centred, 118 px, tracking -4
    $em = 118.0
    $fmt = [System.Drawing.StringFormat]::GenericTypographic
    $fnt = New-Object System.Drawing.Font $family, $em, ([System.Drawing.FontStyle]::Regular), ([System.Drawing.GraphicsUnit]::Pixel)
    $aW = $g.MeasureString('a', $fnt, 1000, $fmt).Width
    $bW = $g.MeasureString('/b', $fnt, 1000, $fmt).Width
    $gap = -4.0
    $x0 = 128.0 - ($aW + $gap + $bW) / 2.0
    $ascent = $em * $family.GetCellAscent('Regular') / $family.GetEmHeight('Regular')
    $top = 160.0 - $ascent
    $g.DrawString('a',  $fnt, (New-Object System.Drawing.SolidBrush ([System.Drawing.ColorTranslator]::FromHtml('#E7E2D2'))), $x0, $top, $fmt)
    $g.DrawString('/b', $fnt, (New-Object System.Drawing.SolidBrush ([System.Drawing.ColorTranslator]::FromHtml('#E8402F'))), ($x0 + $aW + $gap), $top, $fmt)

    $g.Dispose()
    $path = Join-Path $out "icon-$size.png"
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $path
}

foreach ($s in 512, 64) { Render $s }
