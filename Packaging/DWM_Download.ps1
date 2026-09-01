<#
    Dream World Maker - downloader

    The build ships as a spanned installer: DWM_Setup_x.exe plus numbered .bin
    volumes. Every one must sit in the SAME FOLDER before Setup runs, and
    GitHub's release page has no "download all" button -- miss one file and the
    install fails part-way through. This fetches the set.

    Needs nothing installed. PowerShell and curl.exe ship with Windows 10+, and
    the repository is public so no sign-in or token is involved.

    Run it with:
        powershell -ExecutionPolicy Bypass -File DWM_Download.ps1

    The ExecutionPolicy switch is there because Windows blocks downloaded
    scripts by default; it applies to this one run only and changes nothing
    on the machine.
#>

param(
    [string]$Repo    = "TSPFounder/DWM_Dev",
    [string]$Tag     = "v0.1",
    [string]$DestDir = (Join-Path $PSScriptRoot "DWM_Install")
)

$ErrorActionPreference = "Stop"

Write-Host "Dream World Maker - downloading $Tag from $Repo" -ForegroundColor Cyan

# Ask the API what the release actually contains rather than hardcoding file
# names: the number of .bin volumes changes with the size of the build.
$api = "https://api.github.com/repos/$Repo/releases/tags/$Tag"
try {
    $release = Invoke-RestMethod -Uri $api -Headers @{ "User-Agent" = "DWM-Downloader" }
} catch {
    Write-Host "Could not reach GitHub. Check your connection, or that $Tag exists." -ForegroundColor Red
    exit 1
}

$assets = $release.assets
if (-not $assets -or $assets.Count -eq 0) {
    Write-Host "That release has no files attached." -ForegroundColor Red
    exit 1
}

$totalGb = [math]::Round(($assets | Measure-Object -Property size -Sum).Sum / 1GB, 1)
Write-Host "$($assets.Count) files, $totalGb GB total"
Write-Host "Saving to: $DestDir`n"
New-Item -ItemType Directory -Force -Path $DestDir | Out-Null

$index = 0
foreach ($asset in $assets) {
    $index++
    $target = Join-Path $DestDir $asset.name

    # Skip what is already complete, so an interrupted run can simply be
    # restarted instead of fetching gigabytes again.
    if ((Test-Path $target) -and ((Get-Item $target).Length -eq $asset.size)) {
        Write-Host "[$index/$($assets.Count)] $($asset.name) - already downloaded"
        continue
    }

    Write-Host "[$index/$($assets.Count)] $($asset.name) ..."
    & curl.exe -L --fail --retry 3 --retry-delay 5 -o "$target" $asset.browser_download_url
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Download failed. Re-run this script; finished files are kept." -ForegroundColor Red
        exit 1
    }
}

# Size-check every file. A truncated volume fails deep inside Setup with an
# unhelpful message, so it is worth catching here.
$bad = @()
foreach ($asset in $assets) {
    $target = Join-Path $DestDir $asset.name
    if (-not (Test-Path $target) -or ((Get-Item $target).Length -ne $asset.size)) {
        $bad += $asset.name
    }
}
if ($bad.Count -gt 0) {
    Write-Host "`nIncomplete: $($bad -join ', '). Re-run this script." -ForegroundColor Red
    exit 1
}

$setup = Get-ChildItem -Path $DestDir -Filter "*Setup*.exe" | Select-Object -First 1
Write-Host "`nAll files downloaded and verified." -ForegroundColor Green
if ($setup) {
    Write-Host "Run this to install:`n  $($setup.FullName)"
}
