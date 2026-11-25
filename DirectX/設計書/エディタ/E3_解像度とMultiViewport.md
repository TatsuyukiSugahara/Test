# aqEngine エディタ E3: ゲームビューの解像度追従と Multi-Viewport

> 対象コミット: d993cda / 最終更新: 2026-11-25(エディタ全体設計.md の旧 5.9、5.10 節と 11.3 節の該当フェーズを分割して移した。本文は分割前のまま。各節の照合コミットは節の「現状」に記載)
> 全体の構成・到達点・フェーズ一覧・未決事項は [エディタ全体設計](../エディタ全体設計.md)。節番号(4.4 など)は分割前の番号のまま残している。

## 5.9 E3b の詳細(2026-11-19 追記)

対象は **D3D12**(Editor は D3D12 構成しか持たない)。Windows 機で進める。描画解像度のリサイズの仕組み(`HandleRenderResize`、パスの `PrepareResize` / `CommitResize`)の一次資料は [エンジン課題解消設計](../エンジン課題解消設計.md) の P4 と [01_レンダリング設計](../01_レンダリング設計.md) 2.11 節。本節は D3D12 の実装と、エディタで提示面とレンダー解像度を分ける部分だけを書く。

### 現状(調査結果。c79a0f5 で確認)

| 項目 | 実コード |
|---|---|
| Windows のリサイズ対応 | `SupportsRenderResize()` を true にしているのは Metal と、Apple の Vulkan だけ。D3D11・D3D12・Windows の Vulkan は既定の false で、`Engine::HandleRenderResize` は何もしない(起動時の 1280×720 のまま)。エンジン課題解消設計の **P4-C(D3D11 / D3D12 の `ResizeBuffers`、Win32 の `GetClientPixelSize`)は未実装**。11.3 節の旧 E3b の「D3D11 / Vulkan と同じく動く」は事実と違っていた |
| Win32 のウィンドウ寸法 | `PlatformWin32` は `WM_SIZE` を処理せず、`GetClientPixelSize` も実装していない(既定の false)。D3D12 は `GetSurfaceSize` も実装していないので、`SyncScreenSize` で `screenWidth_` も更新されない |
| D3D12 のスワップチェーン | `CreateSwapChain`(`FLIP_DISCARD`、2 枚、`R8G8B8A8_UNORM`、対応時 `ALLOW_TEARING`)。`ResizeBuffers` の呼び出しは無い。バックバッファの RTV(E2a の `CreateBackBufferRTVs`)は「起動時に 1 回」の前提で、`rtvNext_` を進めて確保する |
| D3D12 のメイン RT | `mainRenderTargets_[MAIN_RT_COUNT]`(直列では 1)。1 本で RTV・DSV・SRV・UAV を 1 つずつ使う。寸法のメンバは無い(引数でだけ渡す)。`D3D12RenderTarget::CreateOffscreen` は先頭で `Release()` するので、同じオブジェクトに同じディスクリプタの位置で作り直せる |
| D3D12 のオフスクリーン RT | `offscreenRTs_` に `push_back` するだけで、解放も空き番号の再利用も無い(`ReleaseOffscreenRenderTarget` は既定の何もしない)。ハンドルの基点は `RENDER_TARGET_COUNT`(= 2)で、Vulkan の `MAIN_RT_COUNT` と違う |
| ディスクリプタ | RTV(256)・DSV(64)・SRV ステージング(4096)はどれも線形確保で、解放・再利用が無い。容量の確認は `CreateBackBufferRTVs` にしか無い。幅・高さだけの `CreateOffscreenRenderTarget` は深度付き(DSV を使う)。そのままリサイズで RT を作り直すと、数回で RTV / DSV が尽きる |
| ImGui の不透明エントリ | `D3D12ImGui` の `g_opaqueEntries` は RT のリソースのポインタと `&rt->srv_` を持ち、外す関数が無い(74 行目の TODO)。RT を作り直すと、解放済みのリソースと SRV を指すエントリが残る |
| パス | エンジン標準のパスは全部 `Resizable` か `Independent`(`Unsupported` は無い)。ゲームの `OutlinePass` も `Resizable`。出力 RT(`GetOutputRT`)はパスの RT で、リサイズでハンドルが変わる。エディタは毎フレーム受け取り直しているので追従する |
| 寸法の追従 | `CommitRenderResize` がメインカメラの `SetViewportSize` と `Renderer::SetMainViewport` を更新する。`BuildRenderCommands` は毎フレーム `GetRenderWidth/Height` を読む。ゲーム用 ImGui の `DisplaySize`(エディタ構成)もレンダー解像度を毎フレーム読む。UI の canvas は仮想解像度で、描画先全体へ伸ばす |
| コマンドリスト | `BeginFrameIfNeeded` を呼ぶのは、フレーム中はレンダースレッドの処理(描画コマンド、ImGui、リードバック、`CopyToBackBuffer`、`Present`)。ただし初期化中はメインスレッドも RT やビューポートの設定でフレームを開く(`Engine.cpp` の初期化)。`Present` が `frameOpen_` を false に戻すので、最初の `Present` の後は `WaitForRenderIdle` の後に閉じている。`WaitForRenderIdle` 自体は開いたフレームを閉じない |
| 描画コンテキストの保留状態 | `D3D12RenderContextImpl` は保留中の SRV(`pendingSRV_`、ステージングの位置)・RTV・DSV をフレームをまたいで持ち続け、新しいフレームではテーブルを張り直すときにそのままコピーする(`FlushDescriptors`)。RT を解放・再利用すると、古い位置(解放済みのリソースや、同じ位置に作り直した別の RT)を参照し得る |
| 寸法の更新順 | `FrameStep` は `SyncScreenSize` → `HandleRenderResize` → `Update` の順。リサイズが成功したフレームは `renderWidth_` だけが新しく、`screenWidth_` は次のフレームまで旧い(ゲーム内 UI の当たり判定は `screenWidth_` を使う) |
| 埋め込み起動 | 子ウィンドウは親のクライアントの大きさで作り、`screenWidth_` も親に合わせるが、レンダー解像度は初期化引数のまま(スワップチェーンもレンダー解像度で作り、提示で拡縮する) |
| エディタの描画 | `RenderEditorOverlay` はビューポートとシザーをバックバッファの `GetDesc()` から取る。エディタ用 ImGui の `DisplaySize` は win32 バックエンドが `GetClientRect` から入れる。今はバックバッファが 1280×720 のままなので、ウィンドウの大きさを変えると UI が崩れる(README の既知の課題) |
| `CopyToBackBuffer`(D3D12) | `CopyResource` なので、出力 RT とバックバッファが同じ寸法である必要がある |

