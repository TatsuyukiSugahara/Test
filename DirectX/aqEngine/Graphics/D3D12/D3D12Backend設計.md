# DirectX 12 バックエンド 設計ドキュメント

対象: `aqEngine/Graphics/D3D12/`
前提: 既存の抽象レイヤ（Bridge パターン）を変更せず、D3D12 を `IGraphicsDeviceImpl` / `IRenderContextImpl` の実装として追加する。
本書は **設計のみ**。実装コードは含まない。

---

## 0. 設計の指針

- **抽象レイヤ（`IGraphicsDeviceImpl` / `IRenderContextImpl` / `I*Buffer` / `IShader` 等）は変更しない。** 呼び出し側（`UIRenderPipeline`、各 `*Command`、`Engine`）は一切書き換えずに DX12 へ切り替えられる状態をゴールとする。
- DX11 の **イミディエイト** モデルと DX12 の **コマンドリスト記録＋PSO** モデルの差は、すべて `D3D12RenderContextImpl` 内部に閉じ込める。
- まず「クリア画面が出る」→「メッシュ1枚」→「UI」→「フルパイプライン」と段階的に積む（§12 フェーズ計画）。

---

## 1. アーキテクチャ全体像

```
GraphicsDevice (抽象, 変更なし)
   └─ impl_ : IGraphicsDeviceImpl
        ├─ D3D11GraphicsDeviceImpl  (既存)
        └─ D3D12GraphicsDeviceImpl  (新規) ← デバイス/キュー/スワップチェーン/ヒープ所有

RenderContext (抽象, 変更なし)
   └─ impl_ : IRenderContextImpl
        ├─ D3D11RenderContextImpl   (既存)
        └─ D3D12RenderContextImpl   (新規) ← コマンドリスト記録 + PSO/ルートシグネチャ解決
```

DX12 で新たに必要となる「DX11 には存在しない」内部サブシステム（すべて `D3D12GraphicsDeviceImpl` が所有、または近傍に配置）:

| サブシステム | 役割 | 章 |
|---|---|---|
| `D3D12FrameContext`（フレームリングバッファ） | フレーム同時実行（frames-in-flight）管理 | §2 |
| `D3D12DescriptorHeapManager` | RTV/DSV/CBV-SRV-UAV/Sampler ヒープ確保 | §6 |
| `D3D12UploadAllocator` | 定数バッファ・動的VB/IB のアップロードヒープ・リング確保 | §7 |
| `D3D12PipelineStateCache` | (VS,PS,IL,Blend,Depth,RTフォーマット) → `ID3D12PipelineState` | §4 |
| `D3D12RootSignatureRegistry` | グラフィクス用/コンピュート用ルートシグネチャ | §5 |
| `D3D12ResourceStateTracker` | リソースの現在ステートとバリア発行 | §9 |

---

## 2. フレームライフサイクルと GPU 同期

### 2.1 frames-in-flight

- バックバッファ数 `RENDER_TARGET_COUNT = 2`（ヘッダ既定）。フレームインフライトも 2 とする（`FRAME_COUNT = 2`）。
- フレームごとに以下を1セット持つ `D3D12FrameContext` をリング化する:
  - `ID3D12CommandAllocator`（フレーム単位でしか Reset できないため必須）
  - アップロードヒープのリング領域（CB/動的VB/IB 用、§7）
  - シェーダー可視ディスクリプタヒープのリング領域（CBV/SRV、§6）
  - フェンス到達値 `fenceValue`

### 2.2 同期

- `ID3D12Fence` + `HANDLE`（イベント）。`commandQueue_->Signal(fence_, ++fenceValue_)`。
- フレーム開始時（`BeginFrame` 相当）: これから使う `D3D12FrameContext` の `fenceValue` まで GPU が到達済みか確認し、未達なら `WaitForSingleObject`。到達後に当該フレームの CommandAllocator / アップロードリング / ディスクリプタリングを Reset。
- `Present()` 末尾で `Signal` し、`fenceValue` を当該フレームに記録、`currentFrame = (currentFrame+1) % FRAME_COUNT`。

### 2.3 抽象レイヤとの接続点（重要）

現状の抽象には明示的な `BeginFrame/EndFrame` が無い。フレーム境界は **`SetupRenderContext()`（フレーム頭で呼ばれる想定）と `Present()`** で代用する。

