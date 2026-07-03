# Xbox(UWP / 道A)移植 変更まとめ（レビュー用）

最終更新: 2026-07-03 / ブランチ: `feature/xbox-uwp-phase3`

製品版 Xbox One を **開発者モード**にして、UWP アプリ（道A）としてエンジン（aqEngine）を
**起動 → 実機 GPU で描画**するまでに行った、ファイル追加・変更・プロジェクト追加を細かくまとめる。
GDK（道B）は使わない個人開発方針。

---

## 0. 結論（現状の到達点）

| 項目 | 状態 |
|---|---|
| 実機 Xbox One で UWP アプリ起動・エンジン初期化 | ✅ |
| D3D11（FL10_1）でデバイス/スワップチェーン生成 | ✅ |
| モデル/地形の描画 + **テクスチャ** | ✅ |
| ライティング（陰影） | ⚠️ 平坦（シャドウ要因を調査中） |
| Bloom / 海 / GPU カリング / HiZ / HDR トーンマップ | ⛔ FL10 制約で無効 |
| 入力（GameInput） | ❌ no-op |

### 最重要の技術的発見
**Xbox One の UWP アプリコンテナが公開する GPU は D3D11 フィーチャーレベル 10_1 が上限**。
- D3D12 は実 GPU（`SraKmd`）が全フィーチャーレベルで `DXGI_ERROR_UNSUPPORTED`、成功するのは WARP（ソフト）のみ。
- D3D11 はハードウェアで動くが **FL10_1**（11_0 非対応）。
- **SM5.0 シェーダは FL11 必須**なので、エンジン（全面 SM5.0 + コンピュート）はそのままでは描画不可 → 黒画面だった。
- フル品質（HDR / Bloom / GPU 駆動カリング / 海 FFT / コンピュート）は **D3D12 or FL11 が前提 = 道B(GDK) が必要**。
- 今回は「割り切り」で **D3D11 + FL10 フォールバック**を実装して実機描画にこぎ着けた。

---

## 1. 追加したプロジェクト

### `Game/GameUWP.vcxproj`（新規・UWP アプリ本体）
- **GUID**: `{B7E2B9A1-4C3D-4E5F-8A6B-9C0D1E2F3A4B}` / ProjectName=`GameUWP`
- **なぜ別プロジェクトにしたか**:
  当初は既存 `Game`（デスクトップ EXE）に Xbox 構成を足す方針だったが、**VS はアプリ種別（デスクトップ/UWP）をプロジェクト単位でしか判定しない**。単一 vcxproj をデスクトップ/UWP で構成切替すると、デバッグ対象に UWP「ローカル コンピューター」が出ない／`AppHostLocalDebugger` 指定で起動できない等の壁があり、**UWP アプリは独立プロジェクトに分離せざるを得なかった**（コミット `829fa4e`）。
- **Globals（UWP 化の要）**:
  - `AppContainerApplication=true`, `ApplicationType=Windows Store`, `ApplicationTypeRevision=10.0`
  - `WindowsTargetPlatformVersion=10.0.22621.0`, `WindowsTargetPlatformMinVersion=10.0.19041.0`
  - `Microsoft.Cpp.Default.props` の直後に `<TargetPlatformVersion>10.0.22621.0</TargetPlatformVersion>` を**明示上書き**（UWP の SDK 解決が空になり CppWinRT が MSB8036 で失敗する問題への対処。コミット `009d925`）