### 決定

Codex レビュー 1 回目の指摘 8 件(中 5・軽 3)は、すべて実コードで確かめて採用した: 描画コンテキストの保留中のディスクリプタ、リサイズしたフレームの `screenWidth_` の遅れ、提示面とレンダー解像度を分けたときの保留状態、埋め込み起動の寸法、ディスクリプタの頭打ちの評価条件、最小寸法と黒帯、初期化中に開いたフレーム、一次資料の更新計画。2 回目の指摘 2 件(中)も採用した: 保留状態を `FlushDescriptors` で捨てると設定し直した状態まで消える(入口の先頭で捨てる形にした)、D3D12 に `GetSurfaceSize` を足すと埋め込み起動の `screenWidth_` が上書きされる(足さずに、確定寸法を Engine が入れる形にした)。

E3b を 2 段に分ける。

| 段 | 内容 | Game.exe への影響 |
|---|---|---|
| **E3b-1** | D3D12 の描画解像度のリサイズ(P4-C の D3D12 の部分)と、Win32 の `GetClientPixelSize`。提示面とレンダー解像度は一緒にウィンドウへ追従する | D3D12 の Game.exe も、ウィンドウの大きさを変えるとその解像度で描き直すようになる(Mac と同じ動き)。D3D11 / Windows の Vulkan は変わらない(`SupportsRenderResize` が false のまま) |
| **E3b-2** | エンジンに「レンダー解像度をウィンドウ以外が決める」口を足し、エディタではゲームビューの大きさを渡す。提示面(スワップチェーン)はウィンドウ、レンダー解像度はゲームビューに追従する | 無い(口を呼ぶのは Editor だけ) |

**1. Win32 の寸法(E3b-1)**

- `PlatformWin32::GetClientPixelSize` を `GetClientRect(hwnd)` で実装する(ポーリング。`WM_SIZE` は使わない。P4 の方針どおり)。最小化中の 0×0 は Engine が「現状維持」で扱う(既存)。
- **埋め込み起動では false を返す**(今までどおり固定寸法)。埋め込みは子ウィンドウを親の大きさで作るがレンダー解像度は初期化引数のままなので、値を返すと親をリサイズしなくても起動直後にレンダー解像度が変わってしまう。

**2. D3D12 の実装(E3b-1)**

- `SupportsRenderResize()` を true にする。
- **寸法のメンバ**: 提示面の幅・高さ(`presentWidth_` / `presentHeight_`)を持つ。書くのはメインスレッドのリサイズ(レンダースレッドの停止中)と初期化だけなので、atomic にしない。**`GetSurfaceSize` は実装しない**(実装すると、埋め込み起動で `SyncScreenSize` が、親に合わせた `screenWidth_` をスワップチェーンの寸法=レンダー解像度で上書きしてしまう)。提示面の寸法は、リサイズの確定寸法として Engine へ返す(2.5)。
- **ディスクリプタの位置を覚える**:
  - メイン RT は作成時に使った RTV / DSV / SRV / UAV の位置を `mainRtSlots_[i]` に覚え、作り直しでは同じ位置を使う。
  - バックバッファの RTV は、位置の確保(初期化の 1 回)と、ビューの作成(初期化とリサイズのたび)を分ける。
  - オフスクリーン RT は、空き番号(`freeOffscreenSlots_`)と、番号ごとに使ったディスクリプタの位置(RTV・SRV・UAV・DSV。DSV は深度付きのときだけ)を覚え、同じ番号を再利用するときは同じ位置を使う。深度付きの RT を作るときは、DSV を持つ空き番号を優先して選ぶ。無ければ空き番号に DSV を新しく確保して覚える(DSV の増加はこの場合だけ)。
  - リサイズは「新しい RT を全部作ってから旧を解放する」(`PrepareResize` → `CommitResize`)ので、1 回のリサイズでは旧の番号を再利用できない。2 回目のリサイズで 1 回目の旧が再利用されるので、使うディスクリプタは最大で約 2 倍で止まる。
  - ディスクリプタの確保は全部、容量を超えたら失敗(false / 無効なハンドル)を返す形に揃える(今は `CreateBackBufferRTVs` にしか確認が無い)。CPU 専用ヒープなので、容量を RTV 512・DSV 256 に増やす(実際の使用数を E3b-1 の確認で記録する)。