> ⚠ 要確認: `SetupRenderContext()` がフレーム毎に呼ばれるか、起動時1回かを Engine 側で確認する（§13 オープン課題）。毎フレームでなければ、`IGraphicsDeviceImpl` に `BeginFrame()` を足すか、`SetupRenderContext` の DX12 実装内でフレーム回しを行う設計にする。**抽象IFへの最小追加は `BeginFrame()` 1本で済む見込み。**

---

## 3. コマンドリスト記録（`D3D12RenderContextImpl`）

`D3D12RenderContextImpl` は `ID3D12GraphicsCommandList*` を 1 本保持し、`IRenderContextImpl` の各メソッドを記録呼び出しへ変換する。DX11 は状態を即時設定するが、DX12 は **描画コール直前に PSO とルート引数をまとめて確定**する必要がある。そのため「保留ステート（pending state）」を内部に溜め、`Draw* / Dispatch` で flush する設計にする。

### 3.1 保留ステート

```
struct PendingGraphicsState
{
    IShader*           vs, *ps;             // → PSO キー
    入力レイアウト識別子               // → PSO キー（VS 由来）
    BlendMode, DepthMode               // → PSO キー
    PrimitiveTopology                  // → PSO キー（Type）＋ IASetPrimitiveTopology
    RT/DS フォーマット集合              // → PSO キー
    VBV, IBV                           // IASetVertexBuffers/IndexBuffer
    バインド済み CBV/SRV/Sampler        // ルート引数（ディスクリプタテーブル/ルートCBV）
    bool dirtyPSO, dirtyRootArgs;
};
```

### 3.2 `IRenderContextImpl` メソッド → DX12 対応表

| 抽象メソッド | DX12 実装方針 |
|---|---|
| `OMSetRenderTargets` / `OMSetMRTRenderTargets` | RTV ハンドル配列で `OMSetRenderTargets`。RT フォーマットを保留ステートへ記録（PSOキー）。対象リソースを RENDER_TARGET ステートへバリア。 |
| `OMSetRenderTargetWithDepth` | カラー RTV ＋ 深度 DSV をセット。 |
| `OMSetDepthMode` | `DepthMode` を保留（PSOキー）。DX12 は DSS が PSO 内包のため即時設定不可。 |
| `OMSetBlendMode` | `BlendMode` を保留（PSOキー）。 |
| `RSSetViewport` / `RSSetScissorRect` / `RSSetScissorEnabled` | `RSSetViewports` / `RSSetScissorRects`。Scissor 無効時は RT 全面の矩形を設定。 |
| `ClearRenderTargetView` / `ClearDepthBuffer` | `ClearRenderTargetView` / `ClearDepthStencilView`。 |
| `IASetVertexBuffer` | `D3D12_VERTEX_BUFFER_VIEW` を構築し `IASetVertexBuffers`。 |
| `IASetIndexBuffer` | バッファの `GetFormat()` を見て `DXGI_FORMAT_R16/R32_UINT` の `D3D12_INDEX_BUFFER_VIEW`。**抽象IFは前タスクでフォーマット保持済みのため変更不要。** |
| `IASetPrimitiveTopology` | `IASetPrimitiveTopology` ＋ PSO 用 Type を保留。 |
| `IASetInputLayout` | 入力レイアウト識別子を保留（PSOキー）。実体の `D3D12_INPUT_ELEMENT_DESC` は VS リフレクションから生成・キャッシュ。 |
| `VSSetShader` / `PSSetShader` | `pendingVS_/pendingPS_` に保持（既にヘッダにフィールドあり）。PSO キー更新。 |
| `VSSetConstantBuffer(slot)` / `PSSetConstantBuffer(slot)` | ルート CBV（`SetGraphicsRootConstantBufferView`）または CBV ディスクリプタテーブルへ書き込み。スロット→ルートパラメータ対応は §5。 |
| `PSSetShaderResource(slot)` | SRV をシェーダー可視ヒープへコピーし、ディスクリプタテーブルを設定。対象を PIXEL_SHADER_RESOURCE へバリア。 |
| `PsSetSampler(slot)` | サンプラーをサンプラーヒープへ確保しテーブル設定。 |
| `CSSetShader` 他 CS 系 | コンピュート用ルートシグネチャ＋コンピュート PSO（VS/PS とは別キャッシュ）。 |
| `Draw` / `DrawIndexed(count)` / `DrawIndexed(count, start)` | flush（PSO 解決→`SetPipelineState`、ルート引数設定）後 `DrawInstanced` / `DrawIndexedInstanced`。`DrawIndexed(count,start)` は前タスクで追加済み。 |
| `Dispatch` | コンピュート PSO/ルート確定後 `Dispatch`。 |
| `UpdateConstantBuffer` | アップロードリングに書き込み、CBV を更新（§7）。DX11 の `UpdateSubresource` 相当。 |
| `OMSetDepthOnlyTarget(Slice)` / `ClearDepthMap(Slice)` | シャドウ用。DSV のみ設定、RT 無し。配列スライスは DSV を slice 指定で生成。 |
| `PSUnsetShaderResource` / `PSUnsetShader` 等 | DX12 はアンバインド不要なことが多い。SRV ハザードはバリアで担保するため基本 no-op、ステート追跡のみ更新。 |

