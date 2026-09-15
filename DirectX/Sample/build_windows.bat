@echo off
chcp 65001 > nul
setlocal enabledelayedexpansion

rem ==========================================================================
rem  Sample を Windows でビルドする
rem
rem  エンジン開発用の DirectX.sln はこのサンプルを持たない。ここでは CMake に
rem  Visual Studio のプロジェクトを生成させ、Sample だけをビルドする。
rem  生成物は build\<プリセット名>\ に出るので、リポジトリの中身は汚れない。
rem
rem  使い方:
rem    build_windows.bat              Debug でビルドする
rem    build_windows.bat Release      Release でビルドする
rem    build_windows.bat run          ビルドしてそのまま起動する
rem ==========================================================================

set "ROOT=%~dp0.."
set "CONFIG=Debug"
set "RUN=0"

for %%A in (%*) do (
	if /I "%%~A"=="Debug"   set "CONFIG=Debug"
	if /I "%%~A"=="Release" set "CONFIG=Release"
	if /I "%%~A"=="run"     set "RUN=1"
)

rem --- cmake を探す(PATH → Visual Studio 同梱版) --------------------------
set "CMAKE="
where cmake >nul 2>nul && set "CMAKE=cmake"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSMAJOR="
if exist "%VSWHERE%" (
	for /f "usebackq tokens=1 delims=." %%V in (`"%VSWHERE%" -latest -products * -property installationVersion`) do set "VSMAJOR=%%V"
	if not defined CMAKE (
		for /f "usebackq delims=" %%P in (`"%VSWHERE%" -latest -products * -property installationPath`) do (
			if exist "%%P\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
				set "CMAKE=%%P\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
			)
		)
	)
)

if not defined CMAKE (
	echo [error] cmake が見つかりません。
	echo         Visual Studio の「C++ によるデスクトップ開発」を入れるか、
	echo         winget install Kitware.CMake で単体導入してください。
	exit /b 1
)

rem --- 生成に使うプリセットを決める(VS 2026 = v18 / VS 2022 = v17) --------
set "PRESET=windows-vs2026"
if "%VSMAJOR%"=="17" set "PRESET=windows-vs2022"

echo [build] cmake   : %CMAKE%
echo [build] preset  : %PRESET%
echo [build] config  : %CONFIG%

pushd "%ROOT%"

"%CMAKE%" --preset %PRESET%
if errorlevel 1 (
	popd
	echo [error] プロジェクトの生成に失敗しました。
	exit /b 1
)

"%CMAKE%" --build "build\%PRESET%" --config %CONFIG% --target Sample
if errorlevel 1 (
	popd
	echo [error] ビルドに失敗しました。
	exit /b 1
)

popd

set "EXE=%ROOT%\build\%PRESET%\bin\%CONFIG%\Sample.exe"
echo.
echo [build] 完了: %EXE%

if "%RUN%"=="1" (
	pushd "%~dp0"
	"%EXE%"
	popd
) else (
	echo [build] 起動するには build_windows.bat run
)

endlocal
exit /b 0
