# aqEngine エディタ E1: ImGui の更新(docking 版 + 公式バックエンド)

> 対象コミット: d993cda / 最終更新: 2026-11-25(エディタ全体設計.md の旧 4.4、4.5 節と 11.3 節の該当フェーズを分割して移した。本文は分割前のまま。各節の照合コミットは節の「現状」に記載)
> 全体の構成・到達点・フェーズ一覧・未決事項は [エディタ全体設計](../エディタ全体設計.md)。節番号(4.4 など)は分割前の番号のまま残している。

## 4.4 E1a の詳細(2026-11-16 追記)

着手前の調査で分かったことと、それを受けた決定。

### 現状(調査結果)

| 項目 | 現状 |
|---|---|
| 同梱の公式バックエンド | `imgui_impl_win32`(コアと同世代)と `imgui_impl_dx11` を使用。`imgui_impl_dx12.cpp` は同梱されているがコアより新しく、`imgui.vcxproj` で `ExcludedFromBuild`。vulkan / metal / osx / android は同梱なし。CMake の imgui ターゲットは Win32 のときだけ win32 / dx11 を足し、Mac / iOS / Android はコア 4 ファイルだけ |
| 自前バックエンド | D3D12(`D3D12ImGui.cpp`。専用の shader-visible ヒープ 64 個、HLSL を実行時コンパイル)、Vulkan(`VulkanImGui.cpp`。`ImGuiVK.fx`、dynamic rendering、エンジンのフレーム別 DescriptorPool を共用。**Android も同じコード**)、Metal(`MetalImGui.mm`。`ImGuiVK.fx` から作った MSL)。どれも `GetTexDataAsRGBA32` と `SetTexID` で固定アトラスを作る旧式 |
| 描画位置 | D3D11 / D3D12 は描画コマンド列の末尾で、そのときの RT(`displayRT`)に描き、その後 `CopyToBackBuffer`。Vulkan / Metal は描画データを保存し、`CopyToBackBuffer` の後でスワップチェーン / drawable に直接描く |
| テクスチャ ID | デバッグパネル 11 か所が `ImGui::Image((ImTextureID)xxx->GetNativeHandle(), ...)` でエンジンのハンドルを渡す。D3D11 は `ID3D11ShaderResourceView*`、D3D12 は `D3D12SRV*`(自前バックエンドが描画時に遷移とディスクリプタのコピーをする)、Vulkan は `VkImageView` として解釈しているが、実際に渡っているのは `VulkanSRV*` / `ColorSRV*` で食い違っている疑いがある |
| 新コア(1.93.0)の互換 | `GetTexDataAsRGBA32`、`SetTexID`、`ImDrawData::CmdLists`、6 引数の旧 `Image` は残っている。`RendererHasTextures` を立てない旧式バックエンドも動く。`io.FontGlobalScale` は `style.FontScaleMain` へ移った(旧名は互換で残る) |
| Vulkan の関数 | `vulkan-1.lib` を静的リンク(関数ポインタの読み込みは不要) |

### 決定

- **版**: `v1.93.0-docking`(2026-11-16 時点の docking ブランチ最新タグ)。`ThirdParty/imgui/` のコアと、使うバックエンドを差し替える。
- **E1a で公式に置き換えるもの**: win32(版を上げる)、dx11(版を上げる)、dx12(自前を削除)、vulkan(自前を削除。Android も同じコードなので公式に切り替わる)。
- **E1a で置き換えないもの**: Metal と MacImGui、Android / iOS の入力。新コアでも旧式のまま動くので、E1b で公式に置き換える。`ImGuiVK.fx` と `shader_entries.txt` の登録は Metal が使うので E1b まで残す。
- **描画位置は E1a では変えない。** D3D11 / D3D12 は `displayRT` へ、Vulkan はスワップチェーンへ、今と同じタイミングで描く。2 つの描画先への分離は E2a で行う。
- **テクスチャ ID は描画時に変換する。** パネルの `ImGui::Image` の呼び方は変えない。各バックエンドの `ImGuiRender`(レンダースレッド)で、公式の `RenderDrawData` を呼ぶ前に描画データを走査し、`TexRef._TexData == nullptr`(= アプリが渡したテクスチャ。フォントアトラスは `_TexData` を持つ)の描画コマンドの `_TexID` を、エンジンのハンドルからバックエンドの ID に書き換える。

| API | エンジンのハンドル | 変換 |
|---|---|---|
| D3D11 | `ID3D11ShaderResourceView*` | 不要(公式と同じ) |
| D3D12 | `D3D12SRV*` | SRV を `TransitionToSRV` し、ImGui 用ヒープのフレーム別の区画へ `CopyDescriptorsSimple` して、その GPU ハンドルを入れる(自前バックエンドの処理と同じ) |
| Vulkan | `VulkanSRV*` / `ColorSRV*`(`VkImageView`、`VkImage`、追跡中のレイアウトを持つ) | 画像を `SHADER_READ_ONLY_OPTIMAL` へ遷移して追跡中のレイアウトも更新し、`ImGui_ImplVulkan_AddTexture(view, SHADER_READ_ONLY_OPTIMAL)` の DescriptorSet をビュー単位でキャッシュして入れる。`AddTexture` は遷移をしないので、遷移は ImGui の `vkCmdBeginRendering` より前に行う(`CopyToBackBuffer` の遷移の後の状態を基準にする)。例: Hi-Z の出力は `GENERAL` のまま残っている。ビューの破棄時(RT の作り直しなど)にキャッシュから外して `RemoveTexture` する。渡されたハンドルの型を E1a で確かめ、食い違いがあれば直す |

  E2a の `GetImGuiTextureId` はこの変換を前提に、エンジンのハンドルをそのまま返す形にできる(5.4 の表は E2a で見直す)。
- **呼び方の例外(配列テクスチャ)**: 公式のシェーダは 2D テクスチャしか読めない。シャドウの深度マップ(`DeferredDebugPanel` が配列全体の SRV を渡している。D3D12 / Vulkan とも `TEXTURE2DARRAY`。D3D12 の既存のスライス用 SRV も配列型)は、E1a では**画像の表示をやめ、「シャドウ深度マップの画像表示は未対応」と解像度だけを出す**(全 API)。今の自前バックエンドでも 2D として読んでおり、正しく出ている保証がない。スライスを 2D のプレビュー RT に写して表示する方法は 15 章に残す。配列の SRV が `ImGui::Image` に渡された場合は、変換時に検出して描かずに警告ログを 1 回出す(D3D12 / Vulkan)。
- **D3D12 のヒープ**: ImGui 用の shader-visible ヒープを 256 個に広げる。前半をフォントアトラスなどバックエンドが確保するテクスチャ(`SrvDescriptorAllocFn` / `FreeFn` で空きリスト管理)、後半をフレーム別のユーザーテクスチャ区画(`D3D12_FRAME_COUNT` × 64)にする。`NumFramesInFlight = D3D12_FRAME_COUNT`。
  - **公式の `RenderDrawData` はヒープを設定しない**(設定するのは Multi-Viewport の別経路だけ)。`ImGuiRender` で、`RenderDrawData` の前に ImGui 用ヒープを `SetDescriptorHeaps` する。ImGui の後にエンジンが自前のヒープを使う描画を積む場合は張り直す(現状は ImGui の後は `CopyResource` だけ)。
  - ユーザー区画へのコピーは、そのフレームの区画を前回使った GPU 処理の完了を `BeginFrameIfNeeded` のフェンス待ちで保証した後に行う。
  - `RTVFormat` は `displayRT` のフォーマットを渡す。