- **構成**: `DebugXbox|x64` / `ReleaseXbox|x64` のみ（`ConfigurationType=Application`, `PlatformToolset=v142`）
- **重要なコンパイル/リンク設定**:
  - `PreprocessorDefinitions`: `AQ_PLATFORM_UWP; WINAPI_FAMILY=WINAPI_FAMILY_APP`（＋Bullet 定義）。**AQ_IMGUI は付けない**（Xbox は imgui 無効）
  - `RuntimeLibrary`: Debug=`MultiThreadedDebugDLL` / Release=`MultiThreadedDLL`（**UWP は /MD 必須**。MSB8024 対策 `1233f81`）
  - `ConformanceMode=false`（`/permissive-` OFF。CppWinRT+v142 の ICE 回避 `66768bf`）
  - `LanguageStandard=stdcpp20`
  - `AdditionalIncludeDirectories`: `$(SolutionDir)ThirdParty` を含む（`5fa93bb`）
  - `Link/GenerateWindowsMetadata=false`（.winmd を作らない。APPX0702 対策 `652d610`）
  - `AdditionalDependencies`: `d3d11.lib; d3dcompiler.lib; dxguid.lib; BulletCollision.lib; BulletDynamics.lib; LinearMath.lib`
  - `AdditionalLibraryDirectories`: `$(SolutionDir)x64\$(Configuration)`（Bullet の Xbox lib をここから）
  - `OutDir/IntDir` 明示（`8952d69`）
- **参照**: `aqEngine/Engine.vcxproj`（エンジン静的 lib）を ProjectReference
- **アプリのアイコン**: `Assets\StoreLogo.png` ほか 4 種を `<Image>` 登録
- **アセット同梱（後述 §5.3）**: `Assets\**` を `install/Game/Assets/...` に、`aqEngine/Graphics/MaterialDef.h` を `install/aqEngine/Graphics/...` に同梱
- **NuGet インポート**（末尾 ImportGroup）:
  - `Microsoft.Windows.CppWinRT.2.0.210312.4`（props/targets）
  - `directxtex_uwp.2026.5.8.1`（targets）
- **署名/パッケージ設定**（`UserMacros`。VS の「アプリ パッケージの作成」で付与）:
  - `AppxPackageSigningEnabled=True`, `PackageCertificateKeyFile=GameUWP_TemporaryKey.pfx`, `AppxBundle=Never` 等
  - ※ `GameUWP_TemporaryKey.pfx`（秘密鍵）は**コミットしていない**。`vcxproj` の署名設定変更も未コミット（ローカル保持。`*.pfx` は .gitignore 推奨）

> `Game`（デスクトップ EXE）と `GameUWP` は **同じソース/マニフェスト/Assets を共有**する。ソースは `Game/Application/**` を両方が参照。

---

## 2. 新規追加ファイル（意図つき）

### プラットフォーム抽象（Phase 0、main にマージ済み）
- `aqEngine/Platform/IPlatform.h` — プラットフォーム抽象インターフェース（`CreateMainWindow` / `PumpEvents` / `OnSuspend`/`OnResume` / `GetContentRoot`）。Engine から Win32 依存を剥がすため。
- `aqEngine/Platform/PlatformWin32.{h,cpp}` — 既存の WinMain/HWND/メッセージループを移設。`#if !defined(AQ_PLATFORM_UWP)` でガード（UWP では空 TU）。
- `aqEngine/Platform/PlatformBudget.h` — `ResourceBudget`（メモリ/スタック/スレッド数/最大ファイルサイズ）を `constexpr` で返す。UWP は Xbox プロファイル、それ以外は Win32。

### UWP 本体（Phase 3）
- **`aqEngine/Platform/PlatformUWP.{h,cpp}`** — C++/WinRT による `IPlatform` 実装。
  - `CoreWindow` を保持、`Closed` イベントで終了フラグ、`PumpEvents` で `Dispatcher().ProcessEvents`
  - `CreateMainWindow` は `winrt::get_unknown(window_)`（CoreWindow の `IUnknown*`）を返す → スワップチェーン生成に使う
  - `GetContentRoot()` = `Package::Current().InstalledLocation().Path()`（パッケージ install フォルダ＝アセット読み込みの基点）
  - `GetPixelSize()` = `CoreWindow.Bounds()` × DPI
  - include: `winrt/Windows.UI.Core.h` / `ApplicationModel.h` / **`Storage.h`** / `Graphics.Display.h`
  - **本セッションで追加**: `aq::StartupLog()` の実装（後述 §7）
