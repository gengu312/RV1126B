$ErrorActionPreference = "Stop"

$program = Join-Path $PSScriptRoot "pressure_audio_manager.py"

if (Get-Command py -ErrorAction SilentlyContinue) {
    & py -3 $program
    exit $LASTEXITCODE
}

if (Get-Command python -ErrorAction SilentlyContinue) {
    & python $program
    exit $LASTEXITCODE
}

throw "Python 3 was not found. Install Python 3 with Tk support and try again."