---

## 4. PSO（パイプラインステート）キャッシュ

DX11 の「VSSetShader/PSSetShader/OMSetBlendState/OMSetDepthStencilState/IASetInputLayout を個別設定」は、DX12 では 1 つの `ID3D12PipelineState` に集約される。

### 4.1 キー

```
struct PipelineStateKey
{
    void*             vsBlob;        // IShader の DXBC/DXIL ポインタ（同一性で識別）
    void*             psBlob;
    uint8_t           inputLayoutId; // 入力レイアウトの種類（VertexData / SkinnedVertexData / UIVertex …）
    BlendMode         blend;
    DepthMode         depth;
    PrimitiveTopology topoType;
    uint8_t           rtCount;
    DXGI_FORMAT       rtFormats[8];
    DXGI_FORMAT       dsFormat;
};
```
`std::unordered_map<PipelineStateKey, ComPtr<ID3D12PipelineState>>`（キーは memcmp ハッシュ）。

### 4.2 生成タイミング

- **遅延生成**: `Draw*` の flush 時にキーを組み、キャッシュ未ヒットなら `CreateGraphicsPipelineState`。初回のみコスト発生、以降はヒット。
- ラスタライザ/ブレンド/デプスの `D3D12_*_DESC` は `BlendMode`/`DepthMode` から変換するヘルパで生成（DX11 実装の RS/BS/DSS 対応表を踏襲）。

---

## 5. ルートシグネチャ

エンジンのバインドモデル（スロット番号）に合わせた **静的ルートシグネチャ** を用意する。`D3D12RenderContextImpl.h` の既存コメントにある想定をベースに、UI の b0/b1（CircleGauge/SdfText CB）と PS テクスチャ t0、サンプラー s0 を含める。

### 5.1 グラフィクス用（実装レイアウト）

| ルートパラメータ | 内容 | 対応する抽象呼び出し |
|---|---|---|
| [0..4] ルート CBV b0..b4 | VS/PS 定数（world/view/proj・ライティング・シャドウ・ボーン等）。ALL 可視のルートディスクリプタ（ヒープ不要） | `VSSetConstantBuffer(n)` / `PSSetConstantBuffer(n)` |
| [5] DescriptorTable SRV t0..t11 | テクスチャ（PIXEL 可視） | `PSSetShaderResource` |
| 静的サンプラー s0=Clamp / s1=Wrap | （ルートシグネチャ内蔵） | `PsSetSampler`（実体は no-op） |

> **SRV テーブルサイズは t0..t11（12個）**。エンジンが使う最大レジスタに合わせる: t0..t3 マテリアル（albedo/normal/specular/emissive）、t4 シャドウマップ配列、t8..t11 GBuffer。間の t5..t7 は未使用だが連続テーブルとして確保し、未バインドスロットは null SRV で埋める。
>
> b0 が VS と PS で意味が異なる箇所がある（UI は PS で b0 を使う）。**可視性 `D3D12_SHADER_VISIBILITY_ALL` の単一ルートシグネチャ**で吸収した。サンプラーは種類が少数のため静的サンプラー（s0/s1）に固定し、`ISamplerState`（`D3D12SamplerState`）は `SamplerDesc` を保持するだけの no-op wrapper とした。