- **描画コンテキストの保留状態を捨てる**: デバイスに「バインドの世代」(`bindingEpoch_`)を持たせ、`ResizeRenderTargets` / `ResizePresentSurface` / `ReleaseOffscreenRenderTarget` のたびに進める。`D3D12RenderContextImpl` は、**状態を設定する・描く・Dispatch する全ての入口で最初に呼んでいる `device_->BeginFrameIfNeeded()` の直後**(その入口自身の設定より前)に世代を確かめ、変わっていたら保留中の SRV・UAV・RTV・DSV と、PSO のキーに使う描画先のフォーマットを全部空にして、テーブルを張り直す印を立てる。リサイズはフレームの外で行うので、新しいフレームの最初の入口で一度だけ捨てられ、その後に各パスが設定した状態は消えない。入口ごとの呼び出しは 1 つの補助関数(`BeginFrameAndSyncBindings`)にまとめる。ImGui のユーザーテクスチャのスロットはフレームごとに作り直すので対象外。
- **開いたフレームの扱い**: 下の 2 関数の先頭で `frameOpen_` が true なら(初期化中にメインスレッドが開いたフレームなど)、記録済みのコマンドを閉じて実行し、完了を待って閉じた状態にしてから進める(`FlushOpenFrame`。フェンスの値とフレームの完了印を `Present` と同じく更新する)。失敗扱いにはしない(保留中は描画しないので、`Present` で閉じるのを待つと再試行が尽きるため)。
- **`ResizeMainTargets(reqW, reqH, outW, outH)`** = 下の `ResizePresentSurface` → `ResizeRenderTargets` の順。確定寸法は要求値と同じ(D3D12 はサーフェスの寸法に縛られない)。
- **`ResizePresentSurface(w, h)`**(新設。D3D12 の内部関数。E3b-2 で `IGraphicsDeviceImpl` に出す):
  1. `FlushOpenFrame()`(開いていれば)、`WaitForGPU()`。
  2. `backBuffers_[i]` を全部解放する(`ResizeBuffers` は、スワップチェーンのバッファへの参照が残っていると失敗する)。
  3. `swapChain_->ResizeBuffers(RENDER_TARGET_COUNT, w, h, DXGI_FORMAT_UNKNOWN, 作成時と同じフラグ)`(フォーマットは据え置き)。
  4. 失敗したら、旧のバッファで `CreateBackBuffers` と RTV の作成をやり直して(`ResizeBuffers` が失敗するとスワップチェーンは旧のバッファのまま)、false を返す。Engine が再試行する。
  5. 成功したら `CreateBackBuffers`、覚えた位置へ RTV を作り直す、`backBufferStates_` を PRESENT、`currentBackBufferIndex_` を取り直す、寸法のメンバを更新する。
- **`ResizeRenderTargets(reqW, reqH, outW, outH)`**(新設。同上):
  1. `FlushOpenFrame()`(開いていれば)、`WaitForGPU()`。
  2. 各メイン RT について、`D3D12ImGui` から旧リソースのエントリを外し、覚えた位置で `CreateOffscreen` し直す。失敗したら false(`IGraphicsDeviceImpl` の契約どおり、旧へ戻る保証はしない。Engine が再試行する)。
- **`ReleaseOffscreenRenderTarget(index)`**: `index < RENDER_TARGET_COUNT` と範囲外・二重解放は何もしない。`D3D12ImGui` からエントリを外し、RT を解放し、番号を空き番号へ入れる。呼ばれるのは `CommitResize` の中(`WaitForRenderIdle` の後)とパスの破棄なので、GPU は旧 RT を使っていない。
- **`CreateOffscreenRenderTarget`**: 空き番号があれば再利用する(Vulkan と同じ)。
- **`CopyToBackBuffer`**: 出力 RT とバックバッファの寸法が違うときは、コピーせずにログを 1 回出す(`CopyResource` の寸法違いはデバイスの削除につながるため)。E3b-1 では起きない(両方が同じ要求寸法で作られる)。E3b-2 でエディタの口を、エディタの合成(`CopyToBackBuffer` を使わない)のときに使う前提の安全網。
- **`D3D12ImGui::UnregisterOpaqueTexture(ID3D12Resource*)`**(新設): 表から外す。ImGui のテクスチャ ID(エントリのアドレス)は、パネルが毎フレーム取り直す契約(5.4 節)なので、外した ID を使い続ける者はいない。外すのは GPU の完了待ちの後。

