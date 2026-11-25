# aqEngine エディタ E10: Mac のエディタ

> 対象コミット: d993cda / 最終更新: 2026-11-25(エディタ全体設計.md の旧 10.1 節と 11.3 節の該当フェーズを分割して移した。本文は分割前のまま。各節の照合コミットは節の「現状」に記載)
> 全体の構成・到達点・フェーズ一覧・未決事項は [エディタ全体設計](../エディタ全体設計.md)。節番号(4.4 など)は分割前の番号のまま残している。

## 10.1 E10 の詳細(2026-11-25 追記)

Mac 機で進める。E9 の Windows 機での確認は後回しにした(ユーザー決定 2026-11-25)。E10 は Mac(Metal)だけを対象にし、Mac の Vulkan(MoltenVK)・iOS・Android のエディタは作らない(10 章の表のとおり)。

### 現状(調査結果。d993cda で確認)

| 項目 | 実コード |
|---|---|
| エンジンの共通部分 | 済み。`AppHost` が `#if AQ_EDITOR` で 2 つの ImGui コンテキスト(フォントアトラス共有)を作り、エディタ用にだけプラットフォームの入力(`platform->ImGuiInit`)を繋ぐ(`GAME_IMGUI_USES_PLATFORM_BACKEND`)。`Render()` はゲーム用の描画コマンドを作った後にエディタ用で `OnBuildUI(displayRT)` を呼び、`RenderThread` は `editorDrawData` があれば `CopyToBackBuffer` の代わりに `RenderEditorOverlay` を呼ぶ。`IEditorLayer` は `OnBeginFrame(gameContext)` / `OnBuildUI(gameViewRT)` / `OnEndFrame()`(5.2 節の初版の形とは違う。実コードが正) |
| エディタ用のグラフィックスの口 | `IGraphicsDeviceImpl` の `ImGuiInitEditor`(既定 false)・`ImGuiShutdownEditor` / `ImGuiNewFrameEditor`(既定は何もしない)・`RenderEditorOverlay`(既定は何もしない)・`GetImGuiTextureId`(既定 nullptr)・`SupportsSeparatePresentSize`(既定 false)・`ResizePresentSurface` / `ResizeRenderTargets`(既定 false)。**実装は D3D12 だけ**。Metal で `AQ_EDITOR` にすると `ImGuiInitEditor` が false で `AppHost` の初期化が assert する |
| Metal の ImGui | 公式 `imgui_impl_metal`(`MetalImGui.mm`)。初期化済みの印は `g_initialized` 1 つ。`NewFrame` は何もせず、レンダースレッドの `Render(cmdBuf, encoder, passDesc, drawData)` が `ImGui_ImplMetal_NewFrame(passDesc)` と `RenderDrawData` をまとめて呼ぶ。公式のデータは current コンテキストの `io.BackendRendererUserData` にあるので、コンテキストごとに `ImGui_ImplMetal_Init` すれば 2 つ持てる |
| ゲーム用 ImGui の描画先 | Metal の `ImGuiRender(drawData)` は描画データを控えるだけで、`CopyToBackBuffer` の末尾で drawable に重ねて描く。**エディタ構成では `CopyToBackBuffer` が呼ばれないので、ゲーム用 ImGui(デバッグ UI)はどこにも描かれない**。D3D12 は `ImGuiRender` でゲーム用コンテキストへ切り替え、そのとき束ねている RT(displayRT)へ描く |
| RT のテクスチャ | `MetalRenderTarget` のカラーは `RenderTarget | ShaderRead | ShaderWrite`。Metal の `ImTextureID` は `id<MTLTexture>`。D3D12 の `GetImGuiTextureId` はアルファを 1 として読む SRV を登録する(displayRT のアルファが 1 でないと透けるため) |
| リサイズ | Metal は `SupportsRenderResize` が true で、`ResizeMainTargets` がメインの RT と `layer.drawableSize` をまとめて作り直す。提示面だけ・RT だけを作り直す口(E3b-2)は無い。`Engine` 側の分ける制御(`DetectSeparateRenderResize` など)は共通で済んでいる |
| Mac の入力 | 公式 `imgui_impl_osx`(`MacImGui.mm`。memory の「自前バックエンド」は E1b-2 より前の記述)。イベントモニタは**イベントが来た時点で current なコンテキスト**へ入れる。`FlushRender` の末尾でエディタ用を current にし、次のフレームの先頭の `PumpEvents` で入力が入るので、Win32 と同じくエディタ用に入る見込み。`IPlatform::IsAppActive` / `HasInputFocus` は Mac では既定(常に true) |
| Editor のコード | Windows 専用は `EditorMain.cpp`(`AQ_PLATFORM_WIN32` で全体を囲む。Mac では空になる)だけ。ほかは Win32 / D3D12 の型を使わない。12 本が `stdafx.h`(`Game/Application`)を include する |
| CMake | `AQ_EDITOR` の定義もエディタのターゲットも無い。`aqEngine` は `AQ_DEBUG_IMGUI` を Debug だけで PUBLIC に定義する。`Game` は `Game/Application` を再帰で集めて 1 つの実行ファイルにする(ゲームロジックのライブラリは無い。エントリ `Main.cpp` / `MacMain.mm` / `iOSMain.mm` / `AndroidMain.cpp` はプラットフォームのガードで空になる)。シェーダの生成(`aqCompileMsl`)は `add_dependencies(Game ...)` にぶら下がる。.app は `MACOSX_BUNDLE_GUI_IDENTIFIER` が空だとキー入力が届かない |
| Multi-Viewport | 同梱の ImGui 1.93 の公式 osx / metal は両方とも対応している(`PlatformHasViewports` / `RendererHasViewports`)。エンジンはエディタ用に `ViewportsEnable` を立てていない |