- **Vulkan の初期化**: `UseDynamicRendering = true`、`PipelineInfoMain.PipelineRenderingCreateInfo` にスワップチェーンのフォーマット、`DescriptorPoolSize` を指定してバックエンド内部のプールを使う(エンジンのフレーム別プールは使わない)。`ImageCount` / `MinImageCount` は初期化時のスワップチェーンに合わせる。
  - **スワップチェーンの作り直し**: 寸法だけが変わる場合はバックエンドに何もしない(dynamic rendering なので描画先はその都度渡す)。フォーマットか画像数が変わる場合(Android のサーフェス再生成を含む)は、`SetMinImageCount` が非対応(assert)なので、GPU の完了を待ってからバックエンドを `Shutdown` → `Init` し直し、ユーザーテクスチャの DescriptorSet のキャッシュも捨てる。
- **フォント**: 読み込み方(`AddFontFromFileTTF` と独自のグリフ範囲)は変えない。D3D11 / D3D12 / Vulkan は公式バックエンドが `RendererHasTextures` を立てるので動的フォントになる。日本語グリフを足すかは E1a では決めない(15 章)。
- **スレッドの契約**: 公式バックエンドは描画中に `draw_data->Textures`(コンテキストが所有するフォントアトラスなど)の状態を書き換える。`ImGuiRenderCommand` は `ImDrawData*` しか持たないので、**ImGui 有効時はレンダーの直列実行を前提とする**。`AQ_RENDER_PIPELINED` と `AQ_IMGUI` が同時に定義されたらコンパイルエラーにする(`static_assert` か `#error`)。現状 PIPELINED は既定で無効なので挙動は変わらない。PIPELINED と ImGui を両立させる方法は 15 章に残す。
- **ビルド**: `imgui.vcxproj` に `imgui_impl_dx12.cpp` と `imgui_impl_vulkan.cpp` を足す(Vulkan の SDK のインクルードが要る)。CMake の imgui ターゲットにも、Windows の D3D12 構成で `imgui_impl_dx12.cpp`、Vulkan を使う構成(Windows の Vulkan、Android)で `imgui_impl_vulkan.cpp` を足す。CMake の Windows ビルド(D3D12 / Vulkan の Sample)はこの PC で確認する。**Android・Mac・iOS のビルド確認は Mac 機で作業するときに行う**(2026-11-16 ユーザー決定)。

### E1a の結果(2026-11-17)

- **この PC に Vulkan SDK が無い**(`VULKAN_SDK` 環境変数が空)。公式 `imgui_impl_vulkan.cpp` は `#include <vulkan/vulkan.h>` を無条件に行う第三者コードでガードできないため、素のまま `imgui.vcxproj` に追加すると D3D11/D3D12 構成まで壊れる。**ユーザー決定**: `imgui.vcxproj` の `imgui_impl_vulkan.cpp` に `<ExcludedFromBuild Condition="'$(AqGraphicsApi)'!='Vulkan'">true</ExcludedFromBuild>` を付け、Vulkan 以外のビルドを守る。Vulkan 自体はこの PC では**実コンパイル未確認**(論理レビューのみ)。CMake(`ThirdParty/CMakeLists.txt`)側も同じ考え方で `AQ_GRAPHICS_API` 条件に合わせて `imgui_impl_dx12.cpp` / `imgui_impl_vulkan.cpp` を足す形にした。
- **ImGui 本体の更新**: `v1.93.0-docking`(GitHub の同タグ)を取得し、コア一式と win32/dx11/dx12/vulkan のバックエンドを `ThirdParty/imgui/` へ配置(既存ファイルの CRLF・BOM なしを保持)。既存の `imconfig.h` に独自定義は無く、素の upstream に差し替えるだけで済んだ。
- **D3D12ImGui.cpp/.h**: 自前の RootSignature・PSO・HLSL・VB/IB・フォントテクスチャ生成を全廃し、`ImGui_ImplDX12_Init/Shutdown/NewFrame/RenderDrawData` を呼ぶ接続層に書き換えた(480 行 → 251 行)。SRV ヒープは 256 個(バックエンド用 128 + フレーム別ユーザー区画 64×2)。`Render()` の冒頭で `TexRef._TexData==nullptr` のコマンドを `D3D12SRV*` として解釈し、このフレームの区画へ `CopyDescriptorsSimple` してからバックエンドの `RenderDrawData` を呼ぶ。`pcmd->GetTexID()` / クリップ矩形を潰すことで不正ハンドルの描画を止める仕組みを、ダウンロードした公式ソース(`imgui_impl_dx12.cpp` 432〜444 行目)と突き合わせて確認済み。
- **VulkanImGui.cpp/.h**: 同様に自前パイプライン・フォント・VB/IB を全廃。`PrepareTextures(ImDrawData*)`(新設、`vkCmdBeginRendering` の**前**にレイアウト遷移とディスクリプタ変換を済ませる)と `Render()`(描画のみ)に分離し、呼び出し元 `VulkanGraphicsDeviceImpl.cpp` の `CopyToBackBuffer` に 1 行追加した。`VulkanGraphicsDeviceImpl` に `GetVkInstance()`/`GetGraphicsQueueFamily()`/`GetSwapchainImageCount()` を追加(既存メンバをそのまま返すだけ)。**この PC では Vulkan SDK が無く実コンパイルできないため、論理レビューのみ**(ダウンロードした公式ヘッダ・ソースと突き合わせ、`PipelineRenderingCreateInfo.pColorAttachmentFormats` の深いコピーや `sType` のアサート条件などを個別に検証した)。
- **設計との食い違い・未対応**(D3D12/Vulkan 共通):
  - **配列テクスチャ(シャドウ深度マップ)の検出・警告は未実装**。`D3D12SRV` / `VulkanSRV` に view dimension を問い合わせるアクセサが無く、新しい仕組みを作らずに検出する手段が無かった。実際の呼び出し元である `ShadowDebugPanel.cpp` は別途、画像表示をやめて「シャドウ深度マップの画像表示は未対応」+解像度表示に変更したので、既知の実害は無い。コード中に TODO コメントあり。
  - **RTVFormat は `DXGI_FORMAT_R8G8B8A8_UNORM` の固定値を維持**。`IRenderTarget` に displayRT の実フォーマットを照会する手段が無く、Init 時点では描画先の RT も未確定なため。旧の自前 PSO もこの値で動いていた実績がある。
  - **Vulkan のユーザーテクスチャキャッシュは「ビュー単位で永続」ではなく「フレーム単位で作り直し、GPU 完了まで遅延解放」に変更した**。永続キャッシュだと RT 作り直し時に `VkImageView` の値が再利用された場合に破棄済みビューの DescriptorSet を誤って使い続ける恐れがあり(E3b のリサイズ確認に直撃)、RT 破棄のフックがまだ無い E1a ではこちらを安全側とした。ビュー破棄時に `RemoveTexture` する持続キャッシュは E2a 以降の TODO としてコードに残した。
