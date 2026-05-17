$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$defaultBuildDir = [System.IO.Path]::GetFullPath((Join-Path $scriptDir '..\build'))
$buildDir = if ($env:BUILD_DIR) { $env:BUILD_DIR } else { $defaultBuildDir }
$testDataDir = if ($env:TEST_DATA_DIR) { $env:TEST_DATA_DIR } else { Join-Path $env:TEMP 'vafs-benchmark-data' }
$outputImage = if ($env:OUTPUT_IMAGE) { $env:OUTPUT_IMAGE } else { Join-Path $testDataDir 'benchmark.vafs' }
$encoding = [System.Text.UTF8Encoding]::new($false)

function Resolve-Mkvafs {
    param([string]$BuildDir)

    $candidates = @(
        (Join-Path $BuildDir 'bin\mkvafs.exe'),
        (Join-Path $BuildDir 'bin\Debug\mkvafs.exe'),
        (Join-Path $BuildDir 'bin\Release\mkvafs.exe'),
        (Join-Path $BuildDir 'bin\RelWithDebInfo\mkvafs.exe'),
        (Join-Path $BuildDir 'bin\MinSizeRel\mkvafs.exe')
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw "mkvafs not found under $BuildDir"
}

function Write-PatternFile {
    param(
        [string]$Path,
        [long]$SizeBytes,
        [string[]]$Blocks
    )

    $parent = Split-Path -Parent $Path
    if ($parent -and -not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }

    $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
    try {
        $written = 0L
        $blockIndex = 0
        while ($written -lt $SizeBytes) {
            $block = $Blocks[$blockIndex % $Blocks.Length]
            $bytes = $encoding.GetBytes($block)
            $remaining = $SizeBytes - $written
            $count = [Math]::Min($bytes.Length, $remaining)
            $stream.Write($bytes, 0, [int]$count)
            $written += $count
            $blockIndex++
        }
    }
    finally {
        $stream.Dispose()
    }
}

function Write-RecordFile {
    param(
        [string]$Path,
        [long]$SizeBytes,
        [string]$Label,
        [int]$Seed
    )

    $parent = Split-Path -Parent $Path
    if ($parent -and -not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }

    $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
    try {
        $written = 0L
        $recordIndex = 0
        while ($written -lt $SizeBytes) {
            $service = Get-ServiceName -Index ($recordIndex + $Seed)
            $mode = if ((($recordIndex + $Seed) % 2) -eq 0) { 'shared' } else { 'private' }
            $checksum = '{0:x8}' -f ((($recordIndex * 1103515245L) + ($Seed * 12345L)) -band 0xffffffffL)
            $lineOne = 'record={0:D8} label={1} service={2} path=/usr/lib/{2}/module/{3:D3}.so mode={4} checksum={5} shard={6} flags=ro,cache,deterministic`n' -f $recordIndex, $Label, $service, (($recordIndex + $Seed) % 257), $mode, $checksum, (($recordIndex + $Seed) % 8)
            $lineTwo = '{{"record":{0},"label":"{1}","service":"{2}","owner":"root","retry":{3},"slice":"{4:D2}","compression":"brieflz"}}`n' -f $recordIndex, $Label, $service, (($recordIndex + $Seed) % 5), (($recordIndex + $Seed) % 64)
            $bytes = $encoding.GetBytes($lineOne + $lineTwo)
            $remaining = $SizeBytes - $written
            $count = [Math]::Min($bytes.Length, $remaining)
            $stream.Write($bytes, 0, [int]$count)
            $written += $count
            $recordIndex++
        }
    }
    finally {
        $stream.Dispose()
    }
}

function Get-ServiceName {
    param([int]$Index)

    return ('svc-{0:D3}' -f (($Index % 23) + 1))
}

function Get-ConfigBlocks {
    param(
        [string]$Label,
        [int]$Index
    )

    $service = Get-ServiceName -Index $Index
    $shard = $Index % 8
    $feature = if (($Index % 3) -eq 0) { 'true' } else { 'false' }

    return @(
@"
# VaFS benchmark payload
name=$Label
service=$service
profile=readonly
arch=all
cache=shared
shard=$shard
feature.alpha=true
feature.beta=$feature
paths=/usr/bin/$service;/usr/lib/$service;/etc/$service
notes=Deterministic benchmark content for reproducible compression and stable timing.
"@,
@"
[unit]
Description=$Label benchmark unit
After=network-online.target local-fs.target
[service]
Type=simple
ExecStart=/usr/bin/$service --config /etc/$service.conf --shard=$shard
Restart=always
Environment=PROFILE=benchmark
Environment=MODE=readonly
Environment=TARGET=$Label
"@,
@"
{
  "name": "$Label",
  "service": "$service",
  "shard": $shard,
  "files": ["manifest.txt", "settings.ini", "payload.bin"],
  "mount": "/opt/$service",
  "compression": "brieflz",
  "deterministic": true
}
"@,
@"
manifest.version=1
manifest.name=$Label
manifest.index=$Index
layers=base,network,storage,services
paths=/etc/$Label.conf,/usr/share/$Label/manifest.txt,/var/lib/$Label/state.db
checksums=00112233,44556677,8899aabb,ccddeeff
"@
    )
}

Write-Host 'VaFS Benchmark Test Data Generator'
Write-Host '==================================='
Write-Host "Build directory: $buildDir"
Write-Host "Test data directory: $testDataDir"
Write-Host "Output image: $outputImage"
Write-Host ''

$mkvafs = Resolve-Mkvafs -BuildDir $buildDir

if (Test-Path -LiteralPath $testDataDir) {
    Remove-Item -LiteralPath $testDataDir -Recurse -Force
}

$sourceDir = Join-Path $testDataDir 'source'
New-Item -ItemType Directory -Path $sourceDir -Force | Out-Null
Push-Location $sourceDir
try {
    Write-Host 'Generating deterministic benchmark data...'

    Write-Host 'Creating small files...'
    New-Item -ItemType Directory -Path (Join-Path $sourceDir 'small_files') -Force | Out-Null
    1..100 | ForEach-Object {
        $index = $_
        $size = (1 + (($index - 1) % 10)) * 1024
        $blocks = Get-ConfigBlocks -Label ("small-file-$index") -Index $index
        Write-PatternFile -Path (Join-Path $sourceDir ("small_files/file_{0}.txt" -f $index)) -SizeBytes $size -Blocks $blocks[0..1]
    }

    $smallBlocks = Get-ConfigBlocks -Label 'small-target' -Index 401
    Write-PatternFile -Path (Join-Path $sourceDir 'small.txt') -SizeBytes (4KB) -Blocks $smallBlocks[0..2]

    Write-Host 'Creating large files...'
    New-Item -ItemType Directory -Path (Join-Path $sourceDir 'large_files') -Force | Out-Null
    1..10 | ForEach-Object {
        $index = $_
        $size = $index * 1MB
        Write-RecordFile -Path (Join-Path $sourceDir ("large_files/file_{0}.bin" -f $index)) -SizeBytes $size -Label ("large-file-$index") -Seed (1000 + $index)
    }

    Write-RecordFile -Path (Join-Path $sourceDir 'large.bin') -SizeBytes (5MB) -Label 'large-target' -Seed 2048

    Write-Host 'Creating directory structure...'
    New-Item -ItemType Directory -Path (Join-Path $sourceDir 'deep/path/to/test/nested/directories/for/traversal/benchmark') -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $sourceDir 'wide_dir') -Force | Out-Null
    1..500 | ForEach-Object {
        $content = "File $_`ncategory=wide`nslot={0:D3}`n" -f ($_ % 32)
        [System.IO.File]::WriteAllText((Join-Path $sourceDir ("wide_dir/file_{0}.txt" -f $_)), $content, $encoding)
    }

    New-Item -ItemType Directory -Path (Join-Path $sourceDir 'lookup_test/subdir1/subdir2/subdir3') -Force | Out-Null
    [System.IO.File]::WriteAllText((Join-Path $sourceDir 'lookup_test/subdir1/subdir2/subdir3/target.txt'), "Target file for lookup benchmark`nmode=stable`n", $encoding)
    [System.IO.File]::WriteAllText((Join-Path $sourceDir 'test.txt'), "Test file for lookup`nmode=stable`n", $encoding)

    Write-Host 'Creating mixed content...'
    New-Item -ItemType Directory -Path (Join-Path $sourceDir 'mixed') -Force | Out-Null
    [System.IO.File]::WriteAllText((Join-Path $sourceDir 'mixed/config.txt'), "Hello, VaFS!`nprofile=deterministic`n", $encoding)
    [System.IO.File]::WriteAllBytes((Join-Path $sourceDir 'mixed/zeros.bin'), (New-Object byte[] (100KB)))
    Write-RecordFile -Path (Join-Path $sourceDir 'mixed/random.bin') -SizeBytes (100KB) -Label 'mixed-random' -Seed 77

    try {
        New-Item -ItemType SymbolicLink -Path (Join-Path $sourceDir 'symlink_test.txt') -Target (Join-Path $sourceDir 'test.txt') -ErrorAction Stop | Out-Null
        Write-Host 'Created symlinks'
    }
    catch {
        Write-Host "Skipped symlink creation: $($_.Exception.Message)"
    }

    Write-Host ''
    Write-Host 'Test data generation complete!'
    $files = Get-ChildItem -LiteralPath $sourceDir -Recurse -File
    $totalBytes = ($files | Measure-Object -Property Length -Sum).Sum
    $sizeMiB = [Math]::Round($totalBytes / 1MB, 2)
    Write-Host "Source directory size: $sizeMiB MiB"
    Write-Host "File count: $($files.Count)"
    Write-Host ''

    Write-Host 'Creating VaFS image...'
    & $mkvafs --out $outputImage .
}
finally {
    Pop-Location
}

if (Test-Path -LiteralPath $outputImage) {
    $image = Get-Item -LiteralPath $outputImage
    Write-Host ''
    Write-Host 'VaFS image created successfully!'
    Write-Host ("Image size: {0} MiB" -f [Math]::Round($image.Length / 1MB, 2))
    Write-Host "Image path: $outputImage"
    Write-Host ''
    Write-Host 'You can now run benchmarks with:'
    Write-Host "  $buildDir\bin\Debug\vafs-bench.exe $outputImage"
}
else {
    throw 'Failed to create VaFS image'
}