- **`Game/Application/UWPMain.cpp`** — UWP のエントリポイント。
  - `App : implements<App, IFrameworkViewSource, IFrameworkView>` を実装
  - `wWinMain` で **`winrt::init_apartment()`（MTA）** → `CoreApplication::Run(make<App>())`
    （MTA にしないと `CoreApplication` が `hresult_wrong_thread` になる）
  - `App::Run()`: CoreWindow を Activate → `PlatformUWP` 生成 → `Engine::Create` → `Application` 生成 → `Engine::Initialize` → `RunGame`
  - **本セッションで追加**: 各段の `StartupLog` と try/catch（例外の HRESULT/メッセージを記録）
  - `#if defined(AQ_PLATFORM_UWP)` でガード（デスクトップ構成では空 TU。Win32 は `Main.cpp`）

### 入力抽象（Phase 0、main）
- `aqEngine/HID/IPadBackend.h` / `XInputPadBackend.{h,cpp}` / `PadBackend.h` — パッド入力を `IPadBackend` で分離（XInput 実装を切り出し）。UWP では DirectInput/XInput を使わず no-op（GameInput 化は Phase 4）。

### セットアップキット（Phase 0 Part D、main）
- `Tools/SetupXboxDevMode/` — `README.md`（Dev Mode 登録・デプロイ手順・残作業チェックリスト）、`UWP.props`（MSBuild 雛形）、`Package.appxmanifest.template`。

### UWP パッケージ
- **`Game/Package.appxmanifest`**（新規）— UWP パッケージマニフェスト。
  - `Identity`（Name=`com.example.aqEngineGame`, Publisher）, `mp:PhoneIdentity`（VS2019 世代の APPX1673 対策 `46db82c`）
  - `VisualElements` + 各種ロゴ, `TargetDeviceFamily Windows.Universal MinVersion 10.0.19041.0`
  - ※ Publisher は「アプリ パッケージ作成」でテスト証明書に合わせ `CN=t-sugahara` に変更（未コミット）
- `Game/Assets/*.png`（StoreLogo など）— パッケージ生成に必須のプレースホルダロゴ。

### 本セッションのレビュー資料
- **`Xbox_UWP移植_変更まとめ.md`**（本ファイル）

---

## 3. 変更した既存ファイル（何を・なぜ）

### エンジン中核
- **`aqEngine/aq.h`**
  1. **バックエンド既定の切替**: `#if defined(AQ_PLATFORM_UWP)` のとき **`ENGINE_GRAPHICS_D3D11`**、それ以外 `ENGINE_GRAPHICS_D3D12`。
     （Xbox One UWP は D3D12 ハードウェア非対応のため。AQ_PLATFORM_UWP は Xbox 構成で全 TU に定義済み＝これで一貫して D3D11 になる）
  2. **DirectXTex のインクルード分岐**: UWP は NuGet の `#include <DirectXTex.h>`（/MD 版・lib は .targets が自動リンク）、デスクトップは従来 `#include <DirectXTex\DirectXTex.h>` + prebuilt /MT lib を `#pragma comment(lib)`。
  3. **`aq::StartupLog(const char*)` の宣言追加**（起動診断ログ）。
- **`aqEngine/Engine.cpp`**
  - `Engine::Initialize` の各段に `StartupLog`（window / graphics API / sound / audio / application の成否）。
  - バックエンド選択（`#ifdef ENGINE_GRAPHICS_D3D11 → D3D11GraphicsDeviceImpl` …）は元から分岐実装済み。
- **`aqEngine/Core/Application.cpp`**
  - **compute 非対応時（FL10）**は `GpuClusterCuller::Initialize()` と `HiZRenderer` のセットアップを**スキップ**、`SetClusterCullEnabled(false)`。
  - `#include "Rendering/Occlusion/ClusterCull.h"` 追加（`SetClusterCullEnabled` 宣言のため）。

### グラフィクス抽象
- **`aqEngine/Graphics/GraphicsDevice.{h,cpp}`**
  - `aq::graphics::SetComputeSupported(bool)` / `IsComputeSupported()` を追加（グローバル、既定 true）。
    compute/UAV/GPU 駆動が使えるかの単一フラグ。D3D11 が FL<11 を検出したとき false にする。

