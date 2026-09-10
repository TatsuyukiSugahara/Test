# SetupXboxDevMode — Xbox(UWP / 道A)セットアップ手順

aqEngine を **UWP(AppContainer)** としてビルド・パッケージ化し、PC と **Xbox 実機(retail 機の Dev Mode)**
で動かすための手順をまとめたフォルダです。

主経路は **道A:UWP(GDK 不要・NDA なし・個人で着手可)**。設計の全体像は
`DirectX/設計書/Xbox移植設計.md` / `Xbox_UWP移植_変更まとめ.md` を参照。

## 現在の到達点(2026-09-10 実測)

| 項目 | 状態 |
|---|---|
| VS 2026(VS18)+ v145 での UWP ビルド | **OK**(VS 2019 は不要) |
| `.msix` パッケージ生成(約 75MB、アセット同梱) | **OK** |
| PC への sideload インストール / 起動 | **OK** |
| D3D12 `CreateSwapChainForCoreWindow` / ゲームループ到達 | **OK** |
| 画面表示(UI) | **未**(下記「既知の未解決」) |
| Xbox 実機へのデプロイ | 未検証(Dev Mode 機が手元に無いため) |

### 既知の未解決
UWP では **CWD がパッケージの読み取り専用ルート**で、ゲームアセットは `<package>/Game/Assets/...` に入る。
そのため **CWD 相対でファイルを開くコードが全滅する**。

- 修正済み: 画像ローダ(`Resource/ImageLoader.cpp`, DDS/TGA/PNG 全経路)。
  `aq::res::ResolveExistingResourcePath()`(`Resource.h`)を通すようにした。
- **未修正**: `Util/SimpleJson.cpp` の `JsonParser::ParseFile` が `std::ifstream` を直に開くため、
  **JSON アセット(フォント atlas.json / UI 画面 / ステージ / レベル / パーティクル等)が読めない**。
  フォントが準備できず 15 秒タイムアウトし、**画面がグレーのまま**になる
  (`LocalState\startup.log` に `[boot] -> Title shown (boot wait 15.00 s, ...)` が出て
  `[boot] UI font ready` が出ないのが目印)。ここに同じ解決を通せば解消する見込み。
- 同種の生ファイルアクセスが他にも残存(要確認): `Sound/Decoder/WavDecoder.cpp`,
  `WavStreamDecoder.cpp`, `Graphics/Vulkan/VulkanShader.cpp`, `Graphics/D3D11/D3D11Shader.cpp`,
  `Resource/Resource.cpp`(3 箇所), `Core/Application.cpp`。

---

## 1. PC 環境セットアップ(別 PC で再現する手順)

### 1.1 必要なもの / 要らないもの

| | 必要 | 備考 |
|---|---|---|
| Visual Studio 2026(VS18)Community | ○ | **VS 2019 / VS 2022 は不要**(§1.2 参照) |
| UWP C++ コンポーネント | ○ | `Microsoft.VisualStudio.ComponentGroup.UWP.VC.v143` |
| Windows 11 SDK 10.0.22621.0 | ○ | vcxproj が `WindowsTargetPlatformVersion` で指定 |
| NuGet パッケージ復元 | ○ | `DirectX/packages` は .gitignore 対象 |
| 自己署名証明書(.pfx) | ○ | `GameUWP_TemporaryKey.pfx` も .gitignore 対象 |
| 開発者モード(Windows) | ○ | sideload に必要 |
| GDK / GDKX / NDA / ID@Xbox | ✕ | 道A では不要 |
| FBX SDK(`FBXSDK_DIR`) | ✕ | **不要**。FBX 読み込みは同梱の ufbx を使う(include パスの残骸) |
| Bullet の prebuilt lib | ✕ | UWP はソリューションが Bullet をソースからビルドする |

### 1.2 UWP C++ ツールセットを入れる

