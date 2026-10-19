param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$size = 48
$bitmap = [System.Drawing.Bitmap]::new(
    $size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
$graphics.ScaleTransform($size / 256.0, $size / 256.0)

$bounds = [System.Drawing.Rectangle]::new(8, 8, 240, 240)
$graphics.FillEllipse(
    [System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml("#10162e")),
    $bounds
)
$graphics.DrawEllipse(
    [System.Drawing.Pen]::new([System.Drawing.ColorTranslator]::FromHtml("#071126"), 8),
    $bounds
)

$face = [System.Drawing.Rectangle]::new(20, 20, 216, 216)
$faceBrush = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
    [System.Drawing.Point]::new(34, 20), [System.Drawing.Point]::new(222, 236),
    [System.Drawing.ColorTranslator]::FromHtml("#a8ff3e"),
    [System.Drawing.ColorTranslator]::FromHtml("#25aaff")
)
$graphics.FillEllipse($faceBrush, $face)
$graphics.DrawEllipse(
    [System.Drawing.Pen]::new([System.Drawing.ColorTranslator]::FromHtml("#d5ff8a"), 7),
    $face
)

$cube = [System.Drawing.PointF[]]@(
    [System.Drawing.PointF]::new(128, 57),
    [System.Drawing.PointF]::new(190, 93),
    [System.Drawing.PointF]::new(190, 163),
    [System.Drawing.PointF]::new(128, 199),
    [System.Drawing.PointF]::new(66, 163),
    [System.Drawing.PointF]::new(66, 93)
)
$graphics.FillPolygon(
    [System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml("#14264b")),
    $cube
)
$graphics.DrawPolygon(
    [System.Drawing.Pen]::new([System.Drawing.ColorTranslator]::FromHtml("#08172d"), 6),
    $cube
)
$graphics.DrawLine(
    [System.Drawing.Pen]::new([System.Drawing.ColorTranslator]::FromHtml("#8cecff"), 6),
    128, 57, 128, 128
)
$graphics.DrawLine(
    [System.Drawing.Pen]::new([System.Drawing.ColorTranslator]::FromHtml("#8cecff"), 6),
    66, 93, 128, 128
)
$graphics.DrawLine(
    [System.Drawing.Pen]::new([System.Drawing.ColorTranslator]::FromHtml("#8cecff"), 6),
    190, 93, 128, 128
)

$letter = [System.Drawing.PointF[]]@(
    [System.Drawing.PointF]::new(147, 103),
    [System.Drawing.PointF]::new(112, 103),
    [System.Drawing.PointF]::new(112, 125),
    [System.Drawing.PointF]::new(91, 125),
    [System.Drawing.PointF]::new(91, 148),
    [System.Drawing.PointF]::new(112, 148),
    [System.Drawing.PointF]::new(112, 170),
    [System.Drawing.PointF]::new(152, 170),
    [System.Drawing.PointF]::new(152, 148),
    [System.Drawing.PointF]::new(137, 148)
)
$shadow = [System.Drawing.Pen]::new(
    [System.Drawing.ColorTranslator]::FromHtml("#08172d"), 13
)
$shadow.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Bevel
$graphics.DrawLines($shadow, $letter)
$mark = [System.Drawing.Pen]::new(
    [System.Drawing.ColorTranslator]::FromHtml("#f6ffdc"), 7
)
$mark.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Bevel
$graphics.DrawLines($mark, $letter)

$highlight = [System.Drawing.Pen]::new(
    [System.Drawing.Color]::FromArgb(100, 255, 255, 255), 4
)
$graphics.DrawArc($highlight, 34, 26, 188, 188, 205, 105)

$graphics.Dispose()
$faceBrush.Dispose()
$bitmap.Save($OutputPath, [System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()
