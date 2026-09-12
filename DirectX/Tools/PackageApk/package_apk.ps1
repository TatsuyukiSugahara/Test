<#
.SYNOPSIS
	libGame.so から Android の APK を作る(Gradle 不使用)。

.DESCRIPTION
	Android SDK の build-tools(aapt2 / zipalign / apksigner)と JDK の keytool を
	直接呼んで APK を組み立てる。Gradle を使わないのは、この開発機に Gradle が
	入っておらず、初回実行でネットワークから配布物を取りに行かせたくないため。
	このアプリは Java/Kotlin のコードを持たない(NativeActivity を使う)ので
	classes.dex が不要で、Gradle が担う仕事はほぼ「zip を作って署名する」だけになる。

	処理の流れ:
	  1. デバッグ用 keystore が無ければ keytool で作る
	  2. aapt2 link で AndroidManifest.xml から素の APK を作る
	  3. lib/<abi>/libGame.so(と任意で Vulkan 検証レイヤ)を APK へ足す
	  4. zipalign -p 4 で整列する
	  5. apksigner で署名し、apksigner verify で検証する

.EXAMPLE
	powershell -ExecutionPolicy Bypass -File Tools\PackageApk\package_apk.ps1

.EXAMPLE
	powershell -ExecutionPolicy Bypass -File Tools\PackageApk\package_apk.ps1 -Config Debug -Abi arm64-v8a
#>
[CmdletBinding()]
param(
	# 同梱するネイティブライブラリ。既定は -Config / -Abi から組み立てる
	[string]$SoPath,

	# 出力する APK。既定は build\android-<abi>\AquaDash-<config>.apk
	[string]$OutApk,

	[ValidateSet('Debug', 'Release')]
	[string]$Config = 'Debug',

	# APK の assets/ へ入れるディレクトリ。P1 では未使用(.spv とアセットは P2 で使う)
	[string]$AssetsDir,

	[string]$Abi = 'arm64-v8a',

	# Vulkan 検証レイヤ(libVkLayer_khronos_validation.so)を同梱する
	[switch]$IncludeValidationLayer,

	# 検証レイヤの .so を明示指定する(自動探索で見つからない場合)
	[string]$ValidationLayerPath,

	[string]$ManifestPath,

	# 署名に使う keystore。無ければデバッグ用を自動生成する
	[string]$Keystore,

	# ツールの場所。この開発機の既定値を入れてある
	[string]$AndroidSdkRoot = 'C:\Program Files (x86)\Android\android-sdk',
	[string]$BuildToolsVersion = '36.0.0',
	[string]$AndroidPlatform = 'android-34',
	[string]$JdkRoot = 'C:\Program Files (x86)\Android\openjdk\jdk-17.0.14'
)

$ErrorActionPreference = 'Stop'


# ---------------------------------------------------------------------------
# 共通ユーティリティ
# ---------------------------------------------------------------------------

function Write-Step([string]$message)
{
	Write-Host ""
	Write-Host "==== $message" -ForegroundColor Cyan
}


function Write-Note([string]$message)
{
	Write-Host "     $message" -ForegroundColor DarkGray
}


# 失敗は必ずここを通す。何が無かった / どのコマンドが落ちたかを明示して止める
function Stop-WithError([string]$message)
{
	Write-Host ""
	Write-Host "[package_apk] エラー: $message" -ForegroundColor Red
	exit 1
}


# 外部コマンドは常に配列で引数を渡す。この環境はパスに空白が多く、
# 1 本の文字列に連結すると引用の取り扱いで壊れるため
function Invoke-Tool([string]$exePath, [string[]]$toolArgs, [string]$label)
{
	Write-Note $label
	& $exePath @toolArgs
	if ($LASTEXITCODE -ne 0) {
		Stop-WithError "$label が失敗しました(終了コード $LASTEXITCODE)"
	}
}


function Assert-FileExists([string]$path, [string]$what)
{
	if ([string]::IsNullOrWhiteSpace($path)) {
		Stop-WithError "$what のパスが空です"
	}
	if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
		Stop-WithError "$what が見つかりません: $path"
	}
}