### 5.2 コンピュート用

CS の CBV / SRV / UAV / Sampler 用に別ルートシグネチャ。`Dispatch` 経路専用。

---

## 6. ディスクリプタヒープ管理（`D3D12DescriptorHeapManager`）

| ヒープ | 種別 | 可視性 | 確保方針 |
|---|---|---|---|
| RTV | `RTV` | CPU only | スワップチェーン2枚＋オフスクリーンRT分を静的確保 |
| DSV | `DSV` | CPU only | 深度バッファ・シャドウマップ分を静的確保 |
| CBV/SRV/UAV | `CBV_SRV_UAV` | **shader visible** | フレームごとのリング。Draw 毎にテーブル分を線形確保し、フレーム頭で Reset |
| Sampler | `SAMPLER` | **shader visible** | サンプラー種が少数のため静的キャッシュ（`SamplerDesc`→ハンドル） |

- シェーダー可視ヒープは「ステージング（CPU可視・非シェーダー可視）に作った SRV/CBV を `CopyDescriptors` でリングへコピー → テーブル設定」という標準パターン。
- リングのサイズは 1 フレームの最大 Draw 数 × テーブル長で見積もる（UI バッチ＋メッシュ＋ライティング）。

---

## 7. アップロードヒープアロケータ（`D3D12UploadAllocator`）

DX11 の `UpdateSubresource`（DEFAULT ヒープ即時更新）と DYNAMIC バッファ（Map/Discard）を DX12 で再現する。

- フレームごとに大きな **アップロードヒープ（`D3D12_HEAP_TYPE_UPLOAD`）** を 1 本確保し、リニアアロケータでスライスを切り出す。
- **定数バッファ（`CreateConstantBuffer` / `UpdateConstantBuffer`）**: 256B アライン（`D3D12` CBV 要件）でスライス確保 → memcpy → CBV 生成。毎フレーム書き換え前提なのでアップロードヒープ常駐で十分。
- **動的VB/IB（`CreateDynamicVertexBuffer` / `CreateDynamicIndexBuffer`）**: アップロードヒープ上に確保し、`Update()` で直接 memcpy（永続 Map）。VBV/IBV はその GPU 仮想アドレスを指す。← **UI が依存する経路。前タスクで抽象IFは整備済み。**
- **静的VB/IB/テクスチャ**: DEFAULT ヒープに本体を作り、アップロードヒープ経由で `CopyBufferRegion` / `CopyTextureRegion`、COMMON→該当ステートへバリア。

---

## 8. レンダーターゲット / 深度

- **スワップチェーン**: `IDXGISwapChain3`（`CreateSwapChainForHwnd`、`DXGI_SWAP_EFFECT_FLIP_DISCARD`）。バックバッファ取得→RTV 生成。
- **`IRenderTarget` 実装（`D3D12RenderTarget`）**: カラーリソース＋RTV(＋SRV)。オフスクリーンRT・メインRTを統一的に扱う。`GetRenderTarget(index)`/`CreateOffscreenRenderTarget` を実装（現状 stub）。
- **`IDepthMap` 実装（`D3D12DepthMap`）**: 深度リソース＋DSV(＋SRV)。配列対応（シャドウ用 slice DSV）。
- **`CopyToBackBuffer`**: ポストプロセス確定RT → バックバッファへ `CopyResource`（または最終パスで直接バックバッファRTVへ描画）。

---

## 9. リソースステート追跡とバリア（`D3D12ResourceStateTracker`）

- 各 D3D12 リソースに「現在のステート」を持たせ、`OMSetRenderTargets`（→RENDER_TARGET）、`PSSetShaderResource`（→PIXEL_SHADER_RESOURCE）、`CopyToBackBuffer`（→COPY_SOURCE/DEST）、`Present`（→PRESENT）で必要な `ResourceBarrier` を自動発行。
- 既存コードは DX11 の暗黙ハザード管理（`PSUnsetShaderResource` で SRV を外す等）に依存している。DX12 ではこれらを **バリアに翻訳**する。`PSUnset*` は基本 no-op としつつ、次に RT として使う時にバリアで解決。

---

## 10. シェーダーコンパイル / 入力レイアウト

