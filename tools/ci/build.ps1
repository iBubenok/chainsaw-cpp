# ==============================================================================
# build.ps1 - Скрипт сборки для Windows (PowerShell)
# ==============================================================================
#
# : CI кроссплатформенный
#
# Использование:
#   .\tools\ci\build.ps1 [OPTIONS]
#
# Опции:
#   -BuildType TYPE        Тип сборки: Debug|Release (default: Release)
#   -WarningsAsErrors      Трактовать предупреждения как ошибки
#   -NoTests               Не собирать тесты
#   -Clean                 Очистить каталог сборки перед конфигурацией
#   -Test                  Запустить тесты после сборки
#   -Jobs N                Количество параллельных задач (default: auto)
#   -Help                  Показать справку
#
# Примеры:
#   .\tools\ci\build.ps1 -BuildType Debug -Test
#   .\tools\ci\build.ps1 -Clean -WarningsAsErrors -Test
#
# ==============================================================================

param(
    [ValidateSet("Debug", "Release")]
    [string]$BuildType = "Release",

    [switch]$WarningsAsErrors,
    [switch]$NoTests,
    [switch]$Clean,
    [switch]$Test,
    [int]$Jobs = 0,
    [switch]$Help
)

$ErrorActionPreference = "Stop"

# Определяем корень проекта
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = (Resolve-Path "$ScriptDir\..\..").Path
$CppDir = Join-Path $ProjectRoot "cpp"
$BuildDir = Join-Path $ProjectRoot "build"

# Определяем количество CPU
if ($Jobs -eq 0) {
    $Jobs = $env:NUMBER_OF_PROCESSORS
    if (-not $Jobs) { $Jobs = 4 }
}

function Write-Info {
    param([string]$Message)
    Write-Host "[INFO] $Message" -ForegroundColor Cyan
}

function Write-Success {
    param([string]$Message)
    Write-Host "[OK] $Message" -ForegroundColor Green
}

function Write-Warning {
    param([string]$Message)
    Write-Host "[WARN] $Message" -ForegroundColor Yellow
}

function Write-Error {
    param([string]$Message)
    Write-Host "[ERROR] $Message" -ForegroundColor Red
}

function Show-Help {
    Get-Content $MyInvocation.ScriptName | Select-Object -First 35 | Select-Object -Skip 5
    exit 0
}

if ($Help) {
    Show-Help
}

# Вывод конфигурации
Write-Info "=== Chainsaw C++ Build (Windows) ==="
Write-Info "Project root: $ProjectRoot"
Write-Info "Build type: $BuildType"
Write-Info "Warnings as errors: $WarningsAsErrors"
Write-Info "Build tests: $(-not $NoTests)"
Write-Info "Parallel jobs: $Jobs"
Write-Host ""

# Проверка наличия CMake
$cmakePath = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmakePath) {
    Write-Error "CMake не найден. Установите CMake версии 3.16 или выше."
    exit 1
}

# Очистка каталога сборки
if ($Clean -and (Test-Path $BuildDir)) {
    Write-Info "Очистка каталога сборки..."
    Remove-Item -Recurse -Force $BuildDir
}

# Формирование аргументов CMake
$cmakeArgs = @(
    "-S", $CppDir,
    "-B", $BuildDir,
    "-DCHAINSAW_BUILD_TESTS=$(-not $NoTests ? 'ON' : 'OFF')",
    "-DCHAINSAW_USE_GTEST=ON"
)

if ($WarningsAsErrors) {
    $cmakeArgs += "-DCHAINSAW_WARNINGS_AS_ERRORS=ON"
}

# Конфигурация
Write-Info "Конфигурация CMake..."
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) {
    Write-Error "Ошибка конфигурации CMake"
    exit 1
}

# Сборка
Write-Info "Сборка проекта..."
& cmake --build $BuildDir --config $BuildType -j $Jobs
if ($LASTEXITCODE -ne 0) {
    Write-Error "Ошибка сборки"
    exit 1
}

Write-Success "Сборка завершена успешно"

# Запуск тестов
if ($Test -and (-not $NoTests)) {
    Write-Info "Запуск тестов..."
    & ctest --test-dir $BuildDir -C $BuildType --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Некоторые тесты не прошли"
        exit 1
    }
    Write-Success "Все тесты пройдены"
}

Write-Success "=== Сборка завершена ==="