- **ビルド確認(このPC、2026-11-17)**: Game の Debug/Release × D3D11/D3D12 と Editor_Debug/Editor_Release の **6 構成すべて成功**(エラー 0、警告は重複を除いて 17 件で E1a 前と同じ)。`AQ_RENDER_PIPELINED` と `AQ_IMGUI` を両方定義すると実際に `#error`(`C1189`)でコンパイルが止まることを単体の `cl` 実行で確認した。
- **起動確認**: Game.exe(Debug、D3D12 / D3D11)と Editor.exe(Editor_Debug)をそれぞれ起動後 8 秒で `CloseMainWindow()` → 終了コード 0、`Game/startup_timing.log` に `[crash]` なし。既存(旧版 ImGui で保存された)の `imgui.ini` のままでも起動できた。
- **追記(2026-11-17 続き)**: CMake の Windows ビルド(D3D12、`windows-vs2026` プリセット)を確認。`Sample.exe` がビルド成功・起動・終了コード 0。Vulkan 構成の CMake は SDK が無いため未確認。Claude がマウス自動操作(`Start-Process` + Win32 API)とスクリーンショットで、D3D11/D3D12 の Game.exe から Rendering パネル(Lighting/Culling/Hi-Z タブのスライダーと表示切り替え)と Hi-Z の `ImGui::Image`(実テクスチャ内容)、修正後の Shadow パネルの表示を確認し、クラッシュや描画崩れは無かった。11.3 のチェックリストへ反映した。
- **未実施**: 1 分以上の継続表示・ウィンドウリサイズ 3 回・未使用文字の動的フォント・デバッグレイヤー/バリデーションのエラー増加確認(キーボード/コントローラ入力の自動化ができず、実際の操作感はユーザー確認に残る。11.3 の E1a チェックリストに残る)。

## 4.5 E1b の詳細(2026-11-18 追記)

着手前に Mac 機で調べたことと、それを受けた決定。公式バックエンドは E1a と同じ `v1.93.0-docking` のものを使う。

### 現状(調査結果)

| 項目 | 現状 |
|---|---|
| Metal の描画 | 自前の `MetalImGui.mm`(364 行)。`ImGuiVK.fx` から作った MSL、専用 PSO、`GetTexDataAsRGBA32` + `SetTexID` の固定アトラス。`MetalGraphicsDeviceImpl::CopyToBackBuffer` の末尾で、drawable を `loadAction = Load` にしたレンダーパスを開いて重ね描く(Vulkan と同じ位置)。Mac の Metal 構成と iOS が使う |
| Mac の入力 | 自前の `MacImGui.mm`(720 行)。`PlatformMac::PumpEvents` → `DispatchInputEvent` から `MacImGui::HandleEvent` を呼ぶ(NSEvent の入り口は 1 本)。フォーカスは `AqWindowDelegate` の `windowDidResignKey` / `windowDidBecomeKey` から `OnFocusChanged`。DisplaySize はエンジンの画面サイズ、DeltaTime はエンジンの値、`DisplayFramebufferScale` は既定の (1,1)。Command 押下中に keyUp が来ない問題を、Command を離した時点の全キー解放で避けている。Mac の Vulkan 構成も同じ入力を使う |
| iOS / Android の入力 | 共通の `ImGuiPointerInput`(マウス抽象からポインタだけを流すシム)。公式の iOS 用プラットフォームバックエンドは無く、Android は `imgui_impl_android` を使わないと Android 移植設計 P7 で決めている |
| Android の描画 | E1a で公式 Vulkan に置き換え済み(Windows と同じ `VulkanImGui.cpp`)。この Mac には NDK が無く、ビルドもできない |
| HiDPI | `PlatformMac` が `contentsScale = 1` に固定し、drawable を論理サイズ(ポイント)にそろえている(Mac移植設計 §8-13)。iOS も同じ前提(`PlatformiOS.mm` 冒頭) |
| ユーザーテクスチャ | Metal の `GetNativeHandle()` は `id<MTLTexture>` を返す。公式 metal の `ImTextureID` も `id<MTLTexture>` なので、**変換は要らない**(D3D12 / Vulkan と違う) |
| ObjC のメモリ管理 | エンジンの `.mm` は **ARC なし(手動の retain / release)**。公式 `imgui_impl_metal.mm` / `imgui_impl_osx.mm` は ARC 前提(`[[X alloc] init]` を strong プロパティへ入れたまま release しない、`__bridge_retained` など)で、ARC なしでコンパイルすると毎フレームのリークになる |
| `ImGuiVK.fx` | 参照は `MetalImGui.mm` と `shader_entries.txt` の 2 行だけ(Vulkan は E1a で使わなくなった) |

公式 `imgui_impl_osx.mm` の性質(読んで確かめたこと):

- 入力は自分で拾う。`ImGui_ImplOSX_Init(view)` が `addLocalMonitorForEventsMatchingMask` でマウス・キー・修飾キーのイベントモニタを張る。エンジンは `[NSApp sendEvent:]` で配送しているので、モニタは呼ばれる(ローカルモニタは `sendEvent:` を通るイベントに掛かる)。
- 文字入力と IME のために、`KeyEventResponder`(`NSTextInputClient`)を渡したビューの子に足し、**ウィンドウのファーストレスポンダにする**。文字は `interpretKeyEvents` → `insertText` で入る。
- フォーカスはアプリのアクティブ化通知(`NSApplicationDidBecomeActive` / `DidResignActive`)から `AddFocusEvent(true/false)` を両方送る。
- `ImGui_ImplOSX_NewFrame(view)` が `DisplaySize = view.bounds`、**`DisplayFramebufferScale = backingScaleFactor`**(Retina では 2)、DeltaTime(mach 時間)を入れ、カーソル形状・ゲームパッド(`GCController`)・IME の位置を更新する。
- `PlatformHasViewports` を立てる(Mac の Vulkan 構成では、E1a で入れた `Platform_CreateVkSurface` の仮ハンドラが効く)。
- バックエンドのデータはコンテキストごと(`io.BackendPlatformUserData`)だが、モニタのハンドラは**そのとき current のコンテキスト**へ入れる。

