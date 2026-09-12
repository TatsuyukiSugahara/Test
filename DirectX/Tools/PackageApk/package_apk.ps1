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
	  3. lib/<abi>/libGame.so(と任意で Vulkan 検証レイヤ)を並べる
	  4. Game/Assets を assets/Game/Assets/... として並べ、
	     展開用の asset_index.txt / asset_stamp.txt を作る
	  5. 2 の APK をベースに 3・4 を流し込んだ APK を書き出す
	  6. zipalign -p 4 で整列する
	  7. apksigner で署名し、apksigner verify で検証する

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

	# APK の assets/ へ入れるディレクトリ。既定は Game\Assets(.spv もこの下)
	[string]$AssetsDir,

	# assets/<AssetsPrefix>/... というエントリ名の頭に付ける部分。
	# エンジン側はソースツリー相対("Game/Assets/...")でパスを解決するので、
	# 展開先でその構造が再現されるようここを合わせる
	[string]$AssetsPrefix = 'Game/Assets',

	# アセットを一切入れない(ネイティブだけ差し替えて試すとき用)
	[switch]$NoAssets,

	# APK へ追加するエントリの圧縮レベル。Fastest が既定なのは、
	# アセットが 100MB 規模あり Optimal では圧縮に時間がかかりすぎるため
	[ValidateSet('Optimal', 'Fastest', 'NoCompression')]
	[string]$CompressionLevel = 'Fastest',

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
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()


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


# aapt2 が作った APK をコピーし、そこへネイティブライブラリとアセットを足す。
#
# ZipArchiveMode::Update を使うのは、aapt2 が作った既存エントリを
# 「圧縮方式ごとそのまま」残せる唯一の手段だから。
# resources.arsc は API 30 以降「無圧縮 (Stored)」でなければインストールが
# 拒否される(INSTALL_PARSE_FAILED_RESOURCES_ARSC_COMPRESSED)のに、
# .NET Framework の ZipArchive は CompressionLevel::NoCompression を渡しても
# Stored ではなく Deflate(レベル 0)で書く。つまり新しい zip へ詰め直すと
# resources.arsc が圧縮扱いになってインストールできなくなる。
# Update モードなら触らないエントリは元のバイト列のまま残るので問題が起きない。
#
# エントリ名は呼び出し側が組み立てたものをそのまま使う。zip の区切りは '/' で、
# aapt2 の -A は Windows のパス区切り '\' をエントリ名に残してしまい
# AAssetManager から引けなくなるので、-A は使わずここで足す。
function Add-EntriesToApk([string]$sourceApk, [string]$destApk, [object[]]$entries, [string]$levelName)
{
	$level = [System.Enum]::Parse([System.IO.Compression.CompressionLevel], $levelName)

	Copy-Item -LiteralPath $sourceApk -Destination $destApk -Force

	$zip = [System.IO.Compression.ZipFile]::Open($destApk, [System.IO.Compression.ZipArchiveMode]::Update)
	try {
		foreach ($entry in $entries) {
			$existing = $zip.GetEntry($entry.Name)
			if ($null -ne $existing) {
				$existing.Delete()
			}
			$created = $zip.CreateEntry($entry.Name, $level)
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
		}
	} finally {
		# Update モードは追加分をここで初めて書き出す(それまではメモリ上)
		$zip.Dispose()
	}
}


# C++ 側が 1 行ずつ素朴に読むので、UTF-8 / BOM なし / LF で書く
function Write-PlainTextFile([string]$path, [string[]]$lines)
{
	$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
	[System.IO.File]::WriteAllText($path, (($lines -join "`n") + "`n"), $utf8NoBom)
}


# resources.arsc が無圧縮(Stored)で入っていることを確認する。
#
# Android 30(R)以降は、インストール時に resources.arsc が
#   ・無圧縮(Stored)
#   ・4 バイト境界に整列
# の 2 条件を満たすことを要求し、満たさないと
#   Failure [-124: ... requires the resources.arsc of installed APKs to be
#   stored uncompressed and aligned on a 4-byte boundary]
# でインストールが拒否される。整列は zipalign が直せるが、
# 圧縮は zip へ入れる時点で決まり zipalign では解けないので、ここで検査する。
#
# 罠: .NET Framework の ZipArchive は CompressionLevel::NoCompression を渡しても
# Stored ではなく Deflate(レベル 0)で書く(40 バイトが 45 バイトに膨らむ)。
# つまり「新しい zip へ詰め直して無圧縮指定」では条件を満たせない。
# aapt2 が Stored で書いた resources.arsc を Update モードでそのまま残すのが唯一の手。
function Test-ApkResourcesArsc([string]$apkPath)
{
	$zip = [System.IO.Compression.ZipFile]::OpenRead($apkPath)
	try {
		$arsc = $zip.GetEntry('resources.arsc')
		if ($null -eq $arsc) {
			Stop-WithError "resources.arsc が APK にありません: $apkPath"
		}
		if ($arsc.CompressedLength -ne $arsc.Length) {
			Write-Host ("       resources.arsc: Length={0} CompressedLength={1}" -f $arsc.Length, $arsc.CompressedLength) -ForegroundColor Red
			Stop-WithError "resources.arsc が圧縮されています。Android 30 以降はインストールを拒否します(zipalign では直せません)"
		}
		Write-Note ("resources.arsc は無圧縮 Stored(Length={0} = CompressedLength={1})" -f $arsc.Length, $arsc.CompressedLength)
	} finally {
		$zip.Dispose()
	}
}