**2.5 Engine の寸法の更新順(E3b-1)**: `AttemptRenderResize` が成功したら、その場で `screenWidth_` / `screenHeight_` も更新する。`SyncScreenSize()` で取れるデバイス(Metal / Vulkan。`GetSurfaceSize` を持つ)はそれを呼び直し、取れないデバイス(D3D12)は確定した提示面の寸法(E3b-1 では `ResizeMainTargets` の確定寸法)を入れる。リサイズしたフレームの `Update` から、`screenWidth_` と `renderWidth_` が両方とも新しい値になる。E3b-2 の提示面だけのリサイズの後も同じ。埋め込み起動は D3D12 のリサイズが起きない(`GetClientPixelSize` も `GetSurfaceSize` も false)ので、親に合わせた `screenWidth_` は変わらない。

**3. 提示面とレンダー解像度を分ける口(E3b-2)**

- **エンジン(`Engine`)**: `void SetRenderSizeOverride(uint32_t width, uint32_t height)` を足す(0×0 で解除)。「レンダー解像度をウィンドウ以外が決める」汎用の口で、エディタの名前を出さない。
  - 有効なのは、デバイスが `SupportsRenderResize()` かつ `SupportsSeparatePresentSize()`(新設、既定 false、D3D12 は true)のときだけ。対応していないデバイスでは無視して、ログを 1 回出す。
  - 有効な間、`HandleRenderResize` は 2 つを別々に扱う。
    - **提示面**: `GetClientPixelSize` の値が 2 フレーム続けて同じで、今の提示面と違えば、`WaitForRenderIdle` → `ResizePresentSurface` → `screenWidth_` を更新(パスは関係しない)。
    - **レンダー解像度**: 口に渡された値が 2 フレーム続けて同じで、今のレンダー解像度と違えば、今までの手順(`CanResizeRender` → `WaitForRenderIdle` → デバイス → `PrepareRenderResize` → `CommitRenderResize`)のうち、デバイスの段を `ResizeMainTargets` ではなく `ResizeRenderTargets` にする。
    - **1 回のリサイズ = 提示面とレンダー解像度の組**として扱う。検出は「(クライアントの寸法, 口の値)の組が 2 フレーム続けて同じで、今の(提示面, レンダー解像度)と違う」とき。保留中は、組の目標値(`pendingPresentW/H_`、`pendingRenderW/H_`)と、段ごとの完了印(`presentDone_`、`renderDeviceDone_`)を持つ。
    - 手順は `WaitForRenderIdle` → 提示面が違えば `ResizePresentSurface`(成功で `presentDone_`)→ レンダー解像度が違えば `CanResizeRender` の確認済みを前提に `ResizeRenderTargets`(成功で `renderDeviceDone_`)→ `PrepareRenderResize` → `CommitRenderResize` → 寸法の更新(2.5)。レンダー解像度が同じなら後ろの 3 段を飛ばす。
    - 失敗したら今の仕組み(200 ms 間隔、5 回で致命終了)で再試行し、完了印の立った段は飛ばす。今の `resizeDeviceDone_` を、上の 2 つの完了印に分ける形になる。口が無効の間は、今までどおり 1 つの寸法でデバイスの段を `ResizeMainTargets` にする(完了印は `renderDeviceDone_` を使う)。
    - 保留中に口の値やウィンドウの大きさが変わっても、保留中の組を最後まで終えてから、次のフレームで新しい組として検出する(今の動きと同じ)。
  - 解除されたら、次のフレームから今までどおり(ウィンドウの寸法で両方)に戻る。
- **デバイス(`IGraphicsDeviceImpl` / `GraphicsDevice`)**: `SupportsSeparatePresentSize()`(既定 false)、`ResizePresentSurface(w, h)`、`ResizeRenderTargets(reqW, reqH, outW, outH)`(どちらも既定 false)を足す。D3D12 は 2 で作った関数を出すだけ。
- **Editor(`EditorLayer`)**: ゲームビューの中身の領域の大きさ(`GetContentRegionAvail()` を整数に丸めたもの)を、毎フレーム `SetRenderSizeOverride` に渡す。
  - ゲームビューが閉じている・畳まれているときは渡さない(前の値のまま)。小さすぎる値(幅か高さが 64 未満)は 64 に切り上げる。このときだけ、絵がゲームビューより大きくなり、今の「アスペクト比を保って収める」表示で余白が出る(評価は 64 以上の大きさで行う)。
  - 表示は今の「アスペクト比を保って収める」のまま。リサイズが確定するまでの 2 フレームは旧い解像度の絵が縮小・拡大されて見え、確定後は領域にぴったり合う。入力の換算(E2b)は画像の矩形から計算しているので、そのまま合う。
  - 起動直後は 1280×720 で始まり、最初の 2 フレームでゲームビューの大きさへ変わる。

**4. ファイル別責務**

