@echo off
setlocal

rem ============================================================
rem gen_font_atlas.bat
rem
rem Usage: gen_font_atlas.bat <font-file> [output-name]
rem
rem  Arg1: font file path (.ttf / .otf)  [required]
rem  Arg2: output folder name            [optional, defaults to font filename]
rem
rem Output: ..\..\DirectX\Game\Assets\Font\<output-name>\atlas.json / atlas.png
rem ============================================================

if "%~1"=="" (
    echo.
    echo [Usage] %~nx0 ^<font-file^> [output-name]
    echo.
    echo  Arg1 : font file path  (required)
    echo  Arg2 : output folder   (optional - defaults to font filename)
    echo.
    echo [Example]
    echo  %~nx0 "C:\Git\Test\DirectX\Game\Assets\Font\MyFont.otf"
    echo  %~nx0 "C:\Git\Test\DirectX\Game\Assets\Font\MyFont.otf" MyFont
    echo.
    exit /b 1
)

set FONT_FILE=%~1

if not exist "%FONT_FILE%" (
    echo [ERROR] Font file not found: %FONT_FILE%
    exit /b 1
)

if "%~2"=="" (
    set OUTPUT_NAME=%~n1
) else (
    set OUTPUT_NAME=%~2
)

set TOOLS_DIR=%~dp0
set ASSETS_FONT_DIR=%TOOLS_DIR%..\..\DirectX\Game\Assets\Font
set CHARSET_FILE=%ASSETS_FONT_DIR%\charset.txt
set OUTPUT_DIR=%ASSETS_FONT_DIR%\%OUTPUT_NAME%

if not exist "%CHARSET_FILE%" (
    echo [ERROR] charset.txt not found: %CHARSET_FILE%
    exit /b 1
)

if not exist "%OUTPUT_DIR%" (
    echo Creating folder: %OUTPUT_DIR%
    mkdir "%OUTPUT_DIR%"
)

echo.
echo [MSDF Atlas Generate]
echo   Font    : %FONT_FILE%
echo   Output  : %OUTPUT_DIR%
echo   Charset : %CHARSET_FILE%
echo   Size    : 32px  pxRange: 4  Atlas: 4096x4096
echo.

"%TOOLS_DIR%msdf-atlas-gen.exe" ^
  -font "%FONT_FILE%" ^
  -type msdf ^
  -format png ^
  -imageout "%OUTPUT_DIR%\atlas.png" ^
  -json     "%OUTPUT_DIR%\atlas.json" ^
  -charset  "%CHARSET_FILE%" ^
  -size 32 ^
  -pxrange 4 ^
  -dimensions 4096 4096

if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Atlas generation failed (exit code: %errorlevel%)
    exit /b %errorlevel%
)

echo.
echo [Done] atlas.json / atlas.png saved to:
echo   %OUTPUT_DIR%
echo.
endlocal