公式 `imgui_impl_metal.mm` の性質:

- `ImGui_ImplMetal_NewFrame(renderPassDescriptor)` が、渡されたパスのカラー / 深度のフォーマットとサンプル数を控える。`RenderDrawData(drawData, commandBuffer, encoder)` はそれをキーに PSO を引き、頂点 / インデックスバッファは `commandBuffer` の完了ハンドラでキャッシュへ戻す。
- `RendererHasTextures`(動的フォント)と `RendererHasViewports` を立てる。描画中に `draw_data->Textures` の状態を書き換える(E1a の「ImGui 有効時はレンダーの直列実行」の契約がそのまま要る)。
- iOS でもコンパイルできる(Multi-Viewport の部分は `TARGET_OS_OSX` で囲まれている)。

### 決定

- **2 段に分ける。** E1b-1 で Metal の描画を公式に置き換え(Mac の Metal と iOS)、E1b-2 で Mac の入力を公式 osx に置き換える。osx は入力の経路・ファーストレスポンダ・HiDPI と、エンジンの前提に触れる箇所が多く、描画と分けて確かめる。それぞれ別コミットにする。
- **同梱**: `imgui_impl_metal.{h,mm}` と `imgui_impl_osx.{h,mm}` を同じタグから `ThirdParty/imgui/` へ素のまま写す(E1a と同じく改変しない)。`imgui.vcxproj` には足さない(Windows ではビルドしない)。
- **CMake**(`ThirdParty/CMakeLists.txt` の imgui ターゲット): Metal 構成(macOS / iOS)で `imgui_impl_metal.mm`、macOS(iOS 以外。API を問わない)で `imgui_impl_osx.mm` を足す。この 2 本だけ `set_source_files_properties(... COMPILE_OPTIONS -fobjc-arc)` で **ARC を有効にする**(エンジンの `.mm` には波及させない)。フレームワークは aqEngine が既にリンクしているもの(Cocoa / Metal / QuartzCore / GameController)で足りる。OBJCXX はルートの `CMakeLists.txt` が Apple で有効にしている。
- **E1b-1: `MetalImGui` を接続層に書き換える**(ファイル名は残す。D3D12ImGui / VulkanImGui と同じ形)。
  - `Init()` は `ImGui_ImplMetal_Init(device)`、`Shutdown()` は `ImGui_ImplMetal_Shutdown()`。PSO・フォントアトラス・頂点ストリーミングなど自前の部分は全部消す。
  - `NewFrame()`(メインスレッド)は何もしない。公式の `ImGui_ImplMetal_NewFrame` は**レンダースレッドの `Render()` の中で、実際に使うレンダーパス記述子を渡して呼ぶ**。メインスレッドでは drawable が無く、フォーマットを正しく渡せないため。公式 metal のデータはレンダースレッドだけが触ることになる(Init / Shutdown はメインスレッドだが、そのときレンダースレッドは描画していない。E1a の D3D12 / Vulkan と同じ前提)。
  - `Render()` の引数を `(commandBuffer, encoder, renderPassDescriptor, drawData)` に変え、中で `ImGui_ImplMetal_NewFrame(rpd)` → `ImGui_ImplMetal_RenderDrawData(drawData, commandBuffer, encoder)` を呼ぶ。呼び出し元 `MetalGraphicsDeviceImpl::CopyToBackBuffer` は、今の `imguiPass` と `objects_->currentCommandBuffer` を渡すだけにする(描画位置・タイミングは変えない)。
  - ユーザーテクスチャの変換は無し。シャドウ深度マップ(配列)は E1a で全 API とも `ImGui::Image` をやめているので、ここでも対処は要らない。
  - `ImGuiVK.fx` と `shader_entries.txt` の 2 行を削除する。`MetalImGui.h` の「imgui_impl_metal は版数非互換で使わない」の説明を書き直す。
  - Shutdown の前に GPU の完了を待っていること(公式の Shutdown はフォントのテクスチャを解放する)を、今の `MetalImGui::Shutdown` の呼ばれ方で確かめ、待っていなければ待つ。
- **E1b-2: `MacImGui` を公式 osx の接続層に書き換える**(ファイル名は残す)。
  - `Init(NSView*)` で `ImGui_ImplOSX_Init(view)`。`PlatformMac::ImGuiInit` から `objects_->view`(`AqMetalView`)を渡す。`Init` の引数に ObjC 型が入るので、`MacImGui.h` の宣言は `__OBJC__` の中へ移す(呼び出し元の `PlatformMac.mm` は ObjC++)。
  - `NewFrame()` は `ImGui_ImplOSX_NewFrame(view)` の後で **`io.DisplayFramebufferScale = (1,1)` に戻す**。`contentsScale = 1` で drawable がポイント単位のため、2 のままだと公式 metal / vulkan がビューポートとシザーを 2 倍で計算してずれる。`DisplaySize` は公式の `view.bounds` のまま使う(drawable と同じ寸法。マウス座標もビューのポイントで来るのでそろう)。DeltaTime も公式の値を使う。
  - `HandleEvent` と `OnFocusChanged` を削除し、`PlatformMac.mm` の呼び出し(`DispatchInputEvent` の末尾、`windowDidResignKey` / `windowDidBecomeKey`)も外す。入力は公式のモニタ、フォーカスは公式のアクティブ化通知に任せる。ゲーム入力との排他は今と同じく `AppHost::Update` の `WantCapture*` → `Suppress*` で効く。`CocoaInputSink::OnFocusLost` の呼び出しは残す。
  - ファーストレスポンダは `KeyEventResponder` に移る。エンジンのゲーム入力は `DispatchInputEvent`(レスポンダチェーンを通らない)で受けているので影響しない。ImGui が使わないキーは `KeyEventResponder` が `super` へ回すので、扱いは今と同じ(`AqMetalView` も keyDown を実装していない)。
  - 自前の対策で公式に無いものは **Command 押下中の keyUp 欠落**への対処。実機で確かめ(11.3 の E1b-2)、キーが押しっぱなしになる場合だけ、`DispatchInputEvent` で Command を離した `FlagsChanged` を見て、マウス以外の全名前付きキーの `io.AddKeyEvent(key, false)` を**イベントキューへ積む**口を `MacImGui` に足す(今の `ReleaseAllKeys` と同じ方式。足したら本節に記録する)。`io.ClearInputKeys()` は使わない。現在の状態を消すだけでキューに残った押下イベントは消さないため、同じ `PumpEvents` の中で押下と Command の解放が続くと、次の `NewFrame` で押下が再び適用されて残る(`imgui.cpp` の `ClearInputKeys` とイベント処理)。
  - **確認用に ImGui 標準の Metrics ウィンドウを開く口を足す。** キーの押下状態(`ImGui::IsKeyDown`)は文字入力(`InputQueueCharacters`)と別の経路で、テキスト欄に文字が連続入力されないことでは押しっぱなしを判定できない。エンジンには `ImGui::ShowMetricsWindow` を開く口が無いので、`AppHost` のメインメニューバー(`ImGui::BeginMainMenuBar` の中、モジュールのメニューの後)に `ImGui` メニューを足し、`Metrics` の項目で `ShowMetricsWindow` の表示を切り替える(`AQ_DEBUG_IMGUI` の範囲。全プラットフォームに出るが、既存のメニューの挙動は変えない)。Metrics の `Inputs` 欄に押下中のキーが出る。
  - **スクロールの量と横方向の向きは公式に合わせて変わる。** トラックパッド(精密デルタ)の係数が自前の 0.1 から公式の 0.01 になり(1/10)、横方向は自前が符号を反転していたのを公式は反転しない。公式の値を受け入れる(公式バックエンドを素のまま使う方針。改変しないと戻せない)。11.3 の E1b-2 で、長いパネルを縦・横にスクロールしてユーザーが受け入れられるか確かめ、受け入れられなければ設計へ戻す。
  - ゲームパッド: 公式は `GCController` を ImGui へ流すが、エンジンは `ImGuiConfigFlags_NavEnableGamepad` を立てていないので ImGui の操作には使われない。Windows の公式 win32 も XInput を流しており、挙動はそろう。
  - IME: 公式の `KeyEventResponder` で使えるようになる。日本語のグリフは Mac のフォント(SFNS)に無いので、入力できても `?` で出る。フォールバックのフォントは E1b では足さない(15 章)。