| ファイル | 段 | 変更 |
|---|---|---|
| `aqEngine/Platform/PlatformWin32.h/.cpp` | E3b-1 | `GetClientPixelSize` |
| `aqEngine/Graphics/D3D12/D3D12GraphicsDeviceImpl.h/.cpp` | E3b-1 / E3b-2 | `SupportsRenderResize`、`ResizeMainTargets`、`ResizePresentSurface`、`ResizeRenderTargets`、`ReleaseOffscreenRenderTarget`、空き番号とディスクリプタの位置の管理、容量の確認、`CopyToBackBuffer` の寸法の確認(E3b-1)。`SupportsSeparatePresentSize` と 2 関数の公開(E3b-2) |
| `aqEngine/Graphics/D3D12/D3D12ImGui.h/.cpp` | E3b-1 | `UnregisterOpaqueTexture` |
| `aqEngine/Graphics/IGraphicsDeviceImpl.h`、`GraphicsDevice.h/.cpp` | E3b-2 | `SupportsSeparatePresentSize`、`ResizePresentSurface`、`ResizeRenderTargets` |
| `aqEngine/Engine.h/.cpp` | E3b-1 / E3b-2 | リサイズ成功後の `screenWidth_` の更新(E3b-1)。`SetRenderSizeOverride`、組の検出と段ごとの完了印、`HandleRenderResize` の分岐(E3b-2) |
| `aqEngine/Graphics/D3D12/D3D12RenderContextImpl.h/.cpp` | E3b-1 | 入口の補助関数 `BeginFrameAndSyncBindings`(バインドの世代が変わったら保留状態を捨てる) |
| `Editor/EditorLayer.cpp` | E3b-2 | ゲームビューの大きさを渡す |
| `設計書/エンジン課題解消設計.md`、`01_レンダリング設計.md` 2.11 節、`06_ゲームアプリケーションコア設計.md` 1.2 節 | E3b-1 / E3b-2 | 恒久の仕様はこれらが一次資料なので、E3b-1 で「D3D12 と Win32 の `GetClientPixelSize` が対応済み(埋め込みは除く)」、E3b-2 で「提示面とレンダー解像度を分ける口と、組で扱うリサイズの手順」を、要点と本節へのリンクで更新する(D3D12 の実装の細部は本節に置き、書き写さない) |

### このフェーズで作らないもの

- D3D11 と Windows の Vulkan のリサイズ(P4-C の残り)。E3a で D3D11 / Vulkan のエディタ合成を作るときに合わせて決める。
- ゲーム側の 2 本目のパイプライン(ミニマップ)は固定寸法のまま(既存の方針)。
- `WM_SIZE` による即時の追従、全画面の切り替え。

### テスト方針

| 確認 | 方法 | 担当 |
|---|---|---|
| ビルド | Editor の 2 構成、Game の Debug × D3D11 / D3D12 / Vulkan と Release × D3D12。警告は基準の 17 件のまま | Claude |
| 起動と終了 | Editor.exe と Game.exe(D3D12)を起動して閉じ、終了コード 0、`[crash]` なし | Claude(起動前にユーザーへ確認) |
| Game.exe(D3D12)のリサイズ | ウィンドウの大きさを 10 回続けて変える(`MoveWindow` の自動操作)。毎回、`startup_timing.log` に「リサイズ完了: W×H」が出て W×H がクライアントの寸法と同じ、絵が伸びずに描かれる(スクリーンショット)、デバッグレイヤーのエラーが増えない、終了コード 0。最小化して戻しても落ちない | Claude(自動操作) |
| ディスクリプタ | 3 つの寸法(例: 1280×720 → 960×600 → 1600×900)を順に 4 周(計 12 回)リサイズし、各周の終わりに RTV / DSV / SRV ステージングの使用数をログに出す。2 周目・3 周目・4 周目の終わりの値が同じ(1 周目で頭打ち)で、容量の半分を超えない | Claude |
| D3D11 / Vulkan の Game.exe | ウィンドウの大きさを変えても今までどおり(固定寸法のまま伸びる)で落ちない | Claude(自動操作) |
| 失敗からの再試行(E3b-2) | コミットしない一時的な注入コードで、`ResizePresentSurface` と `PrepareRenderResize` をそれぞれ 1 回だけ失敗させ、ログで「リサイズ失敗(…)。再試行 1/5」の後に完了し、完了済みの段がやり直されない(提示面の作り直しが 1 回だけ)ことを確かめる | Claude |
| Editor.exe(E3b-2) | (a) ウィンドウの大きさを変えても、エディタの UI が崩れない(README の既知の課題が直る)、(b) ゲームビューの枠を 10 回続けて大きさを変えても、クラッシュ・デバッグレイヤーのエラー・絵の伸びが無く、確定後は絵がゲームビューにぴったり合う(黒帯なし。64 以上の大きさで)、(c) その後もゲームビューの中のクリック位置がずれない、(d) カメラのアスペクト比がビューに合う(球や円が楕円にならない) | Claude(自動操作)+ ユーザー(操作感) |

## 5.10 E3a の詳細(2026-11-20 追記。**保留**)

