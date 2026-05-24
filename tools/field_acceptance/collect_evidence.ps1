param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9_.-]+$')]
    [string]$RunId,

    [string]$OutputRoot = "out\field-acceptance",
    [string]$ConfigPath = "config\default.toml",
    [string]$RecipePath = "config\motion_recipes.toml",
    [string]$LogDir = "logs",
    [string]$TestOutputDir = "test-output",
    [string]$ChecklistPath = "docs\hardware_acceptance_checklist.md"
)

$ErrorActionPreference = "Stop"

function Copy-IfExists {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Source,

        [Parameter(Mandatory = $true)]
        [string]$Destination
    )

    if (Test-Path -LiteralPath $Source) {
        Copy-Item -LiteralPath $Source -Destination $Destination -Recurse -Force
        return $true
    }
    return $false
}

$workspace = (Resolve-Path -LiteralPath ".").Path
$outputRootPath = Join-Path $workspace $OutputRoot
$runPath = Join-Path $outputRootPath $RunId

New-Item -ItemType Directory -Force -Path $runPath | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $runPath "config") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $runPath "logs") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $runPath "test-output") | Out-Null

$copied = New-Object System.Collections.Generic.List[string]
$missing = New-Object System.Collections.Generic.List[string]

if (Copy-IfExists -Source $ConfigPath -Destination (Join-Path $runPath "config")) {
    $copied.Add($ConfigPath)
} else {
    $missing.Add($ConfigPath)
}

if (Copy-IfExists -Source $RecipePath -Destination (Join-Path $runPath "config")) {
    $copied.Add($RecipePath)
} else {
    $missing.Add($RecipePath)
}

if (Copy-IfExists -Source $LogDir -Destination (Join-Path $runPath "logs")) {
    $copied.Add($LogDir)
} else {
    $missing.Add($LogDir)
}

if (Copy-IfExists -Source $TestOutputDir -Destination (Join-Path $runPath "test-output")) {
    $copied.Add($TestOutputDir)
} else {
    $missing.Add($TestOutputDir)
}

if (Copy-IfExists -Source $ChecklistPath -Destination $runPath) {
    $copied.Add($ChecklistPath)
} else {
    $missing.Add($ChecklistPath)
}

$commit = "unknown"
try {
    $commit = (git rev-parse --short HEAD).Trim()
} catch {
    $missing.Add("git commit")
}

$summaryPath = Join-Path $runPath "summary.txt"
$timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss zzz"
$summary = @(
    "run_id=$RunId",
    "timestamp=$timestamp",
    "workspace=$workspace",
    "commit=$commit",
    "copied:",
    ($copied | ForEach-Object { "  $_" }),
    "missing:",
    ($missing | ForEach-Object { "  $_" }),
    "",
    "note=This helper only copies local evidence files. It does not connect to PLC, camera, DUCO, Modbus, or issue motion/IO commands."
)

Set-Content -LiteralPath $summaryPath -Value $summary -Encoding UTF8
Write-Host "Evidence bundle created: $runPath"
Write-Host "Summary: $summaryPath"
