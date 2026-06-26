# Dashboard SCCA — Windows
# Encerra instancias antigas, libera a porta 5006 e inicia o app.

$ErrorActionPreference = "Stop"

$ProjectDir = "\\wsl$\Ubuntu\home\davi_komori\Projetos_Ecomp\Interface-Coletivo"
$PythonExe  = Join-Path $ProjectDir ".venv-win\Scripts\python.exe"
$PiHost     = "10.0.0.1"
$Env:Vars   = @{
    SCCA_COMMAND_HOST      = $PiHost
    SCCA_JOYSTICK_ID       = "0"
    SCCA_JOYSTICK_AXIS     = "0"
    SCCA_JOYSTICK_INVERT   = "0"
}

Write-Host "=== Dashboard SCCA ===" -ForegroundColor Cyan

if (-not (Test-Path $PythonExe)) {
    Write-Host "ERRO: Python do venv nao encontrado em:" -ForegroundColor Red
    Write-Host "  $PythonExe"
    Write-Host ""
    Write-Host "Crie o venv primeiro:"
    Write-Host "  cd $ProjectDir"
    Write-Host "  python -m venv .venv-win"
    Write-Host "  .\.venv-win\Scripts\python.exe -m pip install PySide6"
    exit 1
}

Write-Host "Encerrando instancias antigas de Python..."
Get-Process python* -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2

$portInUse = Get-NetUDPEndpoint -LocalPort 5006 -ErrorAction SilentlyContinue
if ($portInUse) {
    Write-Host "AVISO: porta 5006 ainda em uso. Tentando liberar..." -ForegroundColor Yellow
    Get-Process python* -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 2
}

Set-Location $ProjectDir
foreach ($key in $Env:Vars.Keys) {
    Set-Item -Path "Env:$key" -Value $Env:Vars[$key]
}

Write-Host "Projeto:  $ProjectDir"
Write-Host "Raspberry: $PiHost`:5005 (comandos) | Windows:5006 (telemetria)"
Write-Host "Iniciando dashboard..." -ForegroundColor Green
Write-Host ""

& $PythonExe main.py

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "Dashboard encerrou com codigo $LASTEXITCODE" -ForegroundColor Red
    exit $LASTEXITCODE
}