対象は ImGui の Multi-Viewport(`ImGuiConfigFlags_ViewportsEnable`。パネルをメインウィンドウの外へ出して別モニターへ移せる機能)。
D3D12 → D3D11 → Vulkan の順で対応する(11.2 節の既定方針どおり)。本節は **E3a-1(D3D12)** だけを詳細設計する。
D3D11 / Vulkan(E3a-2 / E3a-3)は現状調査だけ行い、決定は着手時に別途行う。

### 現状(調査結果)

| 項目 | 実コード |
|---|---|
| `ImGuiConfigFlags_ViewportsEnable` | どこにも立てていない(`ImGuiConfigFlags_DockingEnable` のみ `AppHost.cpp:415` でエディタ用コンテキストに設定)。 |
| win32 バックエンドの `PlatformHasViewports` | `ImGui_ImplWin32_Init`(`imgui_impl_win32.cpp`)は**無条件に** `ImGui_ImplWin32_InitMultiViewportSupport()` を呼び、`ImGuiBackendFlags_PlatformHasViewports` を立てる。`PlatformWin32::ImGuiInit()`(`PlatformWin32.cpp:207`)がエディタ用コンテキストに対して呼ばれた時点(`AppHost.cpp:420`)で、`ViewportsEnable` を立てていなくてもこのフラグは既に立っている。 |
| D3D12 公式バックエンドの Multi-Viewport | `ImGui_ImplDX12_Init` は `io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable` のときだけ内部で `ImGui_ImplDX12_InitMultiViewportSupport()` を呼ぶ(`imgui_impl_dx12.cpp`)。`D3D12ImGui.cpp` の `Init()`/`InitEditor()`(223〜312 行)は、追加ウィンドウの描画に使う `ImGui_ImplDX12_InitInfo::CommandQueue` を**既にメインの `commandQueue_` で渡している**(`impl->GetCommandQueue()`。248 行・299 行)。公式バックエンドが追加ウィンドウごとに自前のスワップチェーン・コマンドリストを持ち、このキューへ提出する作りなので、**フラグを立てる以外の初期化コードの追加は不要**と判断した。 |
| Multi-Viewport 発火位置の候補 | `D3D12GraphicsDeviceImpl::RenderEditorOverlay()`(327〜330 行)が既にエディタ用コンテキストを current にして `D3D12ImGui::Render(editorDrawData, commandList_)` を呼び、直後に `prevCtx` へ戻している。公式サンプルの定石(メインビューポート描画 → `UpdatePlatformWindows` + `RenderPlatformWindowsOnCurrentContext` → メインビューポートの Present)に合わせると、**この 2 関数の呼び出しは `D3D12ImGui::Render` の直後・`SetCurrentContext(prevCtx)` の直前**に収まる。`RenderThread.cpp` 側(`Present()` 呼び出し元)は変更不要。 |
| `io.MousePos` の座標系(`GameViewInput.cpp` の既存 TODO、設計書旧 760 行) | `GameViewInput::ForwardMousePos`(206 行)は `mouseX_ - prevState_.imageMinX` のように**差分**だけを使う。`mouseX_` は `editorContext->InputEventsQueue` の `MousePos` イベント、`prevState_.imageMinX` は `ImGui::GetItemRectMin()`(`EditorLayer.cpp` の `DrawGameView`)から取っており、**どちらも ImGui の「スクリーン座標」**(Multi-Viewport 無効時は便宜上クライアント座標、有効時は真のデスクトップ座標になる)。両方が同じ基準でシフトするので差分は変わらない。**実装の変更は不要と判断**(テストで確認する)。 |
| `IsAppActive()`(`PlatformWin32.cpp:160`) | `GetForegroundWindow()` のプロセス ID を見るだけなので、前面が(Multi-Viewport で生成された)別ネイティブウィンドウでも同一プロセスなら true になる。変更不要。 |
| 既存デバッグパネルの所属コンテキスト | `DebugUI::Register` 経由のパネル(Rendering / Profiling / UI / Prefab Editor / Level Editor 等)は `AppHost.cpp` の `BuildRenderCommands()` 内、**ゲーム用コンテキスト**で描画している(`EngineAssertMsg(ImGui::GetCurrentContext() == gameImGuiContext_, ...)` で担保)。ゲーム用コンテキストには win32 バックエンドを繋いでいない(`AppHost.cpp:403〜405` のコメント)ため、これらのパネルは Multi-Viewport で外へ出せない。 |
| シャットダウン順序 | `AppHost.cpp:683〜692` は「エディタの描画バックエンド(`ImGuiShutdownEditor`)→ win32 バックエンド(`platform->ImGuiShutdown()`)→ `DestroyContext`」の順で、公式サンプルの定石(レンダラ → プラットフォームの順)と一致している。Multi-Viewport 有効化後にこの順序で外に出したパネルのウィンドウ・スワップチェーンが正しく畳まれるかは実行して確認する(未検証)。 |
| Vulkan(E3a-3 の下調べ) | `VulkanImGui.cpp` の `CreateVkSurfaceUnsupported()` が `Platform_CreateVkSurface` の仮実装として登録済み(常に失敗を返す)。実装では `vkCreateWin32SurfaceKHR` 等で `ImGuiViewport::PlatformHandleRaw`(HWND)からサーフェスを作る必要がある。 |
| D3D11(E3a-2 の下調べ) | `D3D11GraphicsDeviceImpl.cpp` に `RenderEditorOverlay` の override が無く、エディタ UI をバックバッファへ合成する経路自体が未実装(`IGraphicsDeviceImpl` の既定 no-op のまま)。Multi-Viewport 以前にこの合成を作る必要がある。 |