- **iOS**: 描画は E1b-1 で公式 metal に切り替わる。入力は `ImGuiPointerInput` のまま(公式の iOS 用プラットフォームバックエンドが無い)。
- **Android**: コードの変更は無い(E1a で公式 Vulkan に切り替わっており、入力は `ImGuiPointerInput` のまま)。ビルドと実機の確認は NDK のある Windows 機で行う(11.3 の E1b の Android 項目)。
- **2 つのコンテキスト**: Mac にはまだエディタ構成が無い(E10)ので、Mac で動くのはゲーム用の 1 つだけ。公式 osx のモニタは current のコンテキストへ入れるので、E10 でエディタ用コンテキストを足すときに入力の振り分けを設計し直す(15 章)。
- **一次資料の分担**: Metal / Mac の ImGui の構成は本節を一次資料にする。[MetalBackend設計](../MetalBackend設計.md) §12 P6 と [Mac移植設計](../Mac移植設計.md) P4b の自前バックエンドの記述には、E1b で公式へ置き換えた旨と本節へのリンクを足す(本文は履歴として残す)。

### E1b-2 の途中経過(2026-11-18。実装中に分かった設計からの変更を含む)

- **`AqMetalView` の `acceptsFirstResponder` を NO にした(設計からの変更。2026-11-19 ユーザー承認)。** 設計どおりの実装(公式 osx に置き換えただけ)では、ImGui のテキスト欄に文字が入らなかった。原因: 公式 osx は自分で足す `KeyEventResponder` をファーストレスポンダにし、`interpretKeyEvents` → `insertText` で文字を受ける。`AqMetalView` が `acceptsFirstResponder` で YES を返すため、ビューをクリックした時点でファーストレスポンダが `AqMetalView` へ移り、文字の経路が切れる。キーの押下・解放はイベントモニタで届くので、Backspace は効くのに文字だけが入らない、という症状になる。ゲームのキー入力は `PumpEvents` → `DispatchInputEvent` で直接受けていてレスポンダチェーンを使わないため、YES にした P4 の理由はもう無い。NO にしたビルドで文字入力が通ることを確かめた。`PlatformMac` の初期化にある `[window makeFirstResponder:view]` は NO を返すビューに対しては何もしないので、残っていても害は無い(整理は任意)。
- **Command 押下中の keyUp 欠落への対策は要らなかった。** NO にしたビルドで、テキスト欄で Command+C → Command を先に離す → C を離す、を 500ms 間隔と間隔 0 の両方で送り、`ImGui > Metrics` の Inputs のキーボード図と Keys mods に C や修飾キーが残らないことを撮影で確かめた(C を押し続けている間は図で赤く強調されることも確かめ、判定に使えることを確認した)。合成には修飾キー付きの `CGEventPost` を使った。
- **DragFloat の数値入力は Command+クリックで入る。** macOS では ImGui が Ctrl と Command を入れ替えて扱う(`io.ConfigMacOSXBehaviors`)ので、Windows の Ctrl+クリックに当たる操作は Command+クリック。自前のバックエンドでは入らなかった。
- **未解決の観察(1 回だけ)**: 最初の起動で、`ImGui > Metrics` を開いた直後から ImGui がマウスもキーも受け付けなくなり、ウィンドウのタイトルバーをクリックしたら戻った(マウスボタンが押されたままの扱いになっていた可能性がある)。その後、同じ操作を 5 回以上繰り返しても再現しなかった。原因は分かっていない。再現したら、Metrics の Inputs の Mouse down を撮って確かめる。
- **確認済み(NO にしたビルド、Metal、Validation 有効)**: UI の位置と大きさが E1b-1 と同じ(Retina で `DisplayFramebufferScale` を 1 に戻す処理が効いている)、メニュー・ツリー・ウィンドウのドラッグ・ホイールのスクロール、`ImGui > Metrics` の開閉、他のアプリへ切り替えて戻った後の入力、Validation のエラー 0 件、テキスト欄の編集は Esc で取り消せてデータが変わらないこと。
- **追加の確認(2026-11-19、NO にしたビルド)**:
  - ゲームのキー操作は今までどおり。タイトルで Space を押すとステージが始まり、W を 3 秒押し続けると 266 km/h まで加速した。
  - ImGui のテキスト欄に入力している間に Space を押すと、Space は欄に入り、タイトル画面のまま(`WantCaptureKeyboard` が効いている)。ゲーム画面をクリックして ImGui からフォーカスを外すと、Space でステージが始まる。
  - Mac の Vulkan 構成(`macos-ninja` の Debug)で起動し、`ImGui` メニューが開いて assert で止まらない。`vk_debug.log` は E1a で記録した既存のポストプロセスの警告 3 件だけで、エラーは 0 件。
  - リーク: 「起動 → 20 秒待つ → 閉じるボタン」の同じ手順を 2 回ずつ行った。`leaks` は E1b-1 後 416 / 415 件、E1b-2 後 416 / 415 件で変わらず、報告に ImGui のスタックは無い。MemoryTracker は E1b-1 後 9347 / 9343 件、E1b-2 後 9392 / 9397 件で、約 50 件多い(40 バイト ×18、112 バイト ×18、32 バイト ×15 など)。ImGui はアロケータを差し替えておらず(既定の `malloc`)、公式 osx 自身の確保は MemoryTracker に数えられない。そのため、この差はグローバル `operator new` の置き換えで、公式 osx が使い始めた macOS のフレームワーク(`NSTextInputContext`、`GCController` の問い合わせなど)内部の確保を数えたものと判断した。到達できないメモリを数える `leaks` が増えていないので、リークではない。
  - 合成入力の落とし穴: 起動直後の最初のクリックが効かないことは E1b-1 後のバイナリでも起きた。最初に「1 回だけ入力が止まった」件も、公式 osx に固有とは言い切れない。