### 決定

E10 を 2 段に分ける。**E10a: ビルドと表示**(Editor が Mac で起動し、エディタの UI とゲームビューが出る)→ **E10b: リサイズと E9 までの機能の確認**。

**1. CMake のエディタ構成(E10a)**

- ルートに `option(AQ_BUILD_EDITOR "..." OFF)`。**ON にできるのは Apple(iOS 以外)かつ `AQ_GRAPHICS_API=Metal` のときだけ**(それ以外は `FATAL_ERROR`。Windows のエディタは今までどおり VS で作る)。OFF のときの CMake の出力は今と同じにする。
- ON のツリーは**エディタ専用**にする(3.1 節「静的ライブラリの中の `#if` はそのライブラリのビルドで決まる」の CMake 版。Windows の Editor_Debug / Editor_Release が別の中間フォルダを持つのと同じ考え)。
  - `aqEngine` に `AQ_EDITOR=1` と全構成の `AQ_DEBUG_IMGUI` を PUBLIC で足す(`AppHost` のメンバが `#if AQ_EDITOR` で変わるので、ゲームロジックと Editor にも同じ値が伝わらないといけない)。
  - `Game/CMakeLists.txt` は、ON なら実行ファイルの代わりに静的ライブラリ `GameLogic` を作る。ソースは同じ集め方から `Main.cpp` / `MacMain.mm` / `iOSMain.mm` / `AndroidMain.cpp` を除いたもの。PCH(`stdafx.h`)・定義・include は今の `Game` と同じ。**`aqEngine` へは PUBLIC でリンクする**(今の `Game` は PRIVATE。PRIVATE のままだと `AQ_EDITOR`・`AQ_IMGUI`・`AQ_PLATFORM_*`・エンジンの include が Editor へ伝わらない)。`Game/Application` と `Game` も PUBLIC の include にする(Editor の `stdafx.h` のため)。
  - Sample と Tools(MixerTest / ReflectionTest)は ON のツリーでは作らない(Game と Sample は `AQ_EDITOR=1` では意味が無く、ReflectionTest は通常のツリーで回す)。
  - 新しい `Editor/CMakeLists.txt` が実行ファイル `Editor`(`MACOSX_BUNDLE`)を作る。ソースは `Editor/` の `.cpp` / `.mm` を再帰で集める(`EditorMain.cpp` は Mac では空)。`GameLogic` にリンクする。`MACOSX_BUNDLE_GUI_IDENTIFIER = com.aqengine.editor`、`MACOSX_BUNDLE_BUNDLE_NAME = aqEditor`、Xcode の作業ディレクトリは `Game/`(Game と同じ)。
  - `aqCompileMsl` を `Editor` にもぶら下げる(ON のツリーには `Game` の実行ファイルが無いので、`add_dependencies` の対象を切り替える)。
  - .app の同梱(`aqBundleApp` 相当)は作らない。起動は `Game/` を作業ディレクトリにして `.app/Contents/MacOS/Editor` を直接実行する(Game の開発時の起動と同じ)。