# APK は zip なので、aapt2 の出力へ直接エントリを足す。
# 圧縮して入れるのは、マニフェストが extractNativeLibs="true" を宣言しており
# インストール時に展開される前提のため(効くのは APK のサイズだけ)
function Add-EntriesToZip([string]$zipPath, [object[]]$entries)
{
	$zip = [System.IO.Compression.ZipFile]::Open($zipPath, [System.IO.Compression.ZipArchiveMode]::Update)
	try {
		foreach ($entry in $entries) {
			$existing = $zip.GetEntry($entry.Name)
			if ($null -ne $existing) {
				$existing.Delete()
			}
			$created = $zip.CreateEntry($entry.Name, [System.IO.Compression.CompressionLevel]::Optimal)
			$writer = $created.Open()
			try {
				$reader = [System.IO.File]::OpenRead($entry.Path)
				try {
					$reader.CopyTo($writer)
				} finally {
					$reader.Dispose()
				}
			} finally {
				$writer.Dispose()
			}
			$sizeMB = (Get-Item -LiteralPath $entry.Path).Length / 1MB
			Write-Note ("追加: {0}  ({1:N1} MB)" -f $entry.Name, $sizeMB)
		}
	} finally {
		$zip.Dispose()
	}
}


# ---------------------------------------------------------------------------
# パスの解決
# ---------------------------------------------------------------------------

Write-Step "パスとツールの確認"

# このスクリプトは DirectX\Tools\PackageApk\ にあるので、2 段上が DirectX ルート
$directXRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent

# ABI とビルドディレクトリ名の対応(CMakePresets の android-* プリセットに合わせる)
$presetDirByAbi = @{
	'arm64-v8a'   = 'android-arm64'
	'x86_64'      = 'android-x86_64'
	'armeabi-v7a' = 'android-arm'
	'x86'         = 'android-x86'
}
if (-not $presetDirByAbi.ContainsKey($Abi)) {
	Stop-WithError "未知の ABI です: $Abi(対応: $($presetDirByAbi.Keys -join ', '))"
}
$buildDir = Join-Path $directXRoot ('build\' + $presetDirByAbi[$Abi])

if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
	$ManifestPath = Join-Path $directXRoot 'Android\AndroidManifest.xml'
}
if ([string]::IsNullOrWhiteSpace($SoPath)) {
	$SoPath = Join-Path $buildDir "lib\$Config\libGame.so"
}
if ([string]::IsNullOrWhiteSpace($OutApk)) {
	$OutApk = Join-Path $buildDir ('AquaDash-' + $Config.ToLower() + '.apk')
}
if ([string]::IsNullOrWhiteSpace($Keystore)) {
	$Keystore = Join-Path $env:USERPROFILE '.android\debug.keystore'
}

Assert-FileExists $ManifestPath 'AndroidManifest.xml'
Assert-FileExists $SoPath 'libGame.so(先に cmake で Android ビルドを通すこと)'

# ツール群
$buildTools = Join-Path $AndroidSdkRoot "build-tools\$BuildToolsVersion"
$aapt2      = Join-Path $buildTools 'aapt2.exe'
$zipalign   = Join-Path $buildTools 'zipalign.exe'
$apksigner  = Join-Path $buildTools 'apksigner.bat'
$androidJar = Join-Path $AndroidSdkRoot "platforms\$AndroidPlatform\android.jar"
$keytool    = Join-Path $JdkRoot 'bin\keytool.exe'

Assert-FileExists $aapt2 'aapt2.exe'
Assert-FileExists $zipalign 'zipalign.exe'
Assert-FileExists $apksigner 'apksigner.bat'
Assert-FileExists $androidJar "android.jar($AndroidPlatform)"
Assert-FileExists $keytool 'keytool.exe'

# apksigner.bat は JAVA_HOME か PATH の java を使う。呼ぶ前にこのプロセスへ入れておく
$env:JAVA_HOME = $JdkRoot
$env:PATH = (Join-Path $JdkRoot 'bin') + ';' + $env:PATH

Write-Note "DirectX ルート : $directXRoot"
Write-Note "マニフェスト   : $ManifestPath"
Write-Note "ネイティブ     : $SoPath"
Write-Note "出力 APK       : $OutApk"
Write-Note "構成 / ABI     : $Config / $Abi"


# ---------------------------------------------------------------------------
# 1. デバッグ用 keystore
# ---------------------------------------------------------------------------

Write-Step "署名鍵の確認"