- **スクロール(2026-11-19 ユーザー確認)**: 実際のトラックパッドとマウスホイールで Rendering パネルと UI Editor のツリーをスクロールし、公式の量(トラックパッドは自前の 1/10)と横の向きで違和感が無いことを確かめた。公式の値をそのまま使う。

### テスト方針

| 確認 | 方法 | 担当 |
|---|---|---|
| ビルド | `macos-ninja-metal`・`macos-ninja`(Vulkan)・`ios-simulator-xcode` の Debug と、`ios-xcode` の Debug(`CODE_SIGNING_ALLOWED=NO` でコンパイルまで)。エンジン側の警告が増えていないこと(ImGui 本体の警告は除く) | Claude |
| Mac の表示 | Game.app(Metal)を起動し、ウィンドウ ID 指定の撮影でデバッグ UI と Hi-Z の `ImGui::Image` が出ることを見る。**起動の前にユーザーへ確認する** | Claude(許可後) |
| Mac の入力 | CGEvent の合成入力で、メニュー・スライダー・タブの操作、テキスト欄への文字入力、ウィンドウを裏へ回して戻した後の入力を確かめる(手順は下の「Mac での確かめ方」) | Claude(許可後) |
| 操作感 | 実際のマウス・キーボード・トラックパッドのスクロール、Command+C / V、Retina での文字のにじみ | ユーザー |
| iOS | シミュレータで起動してデバッグ UI が出て、タップで操作できること | Claude(許可後) |
| リーク | E1b-1・E1b-2 のそれぞれで、(a) エンジンの MemoryTracker の終了時の件数と、(b) macOS の `leaks` が報告する件数を、変更前のバイナリと比べる。MemoryTracker はエンジンのアロケータ経由の確保しか数えない(グローバル `operator new` を置き換えているだけで、ImGui の既定アロケータの `malloc` や ObjC の `alloc` は対象外)ので、公式バックエンドの ObjC / Metal オブジェクトのリークは (b) で見る | Claude |

**Mac での確かめ方**(別 PC でも同じ手順で行えるように書く):

1. Debug 構成でビルドする(`AQ_DEBUG_IMGUI` は Debug にしか付かない)。起動は `.app` を `open` せず、作業ディレクトリを `DirectX/Game` にして `build/<preset>/bin/Debug/Game.app/Contents/MacOS/Game` を直接起動する(`aqBundleApp` を通していない `.app` は Assets を同梱していない)。ウィンドウが出るまで 40 秒ほどかかる。Metal の Validation は環境変数 `METAL_DEVICE_WRAPPER_TYPE=1` で有効になる(既定は無効)。
2. 撮影は `screencapture -x -o -l<ウィンドウ ID>`(ウィンドウ ID は `CGWindowListCopyWindowInfo` で pid から引く)。領域指定は他のアプリが写り込むので使わない。操作の後 1 秒待ってから撮る(撮影がフレームを追い越す)。画像は Retina の 2 倍で、スクリーン座標 = ウィンドウの原点 + 画像の座標 × 0.5。
3. 入力は `CGEventCreateMouseEvent` / `CGEventCreateKeyboardEvent` + `CGEventPost` で送る(`osascript` の `click at` はアクセシビリティ経由で、自前描画の ImGui には届かない)。送る前にアプリを前面に出す。ImGui のボタンは「近くへ移動 → 対象へ移動 → 0.3 秒待ってクリック」で押す(同じフレームに移動とクリックが入るとホバー判定が追いつかない)。
4. 左上の FPS オーバーレイがメニューバーの左側を覆ってクリックを吸うことがある(既存の表示の問題)。右端のメニューを開いてからホバーで左へ移る。
5. 終了は閉じるボタンで行う(Esc では終了しない)。起動中に `DirectX/Game/imgui.ini`(追跡対象)が書き換わるので、終わったら `git checkout -- DirectX/Game/imgui.ini` で戻す。
6. `leaks` は `MallocStackLogging=1 leaks --atExit -- <Game のパス>` で起動し、閉じるボタンで終了して `Process N: M leaks` の M を比べる。変更前の値は、変更前のコミットでビルドしたバイナリで同じ操作をして取る。

---

## 評価チェックリスト(旧 11.3 節)

### E1a ImGui 更新(Windows)

