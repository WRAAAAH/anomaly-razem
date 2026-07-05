@echo off
REM Fetches ENet 1.3.17 source into src\xrCoopNet\enet\
REM Requires git. Run from the repo root or this directory.

set ENET_TAG=v1.3.17
set ENET_REPO=https://github.com/lsalzman/enet.git
set OUT_DIR=%~dp0enet

if exist "%OUT_DIR%\.git" (
    echo ENet already present at %OUT_DIR%. Pulling latest...
    git -C "%OUT_DIR%" fetch --tags
    git -C "%OUT_DIR%" checkout %ENET_TAG%
) else (
    echo Cloning ENet %ENET_TAG% into %OUT_DIR%...
    git clone --depth 1 --branch %ENET_TAG% %ENET_REPO% "%OUT_DIR%"
)

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ENet ready. Now build xrCoopNet in Visual Studio.
) else (
    echo ERROR: git clone failed. Install git and retry.
    exit /b 1
)
