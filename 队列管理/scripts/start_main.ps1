# 工业队列系统开机启动脚本
#
# 作用：
# 1. 统一从项目根目录启动 src/main.py
# 2. 自动写入 logs/autostart_runner.log（脚本层）和 logs/autostart_main.log（主程序输出）
# 3. 防止重复启动（已存在 main.py 进程时直接退出）
#
# 说明：
# - 该脚本适合由“任务计划程序”在开机时调用。
# - 若你调整了项目目录，请同步修改任务计划里的 -File 路径。

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
# 在 StrictMode 下，catch 中访问未定义变量会二次报错；
# 先初始化，保证异常路径也能稳定记录。
$script:RunnerLogFile = $null

function Write-RunnerLog {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Message
    )
    $ts = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    Add-Content -Path $script:RunnerLogFile -Value "[$ts] $Message"
}

try {
    # 以脚本目录为基准，向上一级定位项目根目录。
    $scriptDir = Split-Path -Path $PSCommandPath -Parent
    $projectRoot = Split-Path -Path $scriptDir -Parent

    $logsDir = Join-Path $projectRoot "logs"
    if (-not (Test-Path $logsDir)) {
        New-Item -ItemType Directory -Path $logsDir -Force | Out-Null
    }

    $script:RunnerLogFile = Join-Path $logsDir "autostart_runner.log"
    $mainLogFile = Join-Path $logsDir "autostart_main.log"

    $mainPy = Join-Path $projectRoot "src\main.py"
    $venvPython = Join-Path $projectRoot ".venv\Scripts\python.exe"

    if (Test-Path $venvPython) {
        $pythonExe = $venvPython
    } else {
        $pythonExe = "python"
        Write-RunnerLog "WARNING: .venv python not found, fallback to system python."
    }

    if (-not (Test-Path $mainPy)) {
        Write-RunnerLog "ERROR: main.py not found at $mainPy"
        exit 2
    }

    # 防重复：如果已存在运行中的 src/main.py，则不再启动第二个实例。
    $running = Get-CimInstance Win32_Process -Filter "Name='python.exe'" |
        Where-Object {
            $_.CommandLine -and
            $_.CommandLine -like "*src\\main.py*" -and
            $_.CommandLine -like "*$projectRoot*"
        }

    if ($running) {
        Write-RunnerLog "INFO: main.py already running, skip start."
        exit 0
    }

    Write-RunnerLog "INFO: starting main process: $pythonExe $mainPy"
    Write-RunnerLog "INFO: main output redirected to $mainLogFile"

    Push-Location $projectRoot
    try {
        $env:PYTHONDONTWRITEBYTECODE = "1"
        # 前台运行：由任务计划程序托管进程生命周期。
        # 主程序 stdout/stderr 统一写入 autostart_main.log，便于现场排障。
        & $pythonExe $mainPy *>> $mainLogFile
        $exitCode = $LASTEXITCODE
        Write-RunnerLog "INFO: main process exited with code $exitCode"
        exit $exitCode
    } finally {
        Pop-Location
    }
} catch {
    $msg = $_.Exception.Message
    if ($null -ne $script:RunnerLogFile -and $script:RunnerLogFile -ne "") {
        Write-RunnerLog "ERROR: startup script failed: $msg"
    } else {
        Write-Error $msg
    }
    exit 1
}