- プリセット `macos-ninja-metal-editor` / `macos-xcode-metal-editor`(と build プリセットの `-debug` / `-release`)を足す。`binaryDir` は `build/<プリセット名>`。
- Editor のファイルを Mac 用に足しても `Editor.vcxproj` には登録しない(Mac移植設計の `DebugOutputMac.cpp` と同じ扱い)。

**2. Mac のエントリ(E10a。`Editor/EditorMacMain.mm`、新規)**

- `Game/Application/MacMain.mm` と同じ流れ(`NSApplication` の用意、`PlatformMac`、`LaunchOptions::Parse(argc, argv)`、`Engine::Create` → `Initialize` → `RunGame` → `Finalize` → `Release` → `ShutdownMemory`)に、`EditorMain.cpp` のエディタの部分(`ConsoleLog` の Attach / Detach、`EditorLayer` の生成、`SetEditorLayer`、`LevelEditorPanel::SetHostedByEditor(true)`、`-editor-selftest` の判定を argv で)を足す。
- .app からの起動で作業ディレクトリを移す処理(`SetupBundleEnvironment`)は持たない(同梱しないため)。作業ディレクトリに `Assets` が無ければ、エラーを出して終わる。
- `EditorMain.cpp` と重なる部分(エディタの部品の組み立て)を共通の関数に切り出すのはしない(Windows 機で確かめられないので、`EditorMain.cpp` は触らない)。重なりはこの節に記録し、Windows 機で確認できるときに切り出しを検討する。

**3. Metal のエディタ用の口(E10a。エンジン)**

- `MetalImGui` に `InitEditor(gameCtx, editorCtx)` / `ShutdownEditor()` を足す。エディタ用コンテキストを current にして `ImGui_ImplMetal_Init(device)`、印は `g_editorInitialized`。`ShutdownEditor` はエディタ用を current にして `ImGui_ImplMetal_Shutdown`。共有のフォントアトラスのテクスチャの破棄は公式の参照数に任せる(`AppHost` がエディタ用を先に畳む。D3D12 と同じ前提)。
- `MetalGraphicsDeviceImpl` に `ImGuiInitEditor` / `ImGuiShutdownEditor` / `ImGuiNewFrameEditor`(何もしない。公式の NewFrame はレンダースレッドで呼ぶ)を実装し、ゲーム用・エディタ用のコンテキストを控える(`gameImGuiCtx_` / `editorImGuiCtx_`。D3D12 と同じ名前)。
- **ゲーム用 ImGui を displayRT へ描く**: `ImGuiRenderCommand::Execute` が `FrameContext` の displayRT を `GraphicsDevice::SetImGuiRenderTarget(IRenderTarget*)`(新規。`IGraphicsDeviceImpl` の既定は何もしない。D3D11 / D3D12 / Vulkan は変えない)で渡してから `ImGuiRender` を呼ぶ。Metal は `gameImGuiCtx_` があるとき(エディタ構成)だけ、`ImGuiRender` で**保留中のクリアを確定してから**開いているエンコーダを閉じ(`FlushPendingClears` → `EndEncodingIfActive`。`CopyToBackBuffer` と同じ順。閉じるだけではクリアの予約が残り、描いた後で消される)、displayRT のテクスチャへ `loadAction = Load` のパスを開いて、ゲーム用コンテキストを current にして `MetalImGui::Render` で描き、current を戻す。無いとき(Game)は今までどおり控えて `CopyToBackBuffer` の末尾で描く。D3D12 のように「そのとき束ねている RT」へ描かないのは、Metal の描画コンテキストの束ねている RT を外から読む口が無く、`FrameContext` に displayRT があるのでそれを明示的に渡すほうが確かなため。
- `RenderEditorOverlay(editorDrawData)`: 保留中のクリアを確定して開いているエンコーダを閉じ(上と同じ順)、drawable を黒でクリアするパスを開いて、エディタ用コンテキストを current にして `MetalImGui::Render` で描く(`CopyToBackBuffer` の ImGui の部分と同じ形。ゲーム用の控えは描かない)。
- `GetImGuiTextureId(rt)`: カラーのテクスチャの**アルファを 1 にするスウィズル(R, G, B, One)のビュー**を作って返す(D3D12 の「アルファを 1 として読む SRV」と同じ目的)。ビューはテクスチャごとに 1 つ控え、RT を作り直したとき(`ResizeRenderTargets` / `ResizeMainTargets` / `ReleaseOffscreenRenderTarget`)に捨てる。スウィズルのビューに `MTLTextureUsagePixelFormatView` が要ると Metal の API Validation が言うなら、RT のカラーの usage に足す(実装のときに Validation を有効にして確かめる。memory の「Validation は既定オフ」に注意)。ビューを作れなければ **nullptr を返す**(元のテクスチャを返すとアルファで透け、「アルファを 1 として読む」契約を破る。nullptr なら `EditorLayer` が既存の「not supported」の表示を出す)。ログを 1 回出す。
- `IsAppActive` を `PlatformMac` に実装する(`NSApp.isActive`)。`GameViewInput` がアプリが裏に回ったときにゲームのキーを離すのに使う。`HasInputFocus` は既定のまま(Mac には埋め込み起動が無い)。