### D3D11 バックエンド（UWP で実際に使う経路）
- **`aqEngine/Graphics/D3D11/D3D11GraphicsDeviceImpl.cpp`**
  - `Initialize`: UWP は `CreateDeviceAndSwapChainUWP`（HW ドライバ + `CreateSwapChainForCoreWindow`、flip model）を呼ぶ分岐（**UWP パスは Phase 0 で用意、本セッションで実コンパイル/実行された**）。
  - **Win32 用 `CreateDeviceAndSwapChain`（`D3D11CreateDeviceAndSwapChain` 使用）を `#if !defined(AQ_PLATFORM_UWP)` でガード**。この API は UWP で使えないため。
  - デバイス生成後に **`SetComputeSupported(featureLevel_ >= D3D_FEATURE_LEVEL_11_0)`**。
  - 各段 + 失敗 HRESULT の `StartupLog`（`device ok (FL=...)` 等）。
- **`aqEngine/Graphics/D3D11/D3D11Shader.cpp`**
  - **シェーダモデルをデバイス FL に合わせる**: FL11→`_5_0` / FL10_1→`_4_1` / FL10_0→`_4_0`。
    （SM5.0 は FL11 必須。FL10 では SM4 でコンパイル）
  - コンパイル失敗/生成失敗を `[shader] ... FAIL <file>` で記録（どのシェーダが SM4 で落ちるか可視化）。
- **`aqEngine/Graphics/D3D11/D3D11Resources.cpp`**
  - `RenderTarget::Create`: **FL<11 では `D3D11_BIND_UNORDERED_ACCESS`（UAV）を外す**（テクスチャ UAV は FL11 必須。付けると `CreateTexture2D` が失敗）。UAV ビュー生成も同様にスキップ。

### D3D12 バックエンド（デスクトップ用・UWP では無効）
- **`aqEngine/Graphics/D3D12/*.cpp` 11 本**を **`#ifdef ENGINE_GRAPHICS_D3D12` でガード**（D3D11 選択時は空 TU）。
  対象: D3D12Buffers / D3D12DepthMap / D3D12GpuBuffer / D3D12GraphicsDeviceImpl / D3D12ImGui / D3D12PipelineStateCache / D3D12RenderContextImpl / D3D12RenderTarget / D3D12Resources / D3D12RootSignature / D3D12Shader。
  （D3D11 側の .cpp は元から同方式でガード済みだったので対称化。日本語コメントを壊さないようバイト単位でガードを挿入）
  - `D3D12GraphicsDeviceImpl.cpp` は加えて、`CreateSwapChain` の DXGI ファクトリを `_DEBUG` フラグ失敗時に非デバッグで作り直すフォールバックを追加（Graphics Tools 未導入環境向け・PC の D3D12 用）。**アダプタ列挙 + 各 FL 試行のログ**もここで実装し、実機の「SraKmd が UNSUPPORTED / WARP のみ成功」を突き止めた。

### レンダラ
- **`aqEngine/Rendering/Renderer.cpp`**
  - **compute 非対応時**は以下をスキップし、**シーン RT を直接表示**:
    - `GetDisplayRTHandle`: ポストプロセス(Bloom)最終 RT ではなくシーン RT を返す
    - Pass 1.5: GPU クラスタカリング
    - Pass 4: 海（FFT コンピュート依存）
    - Pass 5: ポストプロセス(Bloom)
  - 描画自体は各アイテム `DrawItemCommand`（直接描画）なので、GPU カリング無効でもモデルは描かれる。

### リソース/テクスチャ
- **`aqEngine/Resource/Resource.cpp`**
  - `FindProjectRoot()` が先頭で `Engine::Get().GetContentRoot()`（UWP の install フォルダ）を採用（Phase 3 `13161c9`）。
  - **`TextureLoader::Loading()` の UWP ガードを解除**（本セッション）。DDS/WIC 読み込み + GenerateMipMaps を全プラットフォームで有効化（NuGet directxtex_uwp で /MD lib が使えるため）。
