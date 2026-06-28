@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul
rem ============================================================
rem  Vulkan SDK セットアップ (aqEngine Vulkan バックエンド用)
rem  - SDK が無ければインストーラを取得して導入する
rem  - 既に導入済みなら検証のみ
rem  使い方: このバッチをダブルクリック (管理者昇格に同意)
rem ============================================================

set "VK_VERSION=1.4.350.0"
set "VK_INSTALLER=vulkansdk-windows-X64-%VK_VERSION%.exe"
set "VK_URL=https://sdk.lunarg.com/sdk/download/%VK_VERSION%/windows/%VK_INSTALLER%"
set "SCRIPT_DIR=%~dp0"

echo ============================================================
echo  Vulkan SDK セットアップ  (version %VK_VERSION%)
echo ============================================================
echo.

rem ── 既にインストール済みか ──
if defined VULKAN_SDK (
    if exist "%VULKAN_SDK%\Bin\dxcompiler.dll" (
        echo [OK] 既にインストール済み: %VULKAN_SDK%
        goto :verify
    )
)

rem ── インストーラがフォルダに無ければダウンロード ──
if not exist "%SCRIPT_DIR%%VK_INSTALLER%" (
    echo インストーラが見つかりません。ダウンロードします...
    echo   %VK_URL%
    where curl >nul 2>nul
    if !errorlevel! == 0 (
        curl -L --fail -o "%SCRIPT_DIR%%VK_INSTALLER%" "%VK_URL%"
    ) else (
        powershell -NoProfile -Command "Invoke-WebRequest -Uri '%VK_URL%' -OutFile '%SCRIPT_DIR%%VK_INSTALLER%'"
    )
    if not exist "%SCRIPT_DIR%%VK_INSTALLER%" (
        echo [ERROR] ダウンロードに失敗しました。
        echo         手動で次の URL から取得し、このフォルダに置いてください:
        echo         %VK_URL%
        pause
        exit /b 1
    )
    echo [OK] ダウンロード完了。
) else (
    echo [OK] インストーラ発見: %VK_INSTALLER%
)

echo.
echo インストーラを実行します (ライセンス自動同意・管理者昇格あり)...
start /wait "" "%SCRIPT_DIR%%VK_INSTALLER%" --accept-licenses --default-answer --confirm-command install
if !errorlevel! neq 0 (
    echo [WARN] サイレント導入に失敗。インストーラを通常起動します。画面に従って進めてください。
    start /wait "" "%SCRIPT_DIR%%VK_INSTALLER%"
)

:verify
echo.
echo ============================================================
echo  検証
echo ============================================================
rem VULKAN_SDK は新規シェルで反映される。未設定なら既定パスで検証する。
if not defined VULKAN_SDK set "VULKAN_SDK=C:\VulkanSDK\%VK_VERSION%"
set "OK=1"
call :check "%VULKAN_SDK%\Include\vulkan\vulkan.h"  "Vulkan ヘッダ"
call :check "%VULKAN_SDK%\Lib\vulkan-1.lib"         "vulkan-1.lib (リンク用)"
call :check "%VULKAN_SDK%\Bin\dxcompiler.dll"       "dxcompiler.dll (実行時 SPIR-V)"
call :check "%VULKAN_SDK%\Bin\VkLayer_khronos_validation.dll" "検証レイヤ (任意)"
echo.
if "%OK%"=="1" (
    echo [完了] Vulkan SDK は利用可能です。
    echo   1) 新しいコマンドプロンプト / Visual Studio を開き直す ^(VULKAN_SDK 反映^)
    echo   2) DirectX\aqEngine\aq.h を  #define ENGINE_GRAPHICS_VULKAN  に変更
    echo   3) リビルド ^(dxcompiler.dll は Game の post-build が自動コピー^)
) else (
    echo [未完] 不足項目があります。インストーラの完了を確認するか、再実行してください。
)
echo.
pause
endlocal
exit /b 0

:check
if exist "%~1" (echo [OK] %~2) else (echo [NG] %~2  ^(%~1^) & set "OK=0")
exit /b 0