**VS 2026 単体で UWP を v145 のままビルドできる。**
VS18 のインストーラカタログは `ComponentGroup.UWP.VC.v143`(Component ではなく **ComponentGroup**)を
提供しており、これを入れると VS18 自身の MSBuild 側にも Windows Store のターゲットが展開される:

```
MSBuild\Microsoft\VC\v180\Application Type\Windows Store\10.0\Platforms\x64\PlatformToolsets\v145\
```

つまり **v143 に落とさず v145 のまま**組める(本リポジトリの Xbox 構成は v145)。

```powershell
$inst = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\setup.exe"
$a = @("modify",
       "--installPath","`"C:\Program Files\Microsoft Visual Studio\18\Community`"",
       "--add","Microsoft.VisualStudio.ComponentGroup.UWP.VC.v143",
       "--passive","--norestart")
# 昇格して起動すること(下記の落とし穴を参照)
Start-Process -FilePath $inst -ArgumentList $a -Verb RunAs -Wait
```

> ⚠️ **`--passive` / `--quiet` は最初から管理者権限で起動しないと拒否される。**
> インストーラは自分で UAC を出さず、終了コード **5007** で即終了し
> ログに `Commands with --quiet or --passive should be run elevated from the beginning.` を残す。
> 必ず `-Verb RunAs`(または管理者プロンプト)から実行する。

導入確認:

```powershell
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
  -all -products * -requires Microsoft.VisualStudio.ComponentGroup.UWP.VC.v143 -property installationPath
# → VS18 のパスが出れば OK
Get-ChildItem "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Microsoft\VC\v180\Application Type"
# → "Windows Store" が並んでいれば OK
```

### 1.3 NuGet パッケージを復元する

`DirectX/Game/packages.config` が要求するもの(`DirectX/packages` は .gitignore):

| パッケージ | バージョン | 用途 |
|---|---|---|
| `directxtex_uwp` | 2026.5.8.1 | UWP は /MD 必須のため NuGet 版 DirectXTex を使う |
| `Microsoft.Windows.CppWinRT` | 2.0.210312.4 | C++/WinRT |

`DirectX/DirectX.sln` を Visual Studio で一度開けば自動復元される。CLI なら
[nuget.org](https://www.nuget.org/downloads) の `nuget.exe` を取得して:

```powershell
nuget restore Z:\Git\Test\DirectX\DirectX.sln
```

> 未復元だと `EnsureNuGetPackageBuildImports` ターゲットが
> 「このプロジェクトは、このコンピューター上にない NuGet パッケージを参照しています」で止まる。

### 1.4 署名用の自己署名証明書を作る

`GameUWP_TemporaryKey.pfx` は署名鍵なので **リポジトリに入っていない**(.gitignore)。
**Subject は `Package.appxmanifest` の `Identity/@Publisher` と一致していなければならない**(現在 `CN=t-sugahara`)。

```powershell
$cert = New-SelfSignedCertificate -Type Custom -Subject "CN=t-sugahara" `
  -KeyUsage DigitalSignature -KeyExportPolicy Exportable `
  -FriendlyName "aqEngine Game UWP dev signing" `
  -CertStoreLocation "Cert:\CurrentUser\My" `
  -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3","2.5.29.19={text}")

# パスワード無し pfx として書き出す(VS の一時キーと同じ扱い)
$bytes = $cert.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Pfx, "")
[System.IO.File]::WriteAllBytes("Z:\Git\Test\DirectX\Game\GameUWP_TemporaryKey.pfx", $bytes)
```

無いままビルドすると `APPX0104: 証明書ファイル 'GameUWP_TemporaryKey.pfx' が見つかりません` になる。

### 1.5 Windows の開発者モードを有効化する(sideload に必要)

```powershell
# 要管理者
reg add "HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\AppModelUnlock" `
  /t REG_DWORD /f /v AllowDevelopmentWithoutDevLicense /d 1
```

設定アプリの「システム → 開発者向け → 開発者モード」でも同じ。

---

## 2. ビルドとパッケージ生成