- `aqEngine/Terrain/{HeightmapChunk,HeightmapPainter,SplatmapPainter}.cpp`
  - DirectXTex 呼び出しを `#if !defined(AQ_PLATFORM_UWP)` でガード（Phase 3 `26003f2`）。**まだ UWP 有効化していない**（テクスチャ本体のみ先行対応）。

### シェーダ実行時パス解決
- **`aqEngine/Graphics/D3D12/D3D12Shader.cpp`** — `FindProjectRoot`/`ResolveShaderPath` を `GetContentRoot`（install フォルダ）基点に。UWP でも "Game/" プレフィクスの統一規則で解決（Phase 3 `13161c9`,`b98687a`）。※D3D12 は UWP で未使用だが規則は共通。

---

## 4. ビルド構成・ツールチェーン

- **Windows 開発**: VS 2026（v145）。**Xbox ビルド**: **VS 2019（v142, 16.11.x）**。
  - 理由: VS 2026 / VS 2022 17.14 は **x64 の UWP C++ ビルドツールを削除済み**（MSB8020）。VS 2019 は UWP ワークロードが残る。
- **PlatformToolset**: `v142`（全 Xbox 構成）
- **Windows SDK**: `10.0.22621.0`（CppWinRT が SDK>=22621 を要求）
- **CRT**: `/MDd`（DebugXbox）/ `/MD`（ReleaseXbox）— UWP は動的 CRT 必須
- **NuGet**:
  - `Microsoft.Windows.CppWinRT` **2.0.210312.4**（3.0 / 2.0.240111 は v142 で `winrt/base.h` の内部コンパイラエラー。2021 年版で回避 `8f99f06`）
  - `directxtex_uwp` **2026.5.8.1**（/MD の UWP 版 DirectXTex。aqEngine + GameUWP 両方に追加）
- **ソリューション `DirectX.sln`**: `DebugXbox`/`ReleaseXbox`（x64）構成を追加。GameUWP はこれらで **Build + Deploy**、デスクトップ `Game` は Xbox 構成では非ビルド。
- **Bullet**（物理）: Xbox 構成を追加し、lib を `x64\$(Configuration)` 出力に正規化（`b0c8aaf`,`b98687a` 付近）。

---

## 5. 実機で描画するための要点

### 5.1 D3D11 + FL10 フォールバック（`IsComputeSupported()`）
- D3D11 デバイスが **FL<11** のとき `SetComputeSupported(false)`。
- false のとき:
  - メイン RT を **LDR `R8G8B8A8_UNORM`** で作る（HDR R16F だと LDR バックバッファへ `CopyResource` できずフォーマット不一致になるため。トーンマップ Bloom も使えない）
  - Renderer で **Bloom / 海 / GPU カリング / HiZ をスキップ**し、シーン RT を直接表示
  - Application で GpuClusterCuller / HiZ 初期化をスキップ
  - RenderTarget の UAV バインドを外す
- これで **GBuffer + ディファードライティング(PS)** の結果が直接出る。

### 5.2 シェーダモデルの自動ダウングレード
- FL10_1 では VS/PS を `_4_1` でコンパイル（全て成功）。**cs（Bloom/ClusterCull/HiZ）は SM4 に移植できず失敗**するが、上記スキップで無害。

### 5.3 アセットの appx 同梱（シェーダ #include 解決）
- シェーダは**実行時コンパイル**（appx 内 .fx を D3DCompile）。`Game/Assets/Shader/*.fx` の一部が
  `#include "../../../aqEngine/Graphics/MaterialDef.h"` のように**ソースツリー相対**で engine 共有ヘッダを参照する。
- 対処（`b98687a`）: appx 内に**ソースツリー相対構造を再現**。
  - `Assets\**` → `install/Game/Assets/...`（`<Link>Game\%(Identity)</Link>`）
  - `aqEngine/Graphics/MaterialDef.h` → `install/aqEngine/Graphics/MaterialDef.h`
  - これで相対 include がデスクトップと同じに解決。

