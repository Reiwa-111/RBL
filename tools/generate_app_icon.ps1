param(
    [string]$AssetsDir = (Join-Path $PSScriptRoot '..\assets')
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$assets = [System.IO.Path]::GetFullPath($AssetsDir)
if (-not (Test-Path -LiteralPath $assets -PathType Container)) {
    throw "Assets directory does not exist: $assets"
}

function New-RblIconPng([int]$Size) {
    $renderSize = $Size * 4
    $render = New-Object System.Drawing.Bitmap($renderSize, $renderSize,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($render)
    try {
        $graphics.Clear([System.Drawing.Color]::Transparent)
        $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit

        $inset = [Math]::Max(2.0, $renderSize * 0.055)
        $bounds = New-Object System.Drawing.RectangleF($inset, $inset,
            ($renderSize - 2 * $inset), ($renderSize - 2 * $inset))
        $fill = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 79, 50, 116))
        $outline = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 201, 168, 240),
            [Math]::Max(2.0, $renderSize * 0.018))
        $graphics.FillEllipse($fill, $bounds)
        $graphics.DrawEllipse($outline, $bounds)
        $fill.Dispose()
        $outline.Dispose()

        $font = New-Object System.Drawing.Font('Arial', ($renderSize * 0.70),
            [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
        $format = New-Object System.Drawing.StringFormat
        $format.Alignment = [System.Drawing.StringAlignment]::Center
        $format.LineAlignment = [System.Drawing.StringAlignment]::Center
        $format.Trimming = [System.Drawing.StringTrimming]::None
        $white = [System.Drawing.Brushes]::White
        $graphics.DrawString('R', $font, $white,
            (New-Object System.Drawing.RectangleF(0, 0, $renderSize, $renderSize)), $format)
        $font.Dispose()
        $format.Dispose()

        $result = New-Object System.Drawing.Bitmap($Size, $Size,
            [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $downsample = [System.Drawing.Graphics]::FromImage($result)
        try {
            $downsample.Clear([System.Drawing.Color]::Transparent)
            $downsample.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $downsample.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
            $downsample.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $downsample.DrawImage($render, 0, 0, $Size, $Size)
        }
        finally {
            $downsample.Dispose()
        }

        $stream = New-Object System.IO.MemoryStream
        $result.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
        $bytes = $stream.ToArray()
        $stream.Dispose()
        $result.Dispose()
        return ,$bytes
    }
    finally {
        $graphics.Dispose()
        $render.Dispose()
    }
}

$sizes = @(16, 24, 32, 48, 64, 128, 256)
$pngs = @{}
foreach ($size in $sizes) {
    $bytes = New-RblIconPng $size
    $pngs[$size] = $bytes
    [System.IO.File]::WriteAllBytes((Join-Path $assets "rbl_$size.png"), $bytes)
}

$icoPath = Join-Path $assets 'rblstudio.ico'
$stream = New-Object System.IO.FileStream($icoPath, [System.IO.FileMode]::Create,
    [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
$writer = New-Object System.IO.BinaryWriter($stream)
try {
    $writer.Write([UInt16]0)
    $writer.Write([UInt16]1)
    $writer.Write([UInt16]$sizes.Count)
    $offset = 6 + (16 * $sizes.Count)
    foreach ($size in $sizes) {
        $bytes = $pngs[$size]
        $dimension = if ($size -eq 256) { [byte]0 } else { [byte]$size }
        $writer.Write($dimension)
        $writer.Write($dimension)
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        $writer.Write([UInt16]1)
        $writer.Write([UInt16]32)
        $writer.Write([UInt32]$bytes.Length)
        $writer.Write([UInt32]$offset)
        $offset += $bytes.Length
    }
    foreach ($size in $sizes) { $writer.Write([byte[]]$pngs[$size]) }
}
finally {
    $writer.Dispose()
    $stream.Dispose()
}

Write-Output "Generated RBL Studio icon set (PNG sizes: $($sizes -join ', '); ICO: $icoPath)"