**単体 vcxproj ではなくソリューションを叩くこと**(Bullet / imgui の依存が解決される)。
単体だと `LNK1104: BulletCollision.lib を開くことができません` になる。

```powershell
$msbuild = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
$pkgdir  = "Z:\Git\Test\DirectX\x64\DebugXbox\AppPackages\"

& $msbuild "Z:\Git\Test\DirectX\DirectX.sln" `
  /p:Configuration=DebugXbox /p:Platform=x64 `
  /p:GenerateAppxPackageOnBuild=true /p:UapAppxPackageBuildMode=SideloadOnly /p:AppxBundle=Never `
  "/p:AppxPackageDir=$pkgdir" `
  /nodeReuse:false /m
```

成果物:

```
x64\DebugXbox\AppPackages\GameUWP_0.1.0.0_x64_DebugXbox_Test\
  GameUWP_0.1.0.0_x64_DebugXbox.msix                      … 本体(約 75MB、アセット同梱)
  Dependencies\x64\Microsoft.VCLibs.x64.Debug.14.00.appx   … 依存
```

> ゲームアセットは vcxproj の `<None Include="Assets\**\*.*">`(ワイルドカード)で同梱されるため、
> **アセットを追加しても vcxproj の編集は不要**。パッケージ内では `Game\Assets\...` に配置される
> (ソースツリーの `DirectX/Game` を基点とする構造の再現)。

---

## 3. PC へインストールして起動する

**C++ UWP プロジェクトに `Deploy` ターゲットは無い**(`MSB4057` になる)。
msix を作って `Add-AppxPackage` で入れる。

```powershell
# 自己署名を信頼させる(初回のみ、要管理者)
$cer = "$env:TEMP\aqengine_uwp_dev.cer"
Export-Certificate -Cert (Get-Item "Cert:\CurrentUser\My\<thumbprint>") -FilePath $cer -Force
Start-Process powershell -Verb RunAs -Wait -ArgumentList "-NoProfile","-Command",
  "Import-Certificate -FilePath '$cer' -CertStoreLocation Cert:\LocalMachine\TrustedPeople"

# インストール
$base = "Z:\Git\Test\DirectX\x64\DebugXbox\AppPackages\GameUWP_0.1.0.0_x64_DebugXbox_Test"
Add-AppxPackage -Path "$base\GameUWP_0.1.0.0_x64_DebugXbox.msix" `
                -DependencyPath "$base\Dependencies\x64\Microsoft.VCLibs.x64.Debug.14.00.appx"

# 起動(PFN は Get-AppxPackage で確認。AppId は "App")
Start-Process "shell:AppsFolder\com.example.aqEngineGame_6jv2p5dtxq5hj!App"
```

信頼させないと `APPX0107: 指定された証明書は署名に使用できません` 相当で弾かれる。

### ログの見方(実機デバッグの命綱)

UWP は **CWD が読み取り専用**なので `startup_timing.log` は作れない。診断は下記に出る:

```
%LOCALAPPDATA%\Packages\<PFN>\LocalState\startup.log
```

`aq::StartupMark` / `aq::StartupLog` はどちらもここへ流れる(Xbox 実機はデバッガを繋げないため、
このログが唯一の手がかりになる)。画像ロード失敗は `[img] WIC load failed hr=... path=...` として残る。

### ウィンドウを掴むとき

UWP は CoreWindow を `ApplicationFrameHost` がホストするため、
**`Process.MainWindowHandle` が取れない**(0 になる)。スクリーンショット等でウィンドウが要る場合は
`EnumWindows` でタイトル `aqEngine Game` を探すこと。

---

## 4. Xbox 実機(Dev Mode)へ

> ここから先は **Dev Mode の Xbox 実機が必要**。未検証。

### 4.1 Dev Mode を有効化する

1. Xbox 本体の Microsoft Store で **「Dev Mode Activation」** を入手して起動。
2. [Partner Center](https://partner.microsoft.com/) で **開発者アカウント登録**(個人 ~$19、一度きり)。
3. 表示されたコードを Partner Center 側で登録 → **Dev Mode へ切替**(本体再起動)。
4. **Dev Home** で以下を控える / 設定する:
   - コンソールの **IP アドレス**(Remote Access)
   - **デバイスポータル**の有効化(`https://<IP>:11443`)
   - **ペアリング用 PIN**