### 5.4 テクスチャ（DirectXTex /MD）
- prebuilt DirectXTex は **静的 CRT(/MT)** で UWP の /MD と衝突（LNK2038）→ Phase 3 では UWP で切り離していた。
- 本セッションで **NuGet `directxtex_uwp` 2026.5.8.1** を導入して復活。`.targets` が include パスと `DirectXTex.lib` を自動付与。
- ⚠️ `.targets` の構成判定は `debug`/`release` の**完全一致**。`DebugXbox`/`ReleaseXbox` はどちらも既定の **Release lib(/MD)** を選ぶ →
  - **ReleaseXbox は /MD で一致＝OK**
  - **DebugXbox は /MDd と衝突（LNK2038）** → 当面 **ReleaseXbox でビルド**（DebugXbox が必要なら .targets の判定を直す）

---

## 6. 起動診断ログ（`aq::StartupLog`）
- 実機はデバッガ接続が困難（VS のリモートデプロイは `0x801C0003` で不通）だったため、**ファイルログ**で初期化到達点を特定。
- 宣言: `aq.h`。実装: `PlatformUWP.cpp`（`ApplicationData::Current().LocalFolder()` = `LocalState\startup.log` へ追記）。Win32 は no-op（`PlatformWin32.cpp`）。
- 仕込み位置: `UWPMain::Run`、`Engine::Initialize` 各段、D3D11/D3D12 デバイス初期化各段、`D3D11Shader`（コンパイル/生成失敗）。
- 読み出し: **Device Portal → File explorer → アプリ → `LocalState\startup.log`**。

---

## 7. 実機への配置手順（Device Portal 経由）
1. VS の **リモートデプロイは不通**（`0x801C0003`。port 11080 閉/11443 のみ開）。→ **Device Portal 経由**が確実。
2. VS: `GameUWP` 右クリック → **公開 → アプリ パッケージの作成**（サイドロード / x64 / **ReleaseXbox** / バンドルなし / テスト証明書）。
3. ブラウザ: `https://<XboxIP>:11443` → **My games & apps → Add** で `.msix` + `Dependencies\x64\Microsoft.VCLibs.x64.14.00.appx` をインストール。
4. Xbox のホームから起動。
- ペアリング初期化は Device Portal の「Visual Studio」項目の「すべてのペアリング削除」。

---

## 8. 既知の制約・残作業
- **ライティングが平坦（陰影ゼロ）**: VS/PS は全て SM4.1 で通っている（コンパイル失敗は Bloom cs のみ）。`LightManager` は既定で平行光1灯あり。SM4.1 でも `SampleCmp/SampleCmpLevelZero` 自体は使えるため、シェーダより先に **シャドウ深度パスが実際に描けているか**を疑うのが妥当（深度マップが clear 値のまま比較されると全面影=0 になり、平行光の寄与が消えて ambient だけ＝平坦）。切り分け: (a) DeferredLighting.fx のシャドウ項を色出力するデバッグ、(b) シャドウパスの Draw 数を StartupLog に出す。ただし後述の **PC-UWP で FL10 強制再現**ができれば PIX/RenderDoc で深度パスを直接確認できるので、そちらを先に整えるのが速い（→ §9-2）。
- **DebugXbox の DirectXTex リンク衝突**（§5.4）。最小対処は GameUWP.vcxproj の DebugXbox PropertyGroup に `<NuGetConfiguration>Debug</NuGetConfiguration>` を明示（package の .targets は Configuration 完全一致 or 空のときしか設定しないので、先に入れれば勝てる。構成名変更は lib パス衝突を招くため非推奨 → §9-5）。
- **Terrain の DirectXTex** は UWP 未有効化。
- **入力**: GameInput 未実装（no-op）。
- **音声**: XAudio2（UWP 標準）だが実機での動作未確認。
- **PLM（suspend/resume）**: 未対応。`IPlatform::OnSuspend/OnResume` は配線済みなので、**`OnSuspend` で `IDXGIDevice3::Trim()` を呼ぶ**のを最低限入れる（呼ばないと復帰後に描画が壊れるケースがある）。suspend 中のメモリ制約対応はその後。
- **Xbox のアプリ種別（App / Game）**: Dev Home で種別を Game にするとメモリ・CPU・GPU 割当が大きく変わる（App 扱いだと ~1GB＋GPU 制限）。**さらにフィーチャーレベル（FL10_1 → FL11/D3D12）まで上がる可能性**があり、上がれば FL10 フォールバック自体が不要になる。→ 最優先で検証（§9-0）。※以前メモの "128MB" は PLM サスペンド制約の話でアプリのメモリ割当とは別。
- **Bloom 等の cs COMPILE FAIL ログ**はノイズ（無害）。`BloomRenderer::Initialize` を compute 時のみにすれば消せる。
- **フル品質を出すなら道B(GDK)** が必要（FL11/D3D12/コンピュート）。ただし §9-0 の Game 種別次第では道A のままでも改善余地あり。