- 既存 `.fx` を `D3DCompile` で `vs_5_0/ps_5_0/cs_5_0` 等にコンパイル（DX12 でも DXBC は利用可）。`IShader` 実装（`D3D12Shader`）は blob を保持。将来 DXC/DXIL（`vs_6_0`）へ移行可能な抽象に。
- 入力レイアウトは VS リフレクション（`D3D12_INPUT_ELEMENT_DESC` 生成）。DX11 実装の `D3D11Shader::GetInputLayoutD3D11` と同様のリフレクション手順を流用。`inputLayoutId` で PSO キー化。

---

## 11. バックエンド選択とビルド配線

- **選択**: `Engine.cpp:137` の `GraphicsDevice::Create<D3D11GraphicsDeviceImpl>()` を `#ifdef ENGINE_GRAPHICS_D3D12` で分岐。プリプロセッサ定義で切り替え（DX11 を既定に残す）。
- **vcxproj**: `D3D12GraphicsDeviceImpl.cpp` / `D3D12RenderContextImpl.cpp` / 新規 D3D12 リソース `.cpp` を `<ClCompile>` 追加。
- **リンク**: `d3d12.lib` / `dxgi.lib` / `d3dcompiler.lib`（`#pragma comment(lib, ...)` か vcxproj）。
- **imgui**: `imgui_impl_dx11` → `imgui_impl_dx12` へ差し替え（`Application.cpp` の `dynamic_cast<D3D11GraphicsDeviceImpl*>` 部分も #ifdef 化）。ThirdParty に dx12 実装は既に存在（`imgui_impl_dx12.cpp`）。
- **静的アクセサ**: DX11 は `D3D11GraphicsDeviceImpl::GetStaticDevice()` を各リソースが参照。DX12 も `D3D12GraphicsDeviceImpl::GetStaticDevice()` を用意し、リソース生成クラスから参照する（パターン踏襲）。

---

## 12. フェーズ計画（段階的実装）

| Phase | 内容 | 到達点 | 状態 |
|---|---|---|---|
| **P0** | デバイス/キュー/アロケータ/コマンドリスト/スワップチェーン/RTV/フェンス/Present | 画面クリア色が出る | ✅ 実装済 (コンパイル/リンク検証済) |
| **P0.5** | D3D11 ソースの `#ifdef ENGINE_GRAPHICS_D3D11` ガード化＋バックエンド選択配線 | define 切替でビルド成立 | ✅ 実装済 (D3D12 define でビルド/リンク成功) |
| **P1a** | VB/IB/CB 生成（アップロードヒープ）＋シェーダ生成（コンパイル＋入力レイアウトreflection） | リソース生成層が揃う | ✅ 実装済 (コンパイル/リンク検証済) |
| **P1b** | ルートシグネチャ＋PSOキャッシュ＋RenderContext 記録（保留ステート＋flush）＋フレームbegin/end | 三角形・静的メッシュが出る | ✅ 実装済 (コンパイル/リンク検証済・**実機描画検証は未**) |
| **P2** | テクスチャ＋サンプラー（SRVディスクリプタヒープ／テーブル）＋ディスクリプタリング | テクスチャ付きメッシュ・**UI** が出る | ✅ 実装済 (コンパイル/リンク検証済・**実機描画検証は未**) |
| **P3** | オフスクリーンRT＋深度＋デプスモード／ブレンド／シザー＋ステート追跡 | Deferred/ポストプロセス相当 | ✅ 実装済 (コンパイル/リンク検証済・**実機描画検証は実施中**) |
| **P4** | コンピュート（Dispatch/UAV/SRV）＋シャドウ（slice DSV）＋imgui dx12 | フルパイプライン | ✅ **実機描画成功**（Deferred+シャドウ+海+ブルーム+UI）。imgui dx12 のみ未(no-op) |

各フェーズ後に DX11/DX12 を切替ビルドして回帰確認。