**4. リサイズ(E10b。エンジン)**

- `MetalGraphicsDeviceImpl` の `ResizeMainTargets` を、提示面(`layer.drawableSize`・`width_` / `height_`・`swapchainRT_` の作り直し)の `ResizePresentSurface` と、メインの RT の `ResizeRenderTargets` に分け、`SupportsSeparatePresentSize` を true にする。`ResizeMainTargets` は 2 つを順に呼ぶ形に残す(Game の経路は変えない)。契約は `IGraphicsDeviceImpl` のとおり(RT の作り直しの前に GPU の完了を待つ、失敗したら false)。加えて、今の Metal の `ResizeMainTargets` が持つ「新しい RT を作ってから入れ替えるので、失敗しても古い RT が残る」性質を `ResizeRenderTargets` でも保つ(これは Metal 固有の保証で、D3D12 の実装と抽象の契約は失敗時の状態を保証しない)。
- これで `Engine::SetRenderSizeOverride` が効き、ゲームビューの大きさに描画解像度が追従する(E3b と同じ)。

**5. Multi-Viewport と入力(E10a で確かめ、10 章に記録)**

- Mac も **Docking だけ**にし、`ViewportsEnable` は立てない。公式のバックエンドは対応しているが、Windows の E3a(5.10 節)を保留したのと同じ理由(ゲーム用コンテキストの描画とテクスチャの扱いが 1 ウィンドウを前提にしている)で、E3a を再開するときに Windows と一緒に扱う。
- 2 コンテキストの入力: 2 フレーム目からは `FlushRender` の末尾でエディタ用が current になるので、次の `PumpEvents` の入力はエディタ用に入る。しかし**最初の `PumpEvents` は、`AppHost` の初期化がゲーム用を current にして終わるので、ゲーム用に入ってしまう**(公式 osx は current のコンテキストへ入れる)。そこで `AppHost::SetEditorLayer` で、エディタ用のコンテキストを current にする(`SetEditorLayer` は `Initialize` の後・`RunGame` の前に 1 回呼ばれる。フレームの中は `Update` がゲーム用へ明示的に切り替えるので、ほかの経路は変わらない)。Windows も同じ順なので同じ変更が効くが、Win32 のメッセージは `FlushRender` の後にしか来ないので挙動は変わらない。
- **修飾キーの二重の入れ替え**: Apple では ImGui の `ConfigMacOSXBehaviors` が既定で true で、`AddKeyEvent` が Command と Ctrl を入れ替えて内部のイベントの列に積む。`GameViewInput` はエディタ用の入れ替え済みの列をゲーム用の `AddKeyEvent` へ入れ直すので、ゲーム用でもう一度入れ替わる(ゲームのデバッグ UI では Command と Ctrl が逆になる)。`GameViewInput` で、エディタ用の `io.ConfigMacOSXBehaviors` が true のとき、`ImGuiMod_Ctrl` / `ImGuiMod_Super` と左右の Ctrl / Super のキーを入れ替え直してから転送する(Windows は false なので変わらない)。E10a の自動操作で、ゲーム用の `ImGui > Metrics` の Inputs で Command を押したときに Super(ImGui の入れ替えの後は Ctrl)として見えることを、エディタ用と比べて確かめる。
- E10a の自動操作で、エディタの UI のクリック・ゲームビューの中のクリック(`GameViewInput` がゲーム用へ転送)・キー入力を確かめ、15 章の TODO を閉じる。
- Mac の ImGui フォントの日本語(15 章の「任意」)は扱わない。エディタの UI は英語なので困らない。

### 責務