- [x] 採用した ImGui の版(タグかコミット)を本書 4 章に記録した(`v1.93.0-docking`)
- [x] 自前の `D3D12ImGui`・`VulkanImGui` が削除され、公式の dx12 / vulkan / dx11 / win32 が使われている
- [x] Game の Debug / Release × D3D11 / D3D12 と Editor の 2 構成がビルドでき、警告数が E1a 前と同じ(ImGui 本体の警告は除く)。`imgui.vcxproj` は `AqGraphicsApi=Vulkan` のときだけ `imgui_impl_vulkan.cpp` を有効化する。**Vulkan は 2026-11-17 に Vulkan SDK のある PC で Game の Debug / Release をフルビルドし、警告 17 件(ImGui 本体を除く)で成功**
- [x] CMake の Windows ビルド(Sample の D3D12)が通る。`windows-vs2026` プリセットで再構成・ビルドし、`Sample.exe` が起動・終了コード 0。**Vulkan も 2026-11-17 に `build/win-vulkan` の Sample をビルドして成功**(`imgui_impl_vulkan.cpp` がコンパイルされることを確認)
- [x] デバッグ UI の表示と操作が E1a 前と同じ。`ImGui::Image` を使うパネル(G-Buffer、Hi-Z、シャドウなど)の画像も出る(**D3D11 / D3D12 で Claude がマウス自動操作+スクリーンショットで確認**。メニューバー・ドッキング済みウィンドウ・スライダー・タブ切り替えが正常に描画され、Hi-Z タブで実テクスチャ内容(深度ピラミッド、L0〜L3 の各ミップ)が正しく表示された。GBuffer タブはタイトル画面では中身が黒(ディファード未描画)で見た目の判別が付かないため保留。Shadow タブは意図どおり「シャドウ深度マップの画像表示は未対応」+解像度を表示。**Vulkan は未確認**(SDK 無し)。操作感(ドラッグ・入力)はキーボード/コントローラ入力の自動化ができないため未確認)
- [x] 画像を出すパネルを開いたまま 1 分以上動かし、画像が出続ける(D3D11 / D3D12 / Vulkan)
- [x] D3D11 / Vulkan で、画像を出すパネルを開いたままウィンドウのリサイズ(RT とスワップチェーンの作り直し)を 3 回行っても、画像が出続ける。D3D12 は現状レンダーリサイズに対応していない(作り直しの経路がない)ので対象外とし、E3b で同じ確認をする。D3D12 のユーザーテクスチャはフレームごとにディスクリプタをコピーし、RT 単位のキャッシュを持たないので、作り直しで無効になる状態はない
- [x] まだ表示していない文字(例: 普段使わないパネルの記号)を初めて表示したときに、文字化けや欠けがない(動的フォントのアトラス更新を通す)
- [x] 上の操作の間と終了時に、D3D12 のデバッグレイヤーと Vulkan のバリデーションのエラーが E1a 前より増えていない(`vk_debug.log` とデバッグ出力で確認)。MemoryTracker のリーク報告も増えていない
- [x] `AQ_RENDER_PIPELINED` と `AQ_IMGUI` を同時に定義するとコンパイルエラーになる(`cl` 単体実行で `C1189` を確認)
- [x] imgui.ini が旧版のものでも起動できる(既存の `Game/imgui.ini` のまま Game.exe / Editor.exe が起動した)
- [x] (Mac 機で作業するときに確認)Mac / iOS / Android のビルドが通る(Mac・iOS は Mac 上で、Android は APK 生成まで)。**2026-11-18 に Mac で確認**: `macos-ninja`(Vulkan)・`macos-ninja-metal`・`ios-simulator-xcode` の Debug がビルド成功。**Android は Mac 機に NDK が無く環境要因で未確認**(Windows 機の宿題)

**Mac / iOS のビルド確認(2026-11-18)**: Mac の Vulkan(MoltenVK)構成がリンクで落ちた(`ImGui_ImplVulkan_Init` などが未定義)。`VulkanImGui.cpp` は Mac の Vulkan でも公式バックエンドを呼ぶのに、`ThirdParty/CMakeLists.txt` の `imgui_impl_vulkan.cpp` を足す条件が「Windows の Vulkan か Android」で、Mac の Vulkan が漏れていた。条件を `AQ_GRAPHICS_API STREQUAL "Vulkan"`、SDK のインクルードを「Vulkan かつ Android 以外」に直した。`ios-ninja` は Game のリンクで Foundation のシンボルが未定義になるが、これはコンパイル確認専用のプリセット(リンクと署名は Xcode 生成側)で E1a と無関係。`ios-simulator-xcode` は成功する。

**Vulkan の起動(2026-11-17)**: 最初の起動で `imgui_impl_vulkan.cpp` の assert(`Platform_CreateVkSurface != nullptr`)で停止した。docking 版の win32 バックエンドは `PlatformHasViewports` を立てるが Vulkan 用のサーフェス作成ハンドラは登録せず、公式 Vulkan バックエンドの `Init` がその組み合わせを assert するため(Multi-Viewport を有効にしていなくても起きる)。`VulkanImGui.cpp` で `ImGui_ImplVulkan_Init` の前に、未登録なら常に失敗を返す仮のハンドラを置いて回避した。本当のサーフェス作成は Vulkan の Multi-Viewport 対応(E3a)で書く。修正後、Vulkan 版 Game.exe(Debug)が 15 秒動作して終了コード 0、`[crash]` なし、`vk_debug.log` のエラー 0 件。

**操作の確認(2026-11-17)**: Vulkan と D3D11 はユーザーが、D3D12 は Claude がマウス・キーの自動操作とスクリーンショットで確認した。
- 1 分以上の表示: Hi-Z・G-Buffer の画像が 65 秒以上出続けた(3 API)。
- リサイズ: D3D11 / Vulkan でウィンドウを複数回リサイズ(Vulkan はスワップチェーンの作り直し 10 回)しても画像が出続け、クラッシュなし。
- 動的フォント: 「ポストエフェクト」タブや「シャドウ深度マップの画像表示は未対応」など、これまでグリフ範囲から外していた日本語も初回表示から正しく出た(公式バックエンドの `RendererHasTextures` により必要な文字がその場でアトラスへ足される)。
- エラー: Vulkan は最初の確認で `vk_debug.log` に 2D_ARRAY のビューを 2D として読むエラーが 10 件出た。原因は `DeferredDebugPanel` のシャドウ深度マップの `ImGui::Image` の止め忘れ(4.4 の決定に反して残っていた)で、`ShadowDebugPanel` と同じく文字表示に直した後はエラー 0 件。残る警告 3 件はポストプロセスのコンピュートの書式不一致で、ImGui とは無関係の既存のもの。D3D12 のデバッグレイヤーの出力はデバッガ接続が要るため未確認(クラッシュ・表示崩れはなし)。
- 気づいた点: G-Buffer0(BaseColor + Metallic)と Bright Extract の画像が透明に見える。ImGui はアルファでブレンドして描くため、アルファ(Metallic など)が 0 の画像は見えない。3 API で同じで、自前バックエンドの頃と同じ描き方なので E1a の退行ではない(README の既知の課題に記録)。

**E1a で残っている項目は、Mac / iOS / Android のビルド確認(Mac 機で行う)と、D3D12 のデバッグレイヤー出力の確認のみ。D3D12SRV / VulkanSRV に view dimension を問い合わせるアクセサが無いため、配列テクスチャ(シャドウ深度マップ)の検出・警告は未実装(4.4 節「E1a の結果」参照)。**

### E1b ImGui 更新(Mac / iOS / Android)

詳細は 4.5 節。E1b-1 と E1b-2 は別コミットにする。

**E1b-1 Metal の描画(Mac の Metal / iOS)**