### Codex レビューで判明した問題(2026-11-20。採用し、**E3a は保留**)

上の「決定」を一度書いて Codex レビューにかけたところ、重大な指摘 4 件が実コードで裏付けられ、
当初案は成立しないと判断した。指摘は全て実コードで確認済み(採用):

1. **存在しない API**: `ImGui::RenderPlatformWindowsOnCurrentContext()` はこの ImGui 版(v1.93.0-docking)に無い。
   正しくは `ImGui::RenderPlatformWindowsDefault()`(`imgui.h:1212`)。これ自体は単純な誤記で直せる。
2. **スレッド所有権の矛盾(本質的な問題)**: `UpdatePlatformWindows()` は追加ビューポートのぶん `CreateWindowExW` を
   同期で呼ぶ(`imgui_impl_win32.cpp` の `ImGui_ImplWin32_CreateWindow`)。Win32 はウィンドウを作ったスレッドが
   そのメッセージキューの所有者になるが、本エンジンのメッセージポンプ(`PlatformWin32::PumpEvents` の `PeekMessage`)は
   **メインスレッドでしか回っていない**(`06_ゲームアプリケーションコア設計.md` §1.2 どおり `RunFrameLoop` はメインスレッド)。
   上の案は `RenderEditorOverlay`(**レンダースレッド**。`RenderThread.cpp` の `Present()` 呼び出し元)から呼ぶ案だったので、
   外に出したウィンドウが所有スレッド不一致でメッセージを一切処理できない(閉じる・動かす・リサイズが効かない)。
3. **Game View のテクスチャ解決を素通りする**: Game View は本エンジン独自の「不透明テクスチャ ID」の仕組み
   (`D3D12ImGui::Render()` 内の `ResolveUserTextures` 相当。`D3D12ImGui.cpp`)で描いている。Multi-Viewport の
   追加ビューポートは公式バックエンドが直接 `ImGui_ImplDX12_RenderDrawData()` を呼ぶ(`imgui_impl_dx12.cpp` の
   `ImGui_ImplDX12_RenderWindow`)ため、この変換を通らない。Game View を外に出すと不正なテクスチャ ID のまま描画される。
4. **ゲーム本体の入力座標がずれる**: `GameViewInput.cpp` の ImGui イベント転送(差分計算)は Multi-Viewport でも
   成立すると判断したが(表の「`io.MousePos` の座標系」の行は撤回しない)、**別経路**である `InputManager::SetPointerView()`
   (`HID/Input.cpp`)は**メイン HWND のクライアント座標へ `ScreenToClient()` する前提**で、Game View が別ウィンドウに
   出ると合わなくなる。2 つの入力経路(ImGui 用とゲーム本体用)が別物であることを当初の決定は見落としていた。

加えて、調査の過程で**現状エディタ用コンテキストに実在するパネルは Game View 1 つだけ**と判明した
(Scene Hierarchy / Inspector は `SceneHierarchySystem` 等、ゲーム用コンテキストの ECS システムが描いており、
Game View のテクスチャに焼き込まれて見えているだけ。「既存デバッグパネルの所属コンテキスト」の行のとおり)。
Game View を安全に外へ出せない(上記 3・4)以上、**今 Multi-Viewport を有効にしても安全にドラッグアウトできるものが無い**。

### 決定: E3a は保留する(2026-11-20 ユーザー承認)

実装には進まない。D3D11 / Vulkan の調査結果(937〜936 行)は次回 E3a に戻るときのために残す。

**再開条件**: エディタ用コンテキストに Game View 以外の実在パネル(例: Scene Hierarchy / Inspector を本当に
エディタ側へ持ってくる。15 章の TODO、今は E7 以降)ができてから、改めて本節を書き直す。Game View 自体を
外に出す機能は、上記 3・4 の設計(テクスチャ解決の経路分岐、`SetPointerView` の対象ウィンドウ追従)を別途
行わない限り対象外とする。

### このフェーズで作らないもの

- 実装そのもの(保留のため)。上の「決定」のとおり、再開条件が満たされてから改めて設計する。

---

## 評価チェックリスト(旧 11.3 節)

### E3a Multi-Viewport

**保留**(2026-11-20。詳細は 5.10 節)。Codex レビューでスレッド所有権・Game View のテクスチャ解決・入力座標の
3 つの本質的な問題が見つかり、かつ現状エディタ用コンテキストに Game View 以外の実在パネルが無いため、
安全にドラッグアウトできるものが無いと判断した。再開条件(Game View 以外の実在パネルができてから)を
満たすまで、このチェックリストは着手しない。

### E3b ゲームビューの解像度追従

詳細は 5.9 節。

E3b-1(D3D12 の描画解像度のリサイズ)