if (Test-Path -LiteralPath $Keystore -PathType Leaf) {
	Write-Note "既存の keystore を使う: $Keystore"
} else {
	# Android SDK が作るものと同じ慣例の内容(パスワード android / 別名 androiddebugkey)。
	# Android Studio が後から同じ場所に keystore を作っても署名が食い違わないよう、
	# あえて慣例どおりのパス・別名・パスワードに合わせている
	Write-Note "keystore が無いので作る: $Keystore"
	$keystoreDir = Split-Path $Keystore -Parent
	if (-not (Test-Path -LiteralPath $keystoreDir)) {
		New-Item -ItemType Directory -Path $keystoreDir -Force | Out-Null
	}
	Invoke-Tool $keytool @(
		'-genkeypair',
		'-keystore', $Keystore,
		'-storepass', 'android',
		'-keypass', 'android',
		'-alias', 'androiddebugkey',
		'-keyalg', 'RSA',
		'-keysize', '2048',
		'-validity', '10950',
		'-dname', 'CN=Android Debug, O=Android, C=US'
	) 'keytool -genkeypair'
}

if ($Config -eq 'Release') {
	Write-Host "     注意: Release 構成をデバッグ鍵で署名している。配布用の鍵は別途用意すること" -ForegroundColor Yellow
}


# ---------------------------------------------------------------------------
# 2. aapt2 link でベース APK
# ---------------------------------------------------------------------------

Write-Step "aapt2 link(マニフェストから APK の骨組みを作る)"

# 中間物は出力先の隣に置く。失敗時に中身を見たいので、成功してから消す
$stageDir = "$OutApk.work"
if (Test-Path -LiteralPath $stageDir) {
	Remove-Item -LiteralPath $stageDir -Recurse -Force
}
New-Item -ItemType Directory -Path $stageDir -Force | Out-Null

$baseApk    = Join-Path $stageDir 'base-unsigned.apk'
$alignedApk = Join-Path $stageDir 'base-aligned.apk'

$linkArgs = @('link', '-o', $baseApk, '-I', $androidJar, '--manifest', $ManifestPath)

# debuggable はマニフェストに書かず、Debug 構成のときだけ aapt2 に入れさせる。
# マニフェストを構成ごとに複製しないための措置
if ($Config -eq 'Debug') {
	$linkArgs += '--debug-mode'
}

if (-not [string]::IsNullOrWhiteSpace($AssetsDir)) {
	if (-not (Test-Path -LiteralPath $AssetsDir -PathType Container)) {
		Stop-WithError "-AssetsDir が見つかりません: $AssetsDir"
	}
	$assetsFull = (Resolve-Path -LiteralPath $AssetsDir).Path
	# 中間物(ステージングディレクトリ)を assets の下に作ると、aapt2 が
	# それ自身をアセットとして APK へ取り込んでしまう
	if ($stageDir.StartsWith($assetsFull, [StringComparison]::OrdinalIgnoreCase)) {
		Stop-WithError "-OutApk を -AssetsDir の中に置けません(中間物がアセットに混ざる): $OutApk"
	}
	$linkArgs += @('-A', $assetsFull)
	Write-Note "assets: $AssetsDir"
}

Invoke-Tool $aapt2 $linkArgs 'aapt2 link'


# ---------------------------------------------------------------------------
# 3. ネイティブライブラリを APK へ足す
# ---------------------------------------------------------------------------

Write-Step "lib/$Abi へネイティブライブラリを追加"

Add-Type -AssemblyName 'System.IO.Compression' | Out-Null
Add-Type -AssemblyName 'System.IO.Compression.FileSystem' | Out-Null

$zipEntries = @()
$zipEntries += [PSCustomObject]@{ Name = "lib/$Abi/libGame.so"; Path = (Resolve-Path -LiteralPath $SoPath).Path }

