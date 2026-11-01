param(
    [Parameter(Mandatory = $true)]
    [string]$PlanGenerator,

    [Parameter(Mandatory = $true)]
    [string]$ChrononCli,

    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [Parameter(Mandatory = $true)]
    [string]$AssetsRoot,

    [ValidateSet('software', 'vulkan')]
    [string]$Backend = 'software',

    [switch]$Animated,

    [ValidateRange(0.25, 10.0)]
    [double]$BackgroundScale = 1.0,

    [string]$Ffmpeg = 'ffmpeg.exe'
)

$ErrorActionPreference = 'Stop'
$PlanGenerator = (Resolve-Path -LiteralPath $PlanGenerator).Path
$ChrononCli = (Resolve-Path -LiteralPath $ChrononCli).Path
$AssetsRoot = (Resolve-Path -LiteralPath $AssetsRoot).Path
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
Add-Type -AssemblyName System.Drawing

Push-Location $OutputDirectory
try {
    $failedCases = [System.Collections.Generic.List[string]]::new()
    foreach ($planeCount in 2, 3, 4) {
        $motionTag = if ($Animated) { 'animated' } else { 'static' }
        $plan = "optical-planes-$planeCount-$motionTag.plan.json"
        $video = "optical-planes-$planeCount-$motionTag-$Backend.mp4"
        $frame = "optical-planes-$planeCount-$motionTag-$Backend.png"
        $animatedValue = if ($Animated) { '1' } else { '0' }

        & $PlanGenerator $plan 'optical-assets' 320 diagnostic offline `
            $planeCount $animatedValue $BackgroundScale
        if ($LASTEXITCODE -ne 0) {
            throw "plan generation failed for $planeCount planes (exit $LASTEXITCODE)"
        }

        & $ChrononCli render --plan $plan --output $video `
            --assets-root $AssetsRoot --backend $Backend `
            --video-sink ffmpeg --pipe-pixfmt nv12
        if ($LASTEXITCODE -ne 0) {
            throw "render failed for $planeCount planes on $Backend (exit $LASTEXITCODE)"
        }

        & $Ffmpeg -v error -y -i $video -frames:v 1 -update 1 $frame
        if ($LASTEXITCODE -ne 0) {
            throw "frame extraction failed for $video (exit $LASTEXITCODE)"
        }

        $bitmap = [System.Drawing.Bitmap]::FromFile((Join-Path $OutputDirectory $frame))
        try {
            # The authored backdrop covers the full canvas. Sample its four
            # corners, which are outside every plate, rather than the old
            # (230,90) point that fell inside the foreground card.
            $samples = @(
                @{ Name = 'top-left'; Pixel = $bitmap.GetPixel(5, 5) },
                @{ Name = 'left'; Pixel = $bitmap.GetPixel(5, 90) },
                @{ Name = 'right'; Pixel = $bitmap.GetPixel(315, 90) },
                @{ Name = 'bottom-right'; Pixel = $bitmap.GetPixel(315, 175) }
            )
            foreach ($sample in $samples) {
                $p = $sample.Pixel
                $rgb = "RGB=($($p.R),$($p.G),$($p.B))"
                if ($p.R -lt 8 -and $p.G -lt 12 -and $p.B -lt 18) {
                    $failedCases.Add("$planeCount planes ${Backend} $($sample.Name) $rgb")
                }
            }
        }
        finally {
            $bitmap.Dispose()
        }
        $sampleReport = ($samples | ForEach-Object {
            $p = $_.Pixel
            "$($_.Name)=($($p.R),$($p.G),$($p.B))"
        }) -join ', '
        Write-Output "rendered $planeCount planes; backdrop corner samples: $sampleReport; $frame"
    }
    if ($failedCases.Count -gt 0) {
        Write-Error "Backdrop coverage failed: $($failedCases -join '; ')"
        exit 2
    }
}
finally {
    Pop-Location
}