### 診断: D3D12 ハードウェア対応 probe（本セッション追加）
- `D3D11GraphicsDeviceImpl.cpp` に `ProbeD3D12Support()` を追加（`#if defined(AQ_PLATFORM_UWP)`）。バックエンドは D3D11 のまま、起動時に `D3D12CreateDevice`（ppDevice=nullptr で対応可否のみ）を各アダプタ×各 FL で試し、`[probe] D3D12 FL 0x.. -> hr=0x..` を startup.log に出す。→ **Xbox を Game 種別に切替えた際、1 回の配置で「D3D11 の FL（既存ログ）」と「D3D12 が通るか（probe）」の両方**が分かる。

---

## 9. 改善バックログ（コードレビュー反映・優先度順）

レビュー所見: Phase 0 で抽象（`IPlatform` によるウィンドウ/イベント/コンテンツルート）を先に切ってから UWP を載せた進め方が効いており、変更が「プラットフォーム層＋アダプター内部＋実行時 caps 分岐」にきれいに収まっている。`#ifdef` をプラットフォーム層とアダプター内部に閉じ込め、compute 可否を実行時フラグ化してゲーム側は `#ifdef` なしで分岐、という構成は設計ガイド通り。以下は投資対効果順の改善候補。

### 9-0.【超短期・上振れ大】Xbox を Game 種別にして FL が上がるか検証
- Dev Home でアプリ種別を App→Game に切替 → ReleaseXbox 再配置 → startup.log 確認。
- 判定: `[D3D11] device ok (FL=0x....)` が **0xB000(11_0) 以上** or `[probe] D3D12 FL ... OK` が出れば、**FL10 フォールバックは不要**になり全品質が狙える。
- 設定変更＋再配置だけで検証でき上振れが大きいので、FL10 再現インフラより先に試す。

### 9-1. `AQ_PLATFORM_UWP` と `AQ_TARGET_XBOX` の分離 ＋ PC での FL10 強制再現
- 現状 `AQ_PLATFORM_UWP` が「アプリモデル(CoreWindow/AppContainer)」と「ターゲットHW(Xbox One=FL10_1)」という**直交2軸**を畳んでいる。UWP は PC でも動き、PC-UWP は D3D12/FL11 が使える。
- 分離案:
  - `AQ_PLATFORM_UWP` … ウィンドウ / ファイル IO / アプリモデル
  - `AQ_TARGET_XBOX` … 性能バジェット・**バックエンド既定**（`AQ_TARGET_XBOX ? D3D11 : D3D12`）
- ※ バックエンドはコンパイル時選択（D3D11/D3D12 で .cpp ガード・include が異なる）。よって「PC 上での FL10 再現」は **PC-UWP + D3D11 + `AQ_DEBUG_FORCE_FL10`（`pFeatureLevels={D3D_FEATURE_LEVEL_10_1}` を強制）** というビルド構成で行う。
- 効果: **§8 のライティング平坦を、Device Portal のログ考古学ではなく PC 上でフルデバッガ + PIX/RenderDoc で再現・解析**できる。実機イテレーションの何倍も速い。Phase 4 の前にやる価値が高い。