# Vulkan 検証レイヤ。NDK r26 以降は同梱されなくなったので、あれば拾う程度に留める
if ($IncludeValidationLayer) {
	$layerArchByAbi = @{
		'arm64-v8a'   = 'aarch64'
		'armeabi-v7a' = 'arm'
		'x86_64'      = 'x86_64'
		'x86'         = 'i386'
	}
	$layerArch = $layerArchByAbi[$Abi]

	$layerPath = $null
	if (-not [string]::IsNullOrWhiteSpace($ValidationLayerPath)) {
		Assert-FileExists $ValidationLayerPath '-ValidationLayerPath で指定された検証レイヤ'
		$layerPath = $ValidationLayerPath
	} else {
		# 探す場所:
		#   NDK のツールチェーン配下 / NDK の sources\third_party\vulkan(r25 以前の配置)
		#   / Vulkan SDK(Windows 版に Android 用 .so は入らないが念のため)
		$searchRoots = @()
		foreach ($envName in @('ANDROID_NDK_HOME', 'ANDROID_NDK_ROOT', 'NDK_ROOT', 'VULKAN_SDK')) {
			$value = [Environment]::GetEnvironmentVariable($envName)
			if (-not [string]::IsNullOrWhiteSpace($value)) {
				if (Test-Path -LiteralPath $value -PathType Container) {
					$searchRoots += $value
				}
			}
		}
		foreach ($root in $searchRoots) {
			$found = Get-ChildItem -Path $root -Recurse -Filter 'libVkLayer_khronos_validation.so' -ErrorAction SilentlyContinue |
				Where-Object { $_.FullName -match [regex]::Escape($layerArch) } |
				Select-Object -First 1
			if ($null -ne $found) {
				$layerPath = $found.FullName
				break
			}
		}
	}

	if ($null -eq $layerPath) {
		# 見つからないのは想定内(NDK r27c には入っていない)。
		# ここで止めると APK が全く作れなくなるので警告だけにする
		Write-Host "     警告: libVkLayer_khronos_validation.so ($layerArch) が見つからないので同梱しない" -ForegroundColor Yellow
		Write-Host "           Vulkan-ValidationLayers のリリースから取得し -ValidationLayerPath で渡すこと" -ForegroundColor Yellow
	} else {
		Write-Note "検証レイヤ: $layerPath"
		$zipEntries += [PSCustomObject]@{ Name = "lib/$Abi/libVkLayer_khronos_validation.so"; Path = $layerPath }
	}
}

Add-EntriesToZip $baseApk $zipEntries


# ---------------------------------------------------------------------------
# 4. zipalign
# ---------------------------------------------------------------------------

Write-Step "zipalign(4 バイト整列 + .so のページ整列)"

# -p は非圧縮の .so をページ境界へ揃える指示。-f は出力の上書き許可
Invoke-Tool $zipalign @('-p', '-f', '4', $baseApk, $alignedApk) 'zipalign'


# ---------------------------------------------------------------------------
# 5. 署名と検証
# ---------------------------------------------------------------------------

Write-Step "apksigner sign"

if (Test-Path -LiteralPath $OutApk) {
	Remove-Item -LiteralPath $OutApk -Force
}

# minSdk 33 なので v1(JAR)署名は検証に使われない。v2 / v3 は apksigner が既定で付ける。
# v4 は無効化する。既定で APK の隣に .idsig を作るが、adb の incremental install 専用で
# 単体の APK としては使わないため、出力物を 1 本に保つ
Invoke-Tool $apksigner @(
	'sign',
	'--ks', $Keystore,
	'--ks-pass', 'pass:android',
	'--ks-key-alias', 'androiddebugkey',
	'--key-pass', 'pass:android',
	'--v4-signing-enabled', 'false',
	'--out', $OutApk,
	$alignedApk
) 'apksigner sign'

Write-Step "apksigner verify"
Invoke-Tool $apksigner @('verify', '--verbose', '--print-certs', $OutApk) 'apksigner verify'

Write-Step "aapt2 dump badging"
& $aapt2 dump badging $OutApk
if ($LASTEXITCODE -ne 0) {
	Stop-WithError "aapt2 dump badging が失敗しました(終了コード $LASTEXITCODE)"
}


# ---------------------------------------------------------------------------
# 後片付け
# ---------------------------------------------------------------------------

Remove-Item -LiteralPath $stageDir -Recurse -Force

$apkInfo = Get-Item -LiteralPath $OutApk
Write-Step "完了"
Write-Host ("     {0}  ({1:N2} MB)" -f $apkInfo.FullName, ($apkInfo.Length / 1MB)) -ForegroundColor Green
Write-Host "     インストール: adb install -r `"$($apkInfo.FullName)`"" -ForegroundColor Green