> 戻すときは Dev Home の「Retail へ切替」。データは消えるので注意。

### 4.2 ★「Game」分類でデプロイする(最重要)

Dev Mode の UWP は **「App」分類だと約 1GB に制限**される。**「Game」分類**にして初めて下記が使える:

| リソース | App 分類 | **Game 分類** |
|---|---|---|
| メモリ(RAM) | ~1GB | **最大 5GB**(Series X/S 共通) |
| CPU | 2〜4 コアのシェア | **4 コア占有 + 2 コア共有** |
| バックグラウンド時 | — | 128MB(超過で強制終了) |
| 1 ファイルのサイズ上限 | — | 2GB |

1. 一度デプロイする。
2. **Dev Home → マイゲーム＆アプリ** で対象を選び **種類を「ゲーム(Game)」** に設定
   (UI はシステム更新で変わる。無ければデバイスポータルの該当アプリ設定から)。
3. 再デプロイして 5GB プロファイルで動くことを確認。

> エンジン側は `PlatformBudget.h` で 5GB / 8MB スタック / 6 ワーカ / 2GB ファイルとして反映済み。
> **必ず「Game」でデプロイすること**が前提。
> ※ PC 上で動かすと `AppMemory limit=34572MB` のように出る。この制約は実機で初めて効く。

### 4.3 残り作業

- [ ] JSON アセットのパス解決(§現在の到達点「既知の未解決」)… これが済むと PC で UI が出る
- [ ] 実機への転送・起動確認
- [ ] GameInput(キーボード/マウス/パッド)の UWP 対応
- [ ] suspend 時に 128MB 以下へメモリ解放

---

## 5. 落とし穴まとめ

| 症状 | 原因 / 対処 |
|---|---|
| インストーラが即終了(exit **5007**) | `--passive`/`--quiet` は**最初から昇格**が必要。`-Verb RunAs` で起動 |
| `MSB8020`(v142 が見つからない) | Xbox 構成のツールセットが古い。本リポジトリは **v145** に統一済み |
| `LNK1104: BulletCollision.lib` | 単体 vcxproj を叩いている。**ソリューション**を `Configuration=DebugXbox` でビルドする |
| `APPX0104` 証明書が無い | §1.4 で自己署名 pfx を生成する(.gitignore 対象なのでクローン直後は必ず無い) |
| `MSB4057: ターゲット "Deploy" は存在しない` | C++ UWP に `Deploy` は無い。msix を作って `Add-AppxPackage` |
| 起動するが**画面がグレー** | CWD 相対のファイルアクセス。§現在の到達点「既知の未解決」を参照 |
| ログが何も出ない | UWP の CWD は読み取り専用。`LocalState\startup.log` を見る |
| デスクトップ専用 API が UWP に混入 | **`_WIN32` は UWP でも定義される**。デスクトップ限定は `AQ_PLATFORM_WIN32` で判定する |
| `.vcxproj` の差分が全行になる | BOM+CRLF のファイルがある。Git Bash の `sed -i` が CRLF を LF に変換する。 PowerShell の `ReadAllText`/`WriteAllText` を使い、`git diff --numstat` で確認する |
| `imgui.ini` に差分が出る | 実行時生成物。コミットしない |

## 6. このフォルダの中身

| ファイル | 用途 |
|---|---|
| `README.md` | 本書 |
| `UWP.props` | UWP 構成の MSBuild プロパティ集(雛形) |
| `Package.appxmanifest.template` | パッケージマニフェスト雛形 |

> 実際に使われている構成は `DirectX/Game/GameUWP.vcxproj` と `DirectX/Game/Package.appxmanifest`。
> 上記 2 つは初期の雛形で、現行構成とは差がある。
