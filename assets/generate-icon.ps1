param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

function Get-Points([single[]]$Coordinates) {
    $points = [System.Collections.Generic.List[System.Drawing.PointF]]::new()
    for ($index = 0; $index -lt $Coordinates.Length; $index += 2) {
        $points.Add([System.Drawing.PointF]::new($Coordinates[$index], $Coordinates[$index + 1]))
    }
    return ,$points.ToArray()
}

function New-GdmlBitmap([int]$Size) {
    $bitmap = [System.Drawing.Bitmap]::new(
        $Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
    )
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $graphics.ScaleTransform($Size / 512.0, $Size / 512.0)

    $outer = Get-Points @(96,30,416,30,482,96,482,416,416,482,96,482,30,416,30,96)
    $frame = Get-Points @(103,48,409,48,464,103,464,409,409,464,103,464,48,409,48,103)
    $face = Get-Points @(117,76,395,76,436,117,436,395,395,436,117,436,76,395,76,117)
    $cubeOuter = Get-Points @(256,131,360,191,360,311,256,371,152,311,152,191)
    $cube = Get-Points @(256,147,346,199,346,303,256,355,166,303,166,199)

    $graphics.FillPolygon(
        [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255,16,22,50)),
        $outer
    )
    $graphics.DrawPolygon(
        [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255,8,16,32),16),$outer
    )
    $frameBrush = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
        [System.Drawing.PointF]::new(74,38), [System.Drawing.PointF]::new(440,474),
        [System.Drawing.ColorTranslator]::FromHtml("#a8ff3e"),
        [System.Drawing.ColorTranslator]::FromHtml("#25aaff")
    )
    $graphics.FillPolygon($frameBrush,$frame)
    $graphics.DrawPolygon(
        [System.Drawing.Pen]::new([System.Drawing.ColorTranslator]::FromHtml("#d5ff8a"),8),
        $frame
    )
    $graphics.FillPolygon(
        [System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml("#192951")),
        $face
    )
    $graphics.DrawPolygon(
        [System.Drawing.Pen]::new([System.Drawing.ColorTranslator]::FromHtml("#143051"),9),
        $face
    )
    $graphics.DrawLine(
        [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(50,255,255,255),7),
        113,92,385,92
    )
    $graphics.FillPolygon(
        [System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml("#08172d")),
        $cubeOuter
    )
    $graphics.DrawPolygon(
        [System.Drawing.Pen]::new([System.Drawing.ColorTranslator]::FromHtml("#08172d"),8),
        $cubeOuter
    )
    $cubeBrush = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
        [System.Drawing.PointF]::new(159,144), [System.Drawing.PointF]::new(353,357),
        [System.Drawing.ColorTranslator]::FromHtml("#d9ff75"),
        [System.Drawing.ColorTranslator]::FromHtml("#54baff")
    )
    $graphics.FillPolygon($cubeBrush,$cube)
    $graphics.DrawPolygon(
        [System.Drawing.Pen]::new([System.Drawing.ColorTranslator]::FromHtml("#d8ff9d"),8),
        $cube
    )

    $letter = Get-Points @(290,194,253,194,253,231,216,231,216,268,253,268,253,305,308,305,308,268,290,268)
    $shadowPen = [System.Drawing.Pen]::new(
        [System.Drawing.ColorTranslator]::FromHtml("#173357"),21
    )
    $shadowPen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Bevel
    $graphics.DrawLines($shadowPen,$letter)
    $letterPen = [System.Drawing.Pen]::new(
        [System.Drawing.ColorTranslator]::FromHtml("#f6ffdc"),12
    )
    $letterPen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Bevel
    $graphics.DrawLines($letterPen,$letter)

    $star = Get-Points @(105,119,113,135,131,138,118,150,121,168,105,159,89,168,92,150,79,138,97,135)
    $graphics.FillPolygon(
        [System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml("#faffc2")),
        $star
    )
    $star = Get-Points @(406,328,412,340,425,342,415,351,417,364,406,357,394,364,397,351,387,342,400,340)
    $graphics.FillPolygon(
        [System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml("#73fff0")),
        $star
    )
    $graphics.FillEllipse(
        [System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml("#a7ff55")),
        389,132,14,14
    )
    $graphics.FillEllipse(
        [System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml("#80faff")),
        117,374,12,12
    )

    $graphics.Dispose()
    return $bitmap
}

$sizes = @(16,24,32,48,64,128,256)
$images = [System.Collections.Generic.List[object]]::new()
foreach ($size in $sizes) {
    $bitmap = New-GdmlBitmap $size
    $stream = [System.IO.MemoryStream]::new()
    $bitmap.Save($stream,[System.Drawing.Imaging.ImageFormat]::Png)
    $images.Add(@{ Size = $size; Bytes = $stream.ToArray() })
    $stream.Dispose()
    $bitmap.Dispose()
}

$iconPath = Join-Path $OutputDirectory "gdml.ico"
$output = [System.IO.File]::Create($iconPath)
$writer = [System.IO.BinaryWriter]::new($output)
$writer.Write([uint16]0)
$writer.Write([uint16]1)
$writer.Write([uint16]$images.Count)
$offset = 6 + 16 * $images.Count
foreach ($image in $images) {
    $dimension = if ($image.Size -eq 256) { 0 } else { $image.Size }
    $writer.Write([byte]$dimension)
    $writer.Write([byte]$dimension)
    $writer.Write([byte]0)
    $writer.Write([byte]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]32)
    $writer.Write([uint32]$image.Bytes.Length)
    $writer.Write([uint32]$offset)
    $offset += $image.Bytes.Length
}
foreach ($image in $images) {
    $writer.Write($image.Bytes)
}
$writer.Dispose()
$output.Dispose()

$preview = New-GdmlBitmap 512
$preview.Save((Join-Path $OutputDirectory "gdml.png"),[System.Drawing.Imaging.ImageFormat]::Png)
$preview.Dispose()