| ファイル | 新規 / 変更 | 責務 | 段 |
|---|---|---|---|
| `CMakeLists.txt`(ルート) | 変更 | `AQ_BUILD_EDITOR`、ON のときのサブディレクトリとシェーダの依存の切り替え | E10a |
| `CMakePresets.json` | 変更 | `macos-ninja-metal-editor` / `macos-xcode-metal-editor` と build プリセット | E10a |
| `aqEngine/CMakeLists.txt` | 変更 | ON のとき `AQ_EDITOR=1` と全構成の `AQ_DEBUG_IMGUI` | E10a |
| `Game/CMakeLists.txt` | 変更 | ON のとき静的ライブラリ `GameLogic` | E10a |
| `Editor/CMakeLists.txt` | 新規 | 実行ファイル `Editor`(.app) | E10a |
| `Editor/EditorMacMain.mm` | 新規 | Mac のエントリ | E10a |
| `aqEngine/Graphics/Metal/MetalImGui.h/.mm` | 変更 | `InitEditor` / `ShutdownEditor` | E10a |
| `aqEngine/Graphics/Metal/MetalGraphicsDeviceImpl.h/.mm` | 変更 | エディタ用の口、ゲーム用 ImGui の displayRT への描画、`RenderEditorOverlay`、`GetImGuiTextureId`(E10a)。提示面と RT の分割(E10b) | E10a / E10b |
| `aqEngine/Graphics/IGraphicsDeviceImpl.h`、`GraphicsDevice.h/.cpp` | 変更 | `SetImGuiRenderTarget`(既定は何もしない) | E10a |
| `aqEngine/Rendering/ImGuiRenderCommand.cpp` | 変更 | displayRT を渡してから描く | E10a |
| `aqEngine/Platform/Mac/PlatformMac.h/.mm` | 変更 | `IsAppActive` | E10a |
| `aqEngine/Core/AppHost.h/.cpp` | 変更 | `SetEditorLayer`(今はヘッダのインライン)でエディタ用のコンテキストを current にする | E10a |
| `Editor/GameViewInput.cpp` | 変更 | 修飾キーの入れ替え直し(`ConfigMacOSXBehaviors` のとき) | E10a |
| `Tools/SetupCMake/README.md` | 変更 | エディタのビルドと起動の手順 | E10a |
| VS のプロジェクト | 変更なし | Mac 専用のファイルは登録しない。エンジンと Editor の変更は既存ファイルだけ(`AppHost` と `GameViewInput.cpp` は Windows でもコンパイルされるので、Windows 機の確認のときにビルドする) | — |

### このフェーズで作らないもの

- Mac の Vulkan・iOS・Android・Windows の CMake のエディタ。
- エディタの .app の同梱(Finder から起動できる形)。
- Multi-Viewport(E3a と一緒に)。
- `EditorMain.cpp` と `EditorMacMain.mm` の共通部分の切り出し(Windows 機で確かめられるときに)。
- OS のファイルダイアログ(10 章のプラットフォーム抽象。必要になったら別に設計する)。

### テスト方針

手順の一次資料: 起動・撮影・合成入力・終了は 4.5 節「Mac での確かめ方」(作業ディレクトリを `DirectX/Game` にして `.app/Contents/MacOS/<名前>` を直接起動、`screencapture -x -o -l<ウィンドウ ID>`、`CGEventPost`、閉じるボタンで終了、`imgui.ini` を戻す)。エディタは加えて `editor_layout.ini` も作業ディレクトリに書くので、試験の前の状態に戻す。**Metal の API Validation** は環境変数 `MTL_DEBUG_LAYER=1` を付けて起動して有効にし(既定ではオフ)、標準エラーと `startup_timing.log` に `-[MTLDebug` の行や `failed assertion` が無いことを見る。