> **現状の制約 (P2 完了時点)**: P2 までで「テクスチャ＋SRVテーブル」記録パスを実装し、`ENGINE_GRAPHICS_D3D12` 構成で **Game.exe のリンクまで成功**を確認済み（D3D11 既定構成も緑）。ただし **実機 GPU での描画検証は未実施**。残る未実装スタブは `CreateDepthMap`（P3）・`CreateOffscreenRenderTarget`（P3）で、Deferred／ポストプロセス／シャドウ経路はこれらを使う初期化で停止する。前方（forward）テクスチャ付きメッシュ・UI 経路は P2 で揃った（描画ロジックは実装済み・実機未検証）。
>
> **P2 実装メモ**: SRV は「ステージングヒープ（CPU可視・非shader-visible、テクスチャ寿命の間 SRV を恒久格納）＋ shader-visible ring ヒープ（毎フレーム巻き戻し）」の標準2段構成。`PSSetShaderResource` は保留テーブルにステージング CPU ハンドルを記録するだけで、`Draw` 時に `FlushDescriptors()` が dirty なら ring から `SRV_TABLE_SIZE` 個連続スロットを確保し、各スロットを `CopyDescriptorsSimple`（未バインドは null SRV）→ `SetGraphicsRootDescriptorTable`。`Present` が毎フレーム `WaitForGPU` する同期実行のため ring は巻き戻し再利用で安全。コマンドリスト Reset でテーブルバインドが失われる問題は、device の **フレーム世代カウンタ**を RenderContext が監視し各フレーム先頭で強制再バインドして解決。
>
> **フレーム統合**: `SetupRenderContext` が起動時 1 回のため、フレーム境界は `RenderContext` の最初の記録呼び出しで `BeginFrameIfNeeded()`（コマンドリスト Reset・バックバッファ RT 遷移・クリア・ルートシグネチャ設定）が遅延発火し、`Present()` がクローズ／Execute／Present／フェンス待ちで確定する方式（抽象 IF への `BeginFrame()` 追加は不要だった）。

---

## 13. オープン課題 / 要確認

1. **フレーム境界**: `SetupRenderContext()` の呼び出し頻度（毎フレーム or 起動時1回）。毎フレームでなければ抽象IFに `BeginFrame()` を 1 本追加する必要あり。→ Engine のメインループを要確認。
2. **b0 の VS/PS 二義性**: UI は PS で b0/b1 を使い、メッシュは VS で b0 を使う。単一ルートシグネチャ（ALL可視）で吸収するか、用途別ルートシグネチャにするか。
3. **ディスクリプタリングのサイズ見積り**: 1 フレーム最大 Draw 数 × テーブル長。UI バッチの DrawRange 数が支配的。
4. **MRT フォーマット集合の取得**: PSO キーに RT フォーマットが必要。`OMSetMRTRenderTargets` 時に各 RT のフォーマットを保留ステートへ確実に伝播させる。
5. **`UpdateConstantBuffer` の寿命**: 同一フレーム内で同じ CB を複数回更新する箇所があるか（UI の CircleGauge/SdfText は DrawRange 毎更新）。ある場合、CB はリング確保で都度新スライスにするのが安全。
6. **抽象IFへの追加は最小限に**: 現状の見積りでは追加は `BeginFrame()`（課題1次第）のみ。それ以外は既存IFで成立する見込み。

---

## 14. 影響範囲まとめ（実装フェーズでの新規/変更ファイル）

**新規**
- `D3D12GraphicsDeviceImpl.cpp`（lifecycle/factory）
- `D3D12RenderContextImpl.cpp`（記録）
- `D3D12Buffers.{h,cpp}`（VB/IB/CB＋動的）
- `D3D12Resources.{h,cpp}`（SRV/UAV/Sampler/Texture）
- `D3D12RenderTarget.{h,cpp}` / `D3D12DepthMap.{h,cpp}`
- `D3D12PipelineStateCache.{h,cpp}` / `D3D12RootSignature.{h,cpp}`
- `D3D12DescriptorHeap.{h,cpp}` / `D3D12UploadAllocator.{h,cpp}` / `D3D12FrameContext.{h,cpp}`
- `D3D12Shader.{h,cpp}`

**変更**
- `Engine.cpp`（バックエンド選択 #ifdef）
- `Application.cpp`（imgui/dynamic_cast の #ifdef 化）
- `aqEngine/Engine.vcxproj`（ClCompile 追加・lib リンク）
- 抽象IF: 課題1の結論次第で `IGraphicsDeviceImpl` / `IRenderContextImpl` に `BeginFrame()` 追加の可能性

**変更不要（抽象越しに DX12 へ追従）**
- `UIRenderPipeline` / `UIBatchRenderCommand` / 各 `*Command` / `GraphicsDevice` / `RenderContext`
