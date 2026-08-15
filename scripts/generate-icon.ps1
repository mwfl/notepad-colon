param(
    [string]$OutputDirectory = (Join-Path $PSScriptRoot "..\assets")
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$output = [System.IO.Path]::GetFullPath($OutputDirectory)
[System.IO.Directory]::CreateDirectory($output) | Out-Null

$sizes = @(16, 24, 32, 48, 64, 128, 256)
$pngs = @()

foreach ($size in $sizes) {
    $bitmap = [System.Drawing.Bitmap]::new($size, $size)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.Clear([System.Drawing.Color]::Transparent)

    $scale = $size / 256.0
    $rounding = [Math]::Max(2, 52 * $scale)
    $bounds = [System.Drawing.RectangleF]::new(1 * $scale, 1 * $scale, 254 * $scale, 254 * $scale)
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $diameter = 2 * $rounding
    $path.AddArc($bounds.X, $bounds.Y, $diameter, $diameter, 180, 90)
    $path.AddArc($bounds.Right - $diameter, $bounds.Y, $diameter, $diameter, 270, 90)
    $path.AddArc($bounds.Right - $diameter, $bounds.Bottom - $diameter, $diameter, $diameter, 0, 90)
    $path.AddArc($bounds.X, $bounds.Bottom - $diameter, $diameter, $diameter, 90, 90)
    $path.CloseFigure()

    $background = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
        $bounds,
        [System.Drawing.Color]::FromArgb(23, 42, 70),
        [System.Drawing.Color]::FromArgb(7, 20, 38),
        45.0)
    $graphics.FillPath($background, $path)

    $white = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(243, 247, 252),
                                       [Math]::Max(1.5, 18 * $scale))
    $white.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $white.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $white.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $accent = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(35, 177, 249))
    $graphics.DrawLine($white, 60 * $scale, 190 * $scale, 60 * $scale, 66 * $scale)
    $graphics.DrawLine($white, 60 * $scale, 66 * $scale, 124 * $scale, 190 * $scale)
    $graphics.DrawLine($white, 124 * $scale, 190 * $scale, 124 * $scale, 66 * $scale)
    $graphics.FillEllipse($accent, 145 * $scale, 79 * $scale, 26 * $scale, 26 * $scale)
    $graphics.FillEllipse($accent, 145 * $scale, 157 * $scale, 26 * $scale, 26 * $scale)
    $graphics.FillEllipse($accent, 189 * $scale, 79 * $scale, 26 * $scale, 26 * $scale)
    $graphics.FillEllipse($accent, 189 * $scale, 157 * $scale, 26 * $scale, 26 * $scale)

    $pngPath = Join-Path $output "notepad-colon-$size.png"
    $bitmap.Save($pngPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $pngs += $pngPath

    $accent.Dispose()
    $white.Dispose()
    $background.Dispose()
    $path.Dispose()
    $graphics.Dispose()
    $bitmap.Dispose()
}

$icoPath = Join-Path $output "notepad-colon.ico"
$sourceBitmap = [System.Drawing.Bitmap]::FromFile((Join-Path $output "notepad-colon-256.png"))
$iconHandle = $sourceBitmap.GetHicon()
$icon = [System.Drawing.Icon]::FromHandle($iconHandle)
$stream = [System.IO.File]::Create($icoPath)
try {
    $icon.Save($stream)
}
finally {
    $stream.Dispose()
    $icon.Dispose()
    $sourceBitmap.Dispose()
}

Write-Host "Generated $icoPath"

$toolbar = [System.Drawing.Bitmap]::new(
    140, 20, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
$canvas = [System.Drawing.Graphics]::FromImage($toolbar)
$canvas.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$key = [System.Drawing.Color]::FromArgb(255, 0, 255)
$ink = [System.Drawing.Color]::FromArgb(42, 55, 70)
$canvas.Clear($key)
$pen = [System.Drawing.Pen]::new($ink, 1.6)
$pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
$pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
$brush = [System.Drawing.SolidBrush]::new($ink)

function X([int]$icon, [float]$value) { return ($icon * 20) + $value }

# New document
$canvas.DrawRectangle($pen, (X 0 5), 3, 10, 14)
$canvas.DrawLine($pen, (X 0 11), 3, (X 0 15), 7)
$canvas.DrawLine($pen, (X 0 11), 3, (X 0 11), 7)
$canvas.DrawLine($pen, (X 0 11), 7, (X 0 15), 7)
$canvas.DrawLine($pen, (X 0 6), 12, (X 0 12), 12)
$canvas.DrawLine($pen, (X 0 9), 9, (X 0 9), 15)

# Open file
$canvas.DrawRectangle($pen, (X 1 3), 5, 6, 4)
$canvas.DrawLine($pen, (X 1 3), 8, (X 1 17), 8)
$canvas.DrawLine($pen, (X 1 17), 8, (X 1 14), 16)
$canvas.DrawLine($pen, (X 1 14), 16, (X 1 4), 16)
$canvas.DrawLine($pen, (X 1 4), 16, (X 1 3), 8)

# Save
$canvas.DrawRectangle($pen, (X 2 3), 3, 14, 14)
$canvas.DrawRectangle($pen, (X 2 6), 3, 7, 5)
$canvas.DrawRectangle($pen, (X 2 6), 11, 8, 6)
$canvas.FillRectangle($brush, (X 2 11), 4, 1.5, 3)

# Undo and redo
$canvas.DrawArc($pen, (X 3 4), 5, 12, 10, 205, 250)
$canvas.DrawLine($pen, (X 3 4), 5, (X 3 4), 11)
$canvas.DrawLine($pen, (X 3 4), 5, (X 3 10), 5)
$canvas.DrawArc($pen, (X 4 4), 5, 12, 10, 85, 250)
$canvas.DrawLine($pen, (X 4 16), 5, (X 4 16), 11)
$canvas.DrawLine($pen, (X 4 16), 5, (X 4 10), 5)

# Find
$canvas.DrawEllipse($pen, (X 5 3), 3, 10, 10)
$canvas.DrawLine($pen, (X 5 12), 12, (X 5 17), 17)

# Open folder
$canvas.DrawLine($pen, (X 6 2), 6, (X 6 8), 6)
$canvas.DrawLine($pen, (X 6 2), 6, (X 6 2), 16)
$canvas.DrawLine($pen, (X 6 2), 16, (X 6 15), 16)
$canvas.DrawLine($pen, (X 6 15), 16, (X 6 18), 8)
$canvas.DrawLine($pen, (X 6 18), 8, (X 6 9), 8)
$canvas.DrawLine($pen, (X 6 9), 8, (X 6 7), 4)
$canvas.DrawLine($pen, (X 6 7), 4, (X 6 2), 4)

$toolbarPath = Join-Path $output "toolbar.bmp"
$toolbar.Save($toolbarPath, [System.Drawing.Imaging.ImageFormat]::Bmp)
$brush.Dispose()
$pen.Dispose()
$canvas.Dispose()
$toolbar.Dispose()
Write-Host "Generated $toolbarPath"