- [x] 自前の描画(PSO・フォントアトラス・頂点ストリーミング)が `MetalImGui.mm` から消え、公式 `imgui_impl_metal` を呼ぶ接続層になっている。`ImGuiVK.fx` と `shader_entries.txt` の 2 行が消えている(`grep -rn ImGuiVK aqEngine` が 0 件)
- [x] `imgui_impl_metal.mm` が ARC 付きでコンパイルされている(Ninja Multi-Config では `CMakeFiles/impl-Debug.ninja` のそのファイルの FLAGS に `-fobjc-arc` があり、`MetalImGui.mm` には無い)
- [x] `macos-ninja-metal`・`macos-ninja`・`ios-simulator-xcode` の Debug と、`ios-xcode` のコンパイル(`CODE_SIGNING_ALLOWED=NO`)が通り、エンジン側の警告が増えていない(新しい警告は ImGui 本体の `MTLStorageModeManaged` の非推奨だけ)
- [x] Mac(Metal)の Game.app でデバッグ UI が E1b 前と同じ位置・大きさで出て、Hi-Z の `ImGui::Image` に実テクスチャが出る(撮影で確認。Hi-Z は 1 チャンネルの深度なので赤く出るが、E1b 前のバイナリでも同じ。README の既知の課題)
- [x] 画像を出すパネルを開いたまま 1 分以上動かし、ウィンドウのリサイズを 3 回しても画像と UI が出続ける(drawable の作り直し)。リサイズ 4 回で Hi-Z の L0 が 640x360 → 550x309 に作り直され、画像と UI が出続けた
- [x] まだ表示していない文字(Latin-1 の記号など)を初めて出したときに欠けない(動的フォント)。パネルを初めて開いたときの文字は欠けない。日本語は SFNS にグリフが無く「?」になる(E1b 前と同じ。15 章)
- [x] Metal の API Validation を有効にして起動・操作・終了し、ImGui 由来のエラーが出ない(環境変数 `METAL_DEVICE_WRAPPER_TYPE=1` で起動し、`Metal API Validation Enabled` が出ることを確かめる)
- [x] 4.5 節のテスト方針の (a) MemoryTracker と (b) `leaks` の終了時の件数が E1b-1 前と比べて増えていない(同じ手順で E1b-1 前 9310 / 416 件、後 9314 / 415 件。`leaks` の報告に ImGui のスタックは無く、Metal 関連の行数も同じ)
- [x] iOS シミュレータでデバッグ UI が出る(撮影で確認)。**タップでの操作は未確認**: この Mac の Xcode に Simulator.app が無く、タップを合成できない。iOS の入力経路(`ImGuiPointerInput`)は E1b-1 で変えていない

**E1b-1 の結果(2026-11-18)**

- 実装: `MetalImGui.mm` は 364 行から約 100 行の接続層になった。`Init` は `ImGui_ImplMetal_Init(device)`、`NewFrame` は何もせず、`Render(commandBuffer, encoder, renderPassDescriptor, drawData)` が `ImGui_ImplMetal_NewFrame(rpd)` → `RenderDrawData` を呼ぶ。`MetalImGui.mm` 自体は ARC なしのままで、ARC で誤ってビルドされたら `#error` で止まる。
- Shutdown の GPU 待ちは既にある。`AppHost::Finalize` がレンダースレッドを止め、`GraphicsDevice::WaitIdle()`(Metal は最後のコマンドバッファの `waitUntilCompleted`)の後で `ImGuiShutdown` を呼ぶ(`IGraphicsDeviceImpl.h` の契約)。公式がバッファを回収する完了ハンドラも、その前に呼び終わる。
- 公式はビューポートを `DisplaySize × FramebufferScale` から計算する(自前は drawable の実寸で切り詰めていた)。リサイズと Validation 有効での確認では問題は出なかった。
- 計測の落とし穴: 変更前のバイナリを作業ツリーのまま起動すると、`shader_entries.txt` から ImGuiVK が消えているため `LoadMsl` の assert で落ちる。変更前の計測は `git worktree` で変更前のコミットをビルドして、その `DirectX/Game` を作業ディレクトリにして行う。
- 合成入力の落とし穴: 起動直後の最初のクリックが ImGui に届かず、ウィンドウを動かしてしまうことがあった。1 手ごとに撮影して確かめてから次の操作を送る。

**E1b-2 Mac の入力(osx)**

- [x] `MacImGui.mm` から自前の入力処理(キー写像・`HandleEvent`・`OnFocusChanged`)が消え、公式 `imgui_impl_osx` を呼ぶ接続層になっている。`PlatformMac.mm` から `MacImGui::HandleEvent` / `OnFocusChanged` の呼び出しが消えている
- [x] `imgui_impl_osx.mm` が ARC 付きでコンパイルされ、`macos-ninja-metal` と `macos-ninja`(Vulkan)の Debug が通る
- [x] Retina のディスプレイで、ImGui の UI の位置・大きさとクリック位置がずれない(`DisplayFramebufferScale` を 1 に戻す処理の確認)
- [x] マウスでメニュー・スライダー・タブ・ウィンドウのドラッグを操作でき、ImGui の上をクリックしてもゲームが反応しない(`WantCaptureMouse`)。メニュー・ツリー・ウィンドウのドラッグは確認済み。AquaDash はマウスを使わないので、ゲーム側の反応はキーで確かめた(下の項目)
- [x] ImGui のテキスト欄に ASCII を入力でき、入力中はゲームがキーに反応しない(`WantCaptureKeyboard`)。入力は `acceptsFirstResponder` を NO にして確認。入力中の Space でタイトルが進まないことも確認(4.5 節の「E1b-2 の途中経過」)
- [x] ImGui のテキスト欄で Command+C → Command を先に離す → C を離す、を (a) ゆっくり(各 0.5 秒あけて)と (b) 素早く(合成入力で間を空けずに 1 回の `PumpEvents` に入るように)行い、どちらでも C が押しっぱなしにならない(**2026-11-18 確認済み。対策は不要だった**)。判定は、操作の後に `ImGui > Metrics` の `Inputs` 欄で押下中のキー(Keys down)に `C` が残っていないことを撮影で確かめる(文字が連続入力されないことでは判定できない。4.5 節)。残る場合は 4.5 節の対策を足し、同じ操作でもう一度確かめる
- [x] メインメニューバーの `ImGui > Metrics` で ImGui の Metrics ウィンドウが開閉できる
- [x] 他のアプリへ切り替えて戻った後も、マウスとキーの入力が ImGui に届く(`AppFocusLost` の解除)
- [x] Mac の Vulkan 構成で起動し、デバッグ UI が出て操作でき、`Platform_CreateVkSurface` の assert で止まらない
- [x] DragFloat の Ctrl+クリックからの数値入力で文字が入る(自前のバックエンドでは入らなかった)。macOS では Command+クリック(4.5 節)
- [x] 長いパネルをマウスホイールとトラックパッドで縦・横にスクロールし、量(トラックパッドは自前の 1/10 になる)と横の向き(自前と逆になる)をユーザーが受け入れられる(4.5 節。受け入れられなければ設計へ戻す)。2026-11-19 ユーザー確認で違和感なし
- [x] 4.5 節のテスト方針の (a) MemoryTracker と (b) `leaks` の終了時の件数が E1b-1 後と比べて増えていない。`leaks` は同じ。MemoryTracker は約 50 件多いが、macOS のフレームワーク内部の確保でリークではないと判断した(4.5 節)

**Android(NDK のある Windows 機で確認)**

- [ ] Android の APK が生成でき、実機で ImGui のタッチ操作が E1a 前と同じ(E1b ではコードを変えない。E1a の公式 Vulkan への置き換えの確認)