| 段 | 自動で確認する(Claude) | ユーザーに確認する |
|---|---|---|
| E10a | 通常のツリー(`macos-ninja-metal`)で Game・Sample・ReflectionTest の Debug / Release がビルドでき、警告が増えず、T1〜T15 の件数が変わらない(Game の経路を変えていないこと)。Game.app を起動して FPS 表示とデバッグ UI が今までどおり出る(撮影)。エディタのツリー(`macos-ninja-metal-editor`)で Editor の Debug / Release がビルドできる。Metal の API Validation を有効にして Editor を起動し、Validation のエラーが無い・`startup_timing.log` に `[crash]` が無い。`-editor-selftest` が Debug / Release で PASS。撮影で、メニューバー・Hierarchy・Inspector・Console・Content Browser・Game View が出て、Game View にゲームの画(アルファで透けない)とゲーム用のデバッグ UI が出る。合成入力(1 手ずつ撮影)で、Hierarchy のノードを選ぶとインスペクターが変わる、Game View の中のクリックがゲーム用の UI に届く、`Ctrl+Z`(Mac では Command)で Undo のログが出る | エディタの操作感(クリック・キー・スクロール・ドラッグ)。アプリを裏に回したときにゲームのキーが押しっぱなしにならない |
| E10b | エディタのツリーと通常のツリーのビルド・警告・T1〜T15。ウィンドウの大きさを 10 回以上変え、Game View の大きさに描画解像度が追従する(ログの解像度と撮影)・最小化から戻せる・Validation のエラーが無い。Game.app でウィンドウの大きさを変えても今までどおり(撮影)。E9 のテスト方針(7.10 節)の自動操作を Mac で行う(パス欄へのドロップと Undo、Prefab の作成と配置、Prefab エディタで開く) | E9 の操作感(Mac で。7.10 節のユーザー確認)。リサイズの見た目 |

### E10a の結果(2026-11-25、Mac 機。実装とビルドまで)

- ユーザー決定で、動作確認(起動・撮影・合成入力・Validation での起動・自己テスト)とユーザー確認は後回しにした。**確かめたのはビルドだけ**: 通常のツリー(`macos-ninja-metal`)で Game・Sample・ReflectionTest の Debug / Release が通り、警告は既存のもの(`AppHost.h` の `editorImGuiContext_` の unused-private-field は元から)。ReflectionTest は Debug 413/413・Release 171/171。再構成の後にコンパイルし直しが起きないこと(OFF のときに定義が変わらないこと)を確かめた。エディタのツリー(`macos-ninja-metal-editor`)で Editor.app の Debug / Release がビルドとリンクまで通る。Xcode のジェネレータ(`macos-xcode-metal-editor`)は未確認。
- 実装で決めたこと:
  - CMake: ON のときは `Game/CMakeLists.txt` が `GameLogic` を作った直後に `return()` し、.app の設定と `aqBundleApp` は作らない(`Game` ターゲットが無いため)。`Editor` の include に `Editor/` を足した。プリセットは configure に `macos-ninja-metal-editor` / `macos-xcode-metal-editor`、build にそれぞれの `-debug` / `-release`。手順は `Tools/SetupCMake/README.md` の 5.2.0a。
  - `EditorMacMain.mm`: 作業ディレクトリに `Assets` が無ければ標準エラーに出して終了コード 1。
  - Metal: `MetalImGui::Render` は、current のコンテキストに公式バックエンドが繋がっていなければ(`io.BackendRendererUserData` が null)描かずに戻る。`ImGuiRender` / `RenderEditorOverlay` は `BeginFrameIfNeeded` の後に `frameOpen_` を見て、drawable を取れず捨てたフレームでは描かない。描いた後は current を呼ぶ前のものへ戻す。
  - `GetImGuiTextureId` のビュー: **`MTLTextureUsagePixelFormatView` は要らなかった**(同じフォーマットのスウィズルのビューは、`MTL_DEBUG_LAYER=1` の小さな試験プログラムでフラグ無しでも assert せず、alpha = One になった。フォーマットを変えるビューではフラグ無しで assert するので、Validation は効いている。`MetalDepthMap.mm` も同じフォーマットのビューをフラグ無しで作っている)。ビューの表は `MetalDeviceObjects::imguiTextureViews`(元のテクスチャは比較だけで retain しない。ビューが親を保持するので番地は再利用されない)。捨てるのは `ResizeMainTargets`(入れ替えの前)・`ReleaseOffscreenRenderTarget`・`Finalize`。メインスレッドだけが触る。プロキシの RT(drawable)は対象外で nullptr。**E10b で `ResizeRenderTargets` を足すときも、入れ替えの前にビューを捨てる**。
  - `SetEditorLayer` はヘッダのインラインから .cpp へ移した。
  - `GameViewInput`: 押したかどうかの控え(`keySent_`)は入れ替え直した後のキーで付ける。
- 未確認(後回し): テスト方針の E10a の自動確認とユーザー確認のすべて。

---

## 評価チェックリスト(旧 11.3 節)

### E10 Mac のエディタ

- [ ] CMake にエディタ構成が入り、Mac で Editor がビルドできる
- [ ] Mac で E9 までの機能が動く(ユーザー確認)。Multi-Viewport の可否を本書 10 章に記録した