### 9-2. `IsComputeSupported` を `GraphicsCaps` へ分解
- 現状の単一 bool が実際は (a) コンピュート、(b) テクスチャ UAV、(c) HDR RT＋ポストプロセス、の3つを兼ねている（FL10 でたまたま連動）。道B や PC-UWP で「compute はあるが HDR は切る」等の組合せが出ると破綻。
```cpp
struct GraphicsCaps {
    bool computeSupported;  // CS / GPU 駆動
    bool uavSupported;      // テクスチャ UAV
    bool hdrPipeline;       // R16F シーン RT + トーンマップ
};
```
- デバイス初期化時に確定させ、各所は必要なフラグだけ見る。
- 併せて **順序ハザード解消**: 現状 `SetComputeSupported` はグローバル可変で「デバイス初期化前に読むと既定 true を掴む」危険がある。`GraphicsDevice` のメンバに持たせアクセサで引く。

### 9-3. `.vcxitems`（共有アイテムプロジェクト）でソース共有
- `GameUWP.vcxproj` がソース一覧を二重管理している負債を解消。
```
Game.Shared.vcxitems  ← .cpp/.h/.fx とフィルタはここだけ
Game.vcxproj          ← デスクトップ。Shared を Import
GameUWP.vcxproj       ← UWP。同じ Shared を Import
```
- ソース内の切り分けは現行の `#if defined(AQ_PLATFORM_UWP)` 空 TU 方式のまま。共通ビルド設定は `Common.props` に括り出して両 vcxproj から Import。
- 「ifdef で切替えたい」願望はソースレベルでは既に実現済みで、足りなかったのは**プロジェクトファイルの共有機構**だけ、という整理。

### 9-4. `aq.h` の DirectXTex 分岐を TextureLoader の TU へ閉じる
- `aq.h` はエンジン全域が読むコアヘッダ。サードパーティヘッダ（DirectXTex）の分岐は上位に漏らさず、使用する TU（`Resource.cpp` / TextureLoader）に閉じる。ビルド時間にも効く。
- ※ これは今回の変更以前からの既存負債（元々 `#include <DirectXTex\DirectXTex.h>` が aq.h に出ていた）。

### 9-5. DebugXbox の DirectXTex リンク（§8 参照）
- `<NuGetConfiguration>Debug</NuGetConfiguration>` を DebugXbox に明示（構成名変更や .targets 改変より無副作用）。

---

## 10. コミット履歴（`feature/xbox-uwp-phase3`、抜粋・新しい順）
- `ce4f152` 実機 Xbox One(UWP/FL10.1) でモデル描画到達 — D3D11 + FL10 フォールバック
- `b98687a` appx 内でソースツリー相対構造を再現（シェーダ #include 解決）
- `9fc357d` ゲームアセットを appx 同梱 + UWP パス解決を install 基点に
- `287d51a` CppWinRT 2.0.210312.4 の反映漏れ整合
- `13161c9` D3D12Shader のパス解決を GetContentRoot 経由に
- `46db82c` マニフェストに mp:PhoneIdentity（APPX1673）
- `652d610` winmd 生成を無効化（APPX0702）
- `26003f2` UWP で DirectXTex 切り離し（LNK2038）
- `8f99f06` CppWinRT を 2.0.210312.4 に統一
- `66768bf` /permissive- 無効化（ICE 回避）
- `9d55cf1` Xbox 構成を VS2019(v142/SDK22621) 向けに調整
- `009d925` TargetPlatformVersion を明示固定（MSB8036）
- `1233f81` UWP の CRT を DLL 版に（MSB8024）
- `829fa4e` UWP アプリを別プロジェクト GameUWP に分離
- （Phase 0: `8272e7d`/`71f1bf7`/`f25ab7a` は main にマージ済み）

### 未コミット（本セッションのテクスチャ対応・作業ツリー）
- `aq.h`（DirectXTex NuGet 分岐）, `Resource.cpp`（TextureLoader ガード解除）
- `Game/packages.config` / `aqEngine/packages.config`（directxtex_uwp 追加）
- `GameUWP.vcxproj`（directxtex_uwp targets import + 署名設定）
- `GameUWP_TemporaryKey.pfx`（**秘密鍵。コミットしない**）