# エントリ名に '\' が混ざっていないことを確認する。
# 混ざると端末側の AAssetManager / 展開処理から引けなくなる
function Test-ApkEntryNames([string]$apkPath)
{
	$zip = [System.IO.Compression.ZipFile]::OpenRead($apkPath)
	try {
		$total = $zip.Entries.Count
		$bad = @($zip.Entries | Where-Object { $_.FullName.Contains('\') })
		Write-Note ("エントリ数 {0} / '\' を含むもの {1}" -f $total, $bad.Count)
		if ($bad.Count -gt 0) {
			foreach ($entry in ($bad | Select-Object -First 5)) {
				Write-Host "       $($entry.FullName)" -ForegroundColor Red
			}
			Stop-WithError "エントリ名に '\' が混ざっています(AAssetManager から引けません)"
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
if ([string]::IsNullOrWhiteSpace($AssetsDir)) {
	$AssetsDir = Join-Path $directXRoot 'Game\Assets'
}
if ($NoAssets) {
	$AssetsDir = ''
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
$payloadApk = Join-Path $stageDir 'base-payload.apk'
$alignedApk = Join-Path $stageDir 'base-aligned.apk'

# アセットは aapt2 の -A ではなく後段の zip 追加で入れるので、link はマニフェストだけ
$linkArgs = @('link', '-o', $baseApk, '-I', $androidJar, '--manifest', $ManifestPath)

# debuggable はマニフェストに書かず、Debug 構成のときだけ aapt2 に入れさせる。
# マニフェストを構成ごとに複製しないための措置
if ($Config -eq 'Debug') {
	$linkArgs += '--debug-mode'
}

Invoke-Tool $aapt2 $linkArgs 'aapt2 link'


# ---------------------------------------------------------------------------
# 3. ネイティブライブラリの追加リスト
# ---------------------------------------------------------------------------

Write-Step "lib/$Abi へネイティブライブラリを追加"

Add-Type -AssemblyName 'System.IO.Compression' | Out-Null
Add-Type -AssemblyName 'System.IO.Compression.FileSystem' | Out-Null

$soFull = (Resolve-Path -LiteralPath $SoPath).Path
$zipEntries = @()
$zipEntries += [PSCustomObject]@{ Name = "lib/$Abi/libGame.so"; Path = $soFull }
Write-Note ("libGame.so     : {0:N1} MB" -f ((Get-Item -LiteralPath $soFull).Length / 1MB))

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


# ---------------------------------------------------------------------------
# 4. アセットの追加リストとメタファイル
# ---------------------------------------------------------------------------

$assetCount = 0
$assetBytes = 0

if ([string]::IsNullOrWhiteSpace($AssetsDir)) {
	Write-Step "アセットの同梱をスキップ(-NoAssets)"
} else {
	Write-Step "assets/$AssetsPrefix へアセットを追加"

	if (-not (Test-Path -LiteralPath $AssetsDir -PathType Container)) {
		Stop-WithError "-AssetsDir が見つかりません: $AssetsDir"
	}
	$assetsFull = (Resolve-Path -LiteralPath $AssetsDir).Path

	# 中間物(ステージングディレクトリ)がアセットの下にあると、自分の出力を
	# アセットとして取り込んでしまう
	if ($stageDir.StartsWith($assetsFull, [StringComparison]::OrdinalIgnoreCase)) {
		Stop-WithError "-OutApk を -AssetsDir の中に置けません(中間物がアセットに混ざる): $OutApk"
	}

	# 並び順を固定する。asset_stamp.txt の値と一覧の内容を実行ごとに揺らさないため
	$assetFiles = Get-ChildItem -LiteralPath $assetsFull -Recurse -File | Sort-Object -Property FullName

	if ($assetFiles.Count -eq 0) {
		Stop-WithError "-AssetsDir にファイルがありません: $assetsFull"
	}

	$prefix = $AssetsPrefix.Trim('/')
	$indexLines = @()

	foreach ($assetFile in $assetFiles) {
		# AssetsDir からの相対パスを '/' 区切りへ直す。
		# ここで作った名前がそのまま端末側の展開先の相対パスになる
		$relative = $assetFile.FullName.Substring($assetsFull.Length).TrimStart('\', '/').Replace('\', '/')
		$logicalPath = "$prefix/$relative"

		$indexLines += $logicalPath
		$zipEntries += [PSCustomObject]@{ Name = "assets/$logicalPath"; Path = $assetFile.FullName }

		$assetCount++
		$assetBytes += $assetFile.Length
	}

	# AAssetDir はサブディレクトリを返さないため、端末側は APK 内を再帰的に
	# 列挙できない。展開に必要なファイル一覧を APK 自身に持たせる
	$indexFile = Join-Path $stageDir 'asset_index.txt'
	Write-PlainTextFile $indexFile $indexLines
	$zipEntries += [PSCustomObject]@{ Name = 'assets/asset_index.txt'; Path = $indexFile }

	# 展開済み判定用の印。展開先に置いた同名ファイルと文字列比較するだけなので、
	# 内容は「ファイル数 合計バイト数」の 1 行で足りる
	$stampFile = Join-Path $stageDir 'asset_stamp.txt'
	Write-PlainTextFile $stampFile @("$assetCount $assetBytes")
	$zipEntries += [PSCustomObject]@{ Name = 'assets/asset_stamp.txt'; Path = $stampFile }

	Write-Note "元ディレクトリ : $assetsFull"
	Write-Note ("ファイル数     : {0}" -f $assetCount)
	Write-Note ("合計バイト数   : {0:N0} ({1:N1} MB)" -f $assetBytes, ($assetBytes / 1MB))
	Write-Note ("asset_stamp    : {0} {1}" -f $assetCount, $assetBytes)
}


# ---------------------------------------------------------------------------
# 5. APK の組み立て
# ---------------------------------------------------------------------------

Write-Step "APK の組み立て(圧縮レベル $CompressionLevel)"

Add-EntriesToApk $baseApk $payloadApk $zipEntries $CompressionLevel
Test-ApkEntryNames $payloadApk
Test-ApkResourcesArsc $payloadApk


# ---------------------------------------------------------------------------
# 6. zipalign
# ---------------------------------------------------------------------------

Write-Step "zipalign(4 バイト整列 + .so のページ整列)"

# -p は非圧縮の .so をページ境界へ揃える指示。-f は出力の上書き許可
Invoke-Tool $zipalign @('-p', '-f', '4', $payloadApk, $alignedApk) 'zipalign'


# ---------------------------------------------------------------------------
# 7. 署名と検証
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

# zipalign と署名で zip を書き直すので、実機へ渡す成果物そのものを検査する。
# ここが通らない APK はインストールで弾かれる
Write-Step "最終 APK の検査"
Test-ApkEntryNames $OutApk
Test-ApkResourcesArsc $OutApk

# zipalign -c は整列の検査。resources.arsc の行に注目する
Write-Note "zipalign -c -v 4(resources.arsc の整列を確認)"
$alignReport = & $zipalign -c -v 4 $OutApk
if ($LASTEXITCODE -ne 0) {
	$alignReport | Where-Object { $_ -match 'BAD|resources\.arsc' } | ForEach-Object { Write-Host "       $_" -ForegroundColor Red }
	Stop-WithError "zipalign -c で整列不足が見つかりました(終了コード $LASTEXITCODE)"
}
$alignReport | Where-Object { $_ -match 'resources\.arsc|libGame\.so|Verification' } | ForEach-Object { Write-Host "       $_" -ForegroundColor DarkGray }

$apkInfo = Get-Item -LiteralPath $OutApk
$stopwatch.Stop()

Write-Step "完了"
Write-Host ("     {0}" -f $apkInfo.FullName) -ForegroundColor Green
Write-Host ("     APK サイズ     : {0:N0} バイト ({1:N2} MB)" -f $apkInfo.Length, ($apkInfo.Length / 1MB)) -ForegroundColor Green
Write-Host ("     アセット       : {0} ファイル / {1:N0} バイト ({2:N1} MB)" -f $assetCount, $assetBytes, ($assetBytes / 1MB)) -ForegroundColor Green
Write-Host ("     所要時間       : {0:N1} 秒" -f $stopwatch.Elapsed.TotalSeconds) -ForegroundColor Green
Write-Host "     インストール: adb install -r `"$($apkInfo.FullName)`"" -ForegroundColor Green
