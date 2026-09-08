param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("setup", "cleanup", "show")]
    [string]$Action
)

$ErrorActionPreference = "Stop"

$root = (Get-Location).Path
$bench = Join-Path $root "bench"


function Assert-Administrator
{
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)

    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw "NTFS snapshot tests require Administrator privileges."
    }
}


function Invoke-DiskPart
{
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Commands
    )

    $scriptPath = Join-Path $env:TEMP ("snapraid-diskpart-" + [Guid]::NewGuid().ToString() + ".txt")

    try {
        [System.IO.File]::WriteAllLines($scriptPath, $Commands, [System.Text.Encoding]::ASCII)

        $output = & diskpart.exe /s $scriptPath 2>&1
        $exitCode = $LASTEXITCODE

        $output | ForEach-Object {
            Write-Host $_
        }

        if ($exitCode -ne 0) {
            throw "diskpart failed with exit code $exitCode."
        }
    }
    finally {
        Remove-Item -LiteralPath $scriptPath -Force -ErrorAction SilentlyContinue
    }
}


function Get-MountedVolumeName
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$MountPath
    )

    $mountArg = $MountPath.TrimEnd([char]92) + "\"
    $output = & mountvol.exe $mountArg /L 2>$null
    $exitCode = $LASTEXITCODE

    if ($exitCode -ne 0) {
        return $null
    }

    foreach ($line in $output) {
        $value = "$line".Trim()

        if ($value -match '^\\\\\?\\Volume\{[^}]+\}\\$') {
            return $value
        }
    }

    return $null
}


function Normalize-VolumeName
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$VolumeName
    )

    return $VolumeName.Trim().TrimEnd([char]92)
}


function Setup-Volume
{
    param(
        [Parameter(Mandatory = $true)]
        [int]$Index
    )

    $mountPath = Join-Path $bench "snap$Index"
    $vhdPath = Join-Path $bench "snap$Index.vhdx"
    $markerPath = Join-Path $bench "snap$Index.volume"

    if (Test-Path -LiteralPath $mountPath) {
        throw "Mount path '$mountPath' already exists."
    }

    if (Test-Path -LiteralPath $vhdPath) {
        throw "VHD '$vhdPath' already exists."
    }

    if (Test-Path -LiteralPath $markerPath) {
        throw "Volume marker '$markerPath' already exists."
    }

    New-Item -ItemType Directory -Path $mountPath | Out-Null

    $mountForDiskPart = $mountPath.TrimEnd([char]92) + "\"

    Invoke-DiskPart @(
        "create vdisk file=`"$vhdPath`" maximum=1024 type=expandable",
        "select vdisk file=`"$vhdPath`"",
        "attach vdisk",
        "convert gpt",
        "create partition primary",
        "format fs=ntfs quick label=SNAPRAID$Index",
        "assign mount=`"$mountForDiskPart`""
    )

    $volumeName = Get-MountedVolumeName $mountPath

    if (-not $volumeName) {
        throw "Unable to resolve the volume name mounted at '$mountPath'."
    }

    Set-Content -LiteralPath $markerPath -Value $volumeName -Encoding ASCII

    Write-Host "Mounted NTFS test volume $volumeName at $mountPath"
}


function Remove-TestShadows
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$VolumeName
    )

    $wanted = Normalize-VolumeName $VolumeName

    $shadows = @(
        Get-WmiObject -Class Win32_ShadowCopy |
        Where-Object {
            $_.VolumeName -and ((Normalize-VolumeName $_.VolumeName) -ieq $wanted)
        }
    )

    foreach ($shadow in $shadows) {
        Write-Host "Deleting VSS snapshot $($shadow.ID) from $VolumeName"
        Remove-WmiObject -InputObject $shadow
    }
}


function Cleanup-Volume
{
    param(
        [Parameter(Mandatory = $true)]
        [int]$Index
    )

    $mountPath = Join-Path $bench "snap$Index"
    $vhdPath = Join-Path $bench "snap$Index.vhdx"
    $markerPath = Join-Path $bench "snap$Index.volume"

    if (Test-Path -LiteralPath $markerPath) {
        $volumeName = (Get-Content -LiteralPath $markerPath -Raw).Trim()

        if ($volumeName) {
            Remove-TestShadows $volumeName
        }
    }

    if (Test-Path -LiteralPath $vhdPath) {
        Invoke-DiskPart @(
            "select vdisk file=`"$vhdPath`"",
            "detach vdisk noerr"
        )

        Remove-Item -LiteralPath $vhdPath -Force
    }

    if (Test-Path -LiteralPath $mountPath) {
        Remove-Item -LiteralPath $mountPath -Recurse -Force
    }

    if (Test-Path -LiteralPath $markerPath) {
        Remove-Item -LiteralPath $markerPath -Force
    }
}


function Setup
{
    if (-not (Test-Path -LiteralPath $bench)) {
        New-Item -ItemType Directory -Path $bench | Out-Null
    }

    Setup-Volume 1
    Setup-Volume 2
    Setup-Volume 3
}


function Cleanup
{
    $failed = $false

    foreach ($index in 1..3) {
        try {
            Cleanup-Volume $index
        }
        catch {
            Write-Warning "Cleanup of NTFS test volume $index failed: $($_.Exception.Message)"
            $failed = $true
        }
    }

    if ($failed) {
        throw "Cleanup of one or more NTFS test volumes failed."
    }

    if (Test-Path -LiteralPath $bench) {
        Remove-Item -LiteralPath $bench -Recurse -Force
    }
}


function Show
{
    foreach ($index in 1..3) {
        $mountPath = Join-Path $bench "snap$Index"
        $snapshotPath = Join-Path $mountPath ".snapraid"
        $markerPath = Join-Path $bench "snap$Index.volume"

        Write-Host "${snapshotPath}:"

        if (Test-Path -LiteralPath $snapshotPath) {
            Get-ChildItem -LiteralPath $snapshotPath -Force |
            Sort-Object Name |
            ForEach-Object {
                Write-Host "  $($_.Name)"
            }
        }
        else {
            Write-Host "  (missing)"
        }

        if (Test-Path -LiteralPath $markerPath) {
            $volumeName = (Get-Content -LiteralPath $markerPath -Raw).Trim()
            $wanted = Normalize-VolumeName $volumeName

            Get-WmiObject -Class Win32_ShadowCopy |
            Where-Object {
                $_.VolumeName -and ((Normalize-VolumeName $_.VolumeName) -ieq $wanted)
            } |
            ForEach-Object {
                Write-Host "  VSS $($_.ID)"
            }
        }
    }
}


try {
    switch ($Action) {
        "setup" {
            Assert-Administrator
            Setup
        }

        "cleanup" {
            Assert-Administrator
            Cleanup
        }

        "show" {
            Show
        }
    }
}
catch {
    [Console]::Error.WriteLine("error: " + $_.Exception.Message)
    exit 1
}

exit 0