- [x] D3D12 で `SupportsRenderResize` が true になり、Game.exe(D3D12)のウィンドウの大きさを 10 回続けて変えても、毎回その寸法で描き直され(ログの「リサイズ完了」の寸法 = クライアントの寸法、絵が伸びない)、デバッグレイヤーのエラーが増えず、終了コード 0。最小化から戻しても落ちない(Claude の自動操作。`MoveWindow` で 900x600・1300x750・700x500・1600x950・1000x700・1280x720・640x480・1920x1080・960x600・1366x768 の順に変え、毎回「リサイズ完了」のログの寸法とクライアント寸法が一致。最小化 → 復元後も生存。終了コード 0)
- [x] 3 つの寸法を 4 周リサイズして、RTV / DSV / SRV ステージングの使用数が 1 周目で頭打ちになり(2〜4 周目の終わりが同じ)、容量の半分を超えない(記録した値を結果に書く)。**結果**: 一時的な計測ログ(コミットしない)で確認。10 回の寸法変更のうち 1 回目で rtv=28/512 dsv=7/256 staging=65/4094、2 回目以降(3〜10 回目)は rtv=48/512(9.4%) dsv=8/256(3.1%) staging=105/4094(2.6%) で完全に頭打ち。容量の半分を大きく下回る
- [x] リサイズしたフレームから、ゲーム内 UI の当たり判定がずれない(`screenWidth_` が同じフレームで更新される)。一時的な計測ログで確認: リサイズ完了直後のフレームで `screenWidth_`/`screenHeight_` と `renderWidth_`/`renderHeight_` が常に一致(例: 884x561 → 884x561、1284x711 → 1284x711)
- [x] 埋め込み起動(`-parent-hwnd`。親とレンダー解像度が違う大きさ)で、レンダー解像度と `screenWidth_` が今までどおり(レンダーは初期化引数、screen は親のクライアント)で、ゲーム内 UI の当たり判定がずれない。.NET Form を親ウィンドウにして `-parent-hwnd=<10進>` で起動、親を 2 回リサイズ(600x450 → 1000x700)しても子は生存し、ログに「リサイズ開始」が一度も出ない(`GetClientPixelSize` が埋め込みで false を返す設計どおり、D3D12 のリサイズ経路に入らない)
- [x] D3D11 / Windows の Vulkan の Game.exe は今までどおり(固定寸法)で、リサイズしても落ちない。D3D11 は `MoveWindow` で 3 回リサイズしても「リサイズ」のログが出ず(`SupportsRenderResize` が false のまま)、終了コード 0。**Vulkan はこの PC に SDK が無くビルド不可のため未確認**([[vulkan-build-verification]] と同じ既知の制約。D3D11/Vulkan の内部実装そのものは変更していないので回帰のリスクは低いが、実機確認は別途必要)
- [x] 全構成がビルドでき、警告が増えていない。Editor_Debug/Editor_Release、Game の Debug/Release×D3D12、Debug×D3D11 がビルド成功、警告は重複除いて基準の 17 件のまま(すべて Physics 既存警告)。エンジン課題解消設計の P4-C、01_レンダリング設計 2.11 節、06_ゲームアプリケーションコア設計 1.2 節への反映は未実施(TODO として残す。次のコミットで行う)

E3b-2(提示面とレンダー解像度を分ける)

- [x] Editor.exe のウィンドウの大きさを変えても、エディタの UI が崩れない(README の既知の課題を解決済みにした)。Claude の自動操作(`MoveWindow` で 1100x700 → 1400x850 → 900x600、別途 10 回連続)+ `PrintWindow(PW_RENDERFULLCONTENT)` での撮影で確認。パネル(Scene Hierarchy / Inspector / ツールバー / Game View)が左上に寄らず正しいレイアウトのまま描画されている([[screenshot-d3d12-flip-limitation]] に撮影方法を追記)
- [x] ゲームビューの枠を 10 回続けて大きさを変えても、クラッシュ・デバッグレイヤーのエラーが無く、ログの「リサイズ完了(提示面/レンダー)」の寸法がクライアント寸法 − パネル分の差(常に同じオフセット)で一致。終了コード 0、最小化 → 復元後も生存。絵がゲームビューにぴったり合い、アスペクト比も合っている(黒帯・伸び・ガクつき無し。2026-11-20 ユーザー確認)
- [x] 大きさを変えた後も、ゲームビューの中のクリック位置と反応位置がずれない(2026-11-20 ユーザー確認。タイトル画面の操作で確認)
- [ ] 一時的な注入で提示面とパスの準備をそれぞれ 1 回失敗させても再試行で完了し、完了済みの段がやり直されない。**未実施**(コミットしない注入コードが必要な任意の頑健性テスト。設計レビューで手順は妥当と確認済みだが実行はしていない)
- [x] 全構成がビルドでき、警告が増えていない(Editor_Debug/Editor_Release、Game の Debug/Release×D3D12、Debug×D3D11。警告は基準の 17 件のまま。Vulkan はこの PC で検証不可)。01_レンダリング設計 2.11 節と 06_ゲームアプリケーションコア設計 1.2 節に、提示面とレンダー解像度を分ける口と組の手順を本書へのリンクで追記した
