#!/usr/bin/env pwsh
# pre-commit-test.ps1 — Run Logger native unit tests before committing.
#
# Usage:
#   .\scripts\pre-commit-test.ps1
#
# To wire into git pre-commit hook, create .git/hooks/pre-commit:
#   #!/bin/sh
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/pre-commit-test.ps1
#   exit $?

Set-StrictMode -Off
$ErrorActionPreference = "Stop"

# Resolve project root (one level above this script's directory)
$ProjectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $ProjectRoot

try {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "  CoinTrace Logger — native unit tests  " -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host ""

    & pio test -e native-test

    if ($LASTEXITCODE -ne 0) {
        Write-Host ""
        Write-Host "TESTS FAILED — commit blocked." -ForegroundColor Red
        Write-Host "Fix the failing tests then retry." -ForegroundColor Red
        exit 1
    }

    Write-Host ""
    Write-Host "All tests passed." -ForegroundColor Green
    exit 0
}
finally {
    Pop-Location
}
