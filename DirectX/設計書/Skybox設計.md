# スカイキューブ(スカイボックス)設計

> 対象コミット: feed446(実装)/ 文書更新: 92b6c54 以降 / 最終更新: 2026-11-09(P1/P2 完了。Codex レビューの 7 件を実コードで裏取りして反映。§4 課題 5 は Mobile の半透明が空でなくクリア色と合成される不具合。修正案は確認済み。2026-11-10 に Mobile の順序を入れ替えて適用)

対象: `aqEngine/Rendering/Sky/` + `aqEngine/Assets/Shader/Skybox.fx` + `aqEngine/Assets/Sky/DefaultSkyCube.dds` +
`aqEngine/Rendering/Pipeline/Passes/SkyPass.*` + キューブマップ生成ツール。
(設計時は `Game/Assets/` 配下だったが、`883ea1d` でエンジンアセットとして `aqEngine/Assets/` へ移った。)**全バックエンド(D3D11 / D3D12 / Vulkan / Metal)**で動かす。

一次資料の分担:
- **本書** … 方針決定・新規/変更ファイルの責務・フェーズ計画・チェックリスト。
- [01_レンダリング設計.md](01_レンダリング設計.md)、[レンダーパイプライン設計.md](レンダーパイプライン設計.md) … 描画パスの全体像とパス列方式。本書は Sky を 1 パス足す差分だけを書く。

> **§0.1〜§0.3 と §3 の P1/P2 は、パス列方式(`RenderPipeline`)導入前(2026-09-11)の記録。**
> 挿入位置・所有者の現行の姿は §0.2 の表の「現行」列と §1 を正とする。

---

## 0. 現状と方針

### 0.1 現状(調査で判明)

**「空」に相当する描画は存在しない。** ステージで空がグレーに見えるのは次の経路:

1. `Core/Application.cpp:493` がシーン RT を **白**(1,1,1,1)でクリア
2. G-Buffer は別 RT なのでシーン RT に触らない
3. ライティング PS(`PBRLighting.fx`)が `clip(pixelTag - 0.5)` で**背景ピクセルを破棄**する
   ため、シーン RT の白がそのまま残る
4. ポストプロセスの ACES トーンマップが白を **~0.8 のグレー**へ落とす

一方で**キューブマップのリソース層は 4 バックエンドとも実装済み**:
`Texture2DDesc::isCubemap`(`GraphicsTypes.h:198`)を `Resource.cpp:1685` が
DirectXTex の `IsCubemap()` から立て、D3D11 / D3D12 / Vulkan / Metal の各
`Create` が 6 面のキューブとして SRV を作る。**読み込み側に新規実装は要らない。**

### 0.2 確定した方針

| 項目 | 決定 | 理由 |
|---|---|---|
| 絵の出所 | **手続き的に生成した DDS キューブマップ**を同梱 | リポジトリにキューブマップが 1 本も無い。生成しておけば外部依存なしで見た目が出せ、後から市販の HDRI や自作画像へ差し替えられる |
| ファイル形式 | **DDS(BGRA8 / 非圧縮 / キューブマップ)** | `Resource.cpp` の既存経路がそのまま通る唯一の形式。6 面バラの PNG だと組み立てコードの新規実装が要る |
| 描画方法 | **フルスクリーン三角形 1 枚**(頂点バッファ無し) | 立方体メッシュを持つ必要がない。`DeferredLightingCommand.cpp:80-84` に `ctx.Draw(3, 0)` の前例がある |
| 挿す位置 | **ディファードライティングの直後・フォワードの直前**(設計時は `Renderer.cpp:106` の `SetRenderTargetWithDepthCommand` の直後) | その時点で「シーン RT(色)+ GBuffer0(深度)」がバインド済み。半透明より前なので順序も正しい。**現行(Standard)**: `PipelinePresets.cpp` の `Standard()` が Lighting の後・`ForwardPass` の前に `SkyPass` を追加する。ただし `SkyPass` は自分で RT をバインドし直す(パス間契約 3)ので「RT 設定を一切せずに済む」は成り立たない。Mobile も 2026-11-10 に同じ順へ揃えた(§4 課題 5) |
| 深度 | **`DepthMode::ReadOnly`**(テスト有効・書き込み無し) | G-Buffer が書いた深度はライティング後も残っている(ライティングは `Disabled` で描くだけで書き込まない)。ジオメトリのある画素は深度テストで落ちる |
| 所有者 | 設計時: `SkyRenderer` を `Renderer` が持つ。**現行: `SkyPass` が `SkyRenderer` を `unique_ptr` で所有し、`PipelinePresets` が `preset.enableSky` のとき生成して `SkyPass` を追加する** | 空はフォワードでも要るので `DeferredRenderer` には載せない。パス列方式への置き換え(`c60d345`)で `Renderer` から `SkyPass` へ移った。ゲームは `SkyPass::GetSkyRenderer()` から色調を触る |
| シェーダ | `Skybox.fx`(`VSMain` / `PSMain`)。`.fx` は新規 1 本のみ | 既存 `.fx` は無改変 |
| キューブマップのスロット | **t5** | t0-t3 = マテリアル、t4 = シャドウ、t8-t11 = GBuffer で埋まっている。t5-t7 が空き |

### 0.3 **最大の落とし穴**(調査で判明。実装前に必ず読むこと)

**深度比較が全バックエンドで `LESS` 固定**(`D3D11RenderContextImpl.cpp:26` /
`D3D12PipelineStateCache.cpp:101`。`DepthMode` の 3 値とも同じ比較関数を共有する)。
一方 **G-Buffer パスの深度クリア値は 1.0**(`DeferredRenderer.cpp:112`)。

したがって **空を `z = 1.0` で出すと `1.0 < 1.0` が偽になり、全画素が落ちて何も映らない。**

対処は VS の出力 z を **1.0 未満**にする(例 `0.999999`)。
`DepthMode` に `LESS_EQUAL` を足す案は 4 バックエンド全部に手を入れることになるので採らない。

> 既存のフルスクリーン VS(`PBRLighting.fx` の `VSMain`)は **`z = 0.0`** を出している。
> あれは深度無効パス専用なので、**そのまま流用してはいけない**(手前に出て全部隠す)。

---

## 1. 責務

| ファイル | 責務 |
|---|---|
| `Tools/SkyCubeGen/gen_skycube.py` **(新規)** | 手続き的な空のキューブマップを DDS で生成する。天頂/地平/地面色・太陽方向・雲の量を定数で持つ。再生成可能にしておくのが目的で、ビルドには組み込まない(生成物をコミットする) |
| `aqEngine/Assets/Sky/DefaultSkyCube.dds` **(新規・生成物をコミット。設計時は `Game/Assets/Sky/SkyCube.dds`)** | 512x512 x6 面 / BGRA8 / ミップ無し。約 6MB。既定の読み込み先は `RendererPreset::skyCubemapPath` |
| `aqEngine/Assets/Shader/Skybox.fx` **(新規。設計時は `Game/Assets/Shader/`)** | フルスクリーン三角形。VS は NDC から**視線方向**を復元し、`z = 0.999999` を出す。PS は `TextureCube` をサンプルするだけ |
| `aqEngine/Rendering/Sky/SkyRenderer.{h,cpp}` **(新規)** | キューブマップ・シェーダ・サンプラの所有と `BuildCommandList`。**生成に失敗しても続行**(デカールと同じ作法。空が出ないだけ) |
| `aqEngine/Rendering/Sky/SkyCommand.{h,cpp}` **(新規)** | `IRenderCommand` 派生。`ParticleDrawCommand` と同じ構成(値コピーで保持 / `Execute` 1 本) |
| `aqEngine/Rendering/Pipeline/Passes/SkyPass.{h,cpp}` | `SkyRenderer` を包むパス。`Scene` を読み書きし `Depth` は任意(`ReadsOptional`)。**自分で RT をバインドしてから描く**(Depth があれば `SetRenderTargetWithDepthCommand`、無ければ `SetRenderTargetCommand`)。スコープは既定の View(分割画面のビューごとに実行される) |
| `aqEngine/Rendering/Pipeline/PipelinePresets.cpp` | `Standard()` / `Mobile()` が `preset.enableSky` のとき `SkyRenderer::Create(preset.skyCubemapPath)` → `SkyPass` を追加(生成失敗なら追加しない) |
| `aqEngine/Assets/Shader/shader_entries.txt`(設計時は `Game/Assets/Shader/`) | `Skybox.fx VSMain vs` / `Skybox.fx PSMain ps` の 2 行を追加(**登録し忘れると Vulkan / Metal でシェーダが見つからない**。D3D は実行時コンパイルなので気付きにくい) |
| `Engine.vcxproj` / `.filters` | 新規 `.h/.cpp` の登録(`vs-project-files` スキル。**フィルターは実装前にユーザーへ確認する**) |

## 2. 視線方向の復元

VS で NDC(-1..1)から視線方向を作る。**逆ビュープロジェクション行列が要る**ので、
デカールと同じ流儀で **b0 を専用 CB(`SkyCB`)で置き換える**:

```hlsl
cbuffer SkyCB : register(b0)
{
    float4x4 invViewProjection;   // 平行移動を抜いた view * projection の逆行列
    float4   skyTint;             // rgb = 色調(既定 1,1,1,1)。a は強度ではない(下記)
};
```

**平行移動を抜く**のが肝。カメラ位置を含めたまま逆変換すると、空がカメラの移動に
合わせてずれる(無限遠にならない)。`view` の平行移動成分を 0 にしてから
`projection` と合成し、その逆行列を渡す。

> **`skyTint.a` は強度として効かない。** PS は `gSkyCube.Sample(...) * skyTint` と成分ごとに掛けるだけで、
> a は出力の alpha にしか掛からず(`Skybox.fx:53`)、描画は `BlendMode::Opaque`(`SkyCommand.cpp`)なので
> a を変えても明るさは変わらない。明るさは rgb で調整する。a を強度にするなら PS を
> `rgb *= tint.a` にする必要がある(未対応。§4 課題 3)。

CB は `fc.perDrawCBPool->Allocate()` から確保する(`ParticleDrawCommand` と同じ)。

## 3. フェーズ計画

### P1: キューブマップの生成とアセット化

実装:
- `Tools/SkyCubeGen/gen_skycube.py` と `Game/Assets/Sky/SkyCube.dds`。
- 生成物の妥当性をエンジン側で確認する(`SkyRenderer` の仮実装で
  `ResourceManager` からロードし、SRV が作れることをログで確認)。

評価:
- [ ] DDS ヘッダが cubemap + 全 6 面フラグを持ち、サイズが `128 + w*h*4*6` と一致する
- [ ] エンジンがロードして `isCubemap = true` の SRV を作れる(4 バックエンド中、
      Mac で確認できる Metal / Vulkan の 2 つで)
- [ ] 6 面の継ぎ目で色が飛ばない(面ごとの向きの取り違えが無い)

### P2: 描画

実装:
- `Skybox.fx` / `SkyRenderer` / `SkyCommand` / `Renderer` への挿入。

評価(2026-09-11):
- [x] ステージで**空が出る**(雲と太陽まで見える)
- [x] **ジオメトリが空に隠されない**(道路・草・キャラクターが手前に描かれる)
- [x] カメラを回しても空が正しく追従する
- [x] **カメラが移動しても空がずれない**。298 km/h でコースをかなり進んだ状態でも
      **太陽が画面右上のまま**、地平線も水平を保つ。平行移動が漏れていれば
      これだけ動けば空は大きく流れるか単色に潰れる
- [x] Metal / Vulkan の**両構成で空が描かれる**。Metal API Validation エラー 0 /
      VK エラー 0 / 終了コード 0 / Metal 由来の警告 0
- [ ] **同一カメラ状態での 2 構成の画素比較は未実施**(ゲーム進行が揃わないため)
- [ ] 半透明(パーティクル・海)が空の手前に正しく出る — **未確認**
      (この区間に半透明が出てこなかった)。Standard では Sky が Forward より前なので Forward 内の半透明も
      空の手前に出るはずだが、**Mobile は当初 Sky が Forward の後ろだった**(§4 課題 5。2026-11-10 に入れ替え済み)。
      確認対象は「パーティクル・海」に加えて「Forward 内の半透明」と「Mobile 構成」
- 補足: 上の「平行移動で空がずれない」は走行中の**目視**による確認で、条件(固定した回転・投影)と
  許容差を持たない。実装側は平行移動成分を 0 にしている(`SkyCommand.cpp`)ので設計としては正しいが、
  再現可能な検証にするなら「カメラ回転・投影を固定し位置だけ変えた 2 フレームの画素差が 0(または許容差内)」を
  条件にする(未実施)

### P3: 詰め

実装:
- 分割画面への反映。**`BuildCommandListViews` は現行の `Renderer` に無い**(パス列方式で
  `RenderPipeline::BuildViews` に置き換わった)。`SkyPass` は View スコープなので `BuildViews` が
  ビューごとに `Build` を呼び、追加実装なしで全ビューに出るはず。残るのは**確認だけ**(未実施)。
- ミニマップ(2 本目の `RenderPipeline`)に空を出すかの判断。
- デバッグ UI のトグル(Rendering パネル)。

評価:
- [ ] 分割画面の全ビューで空が出る
- [ ] デバッグ UI から空の ON/OFF を切り替えられる
- [ ] 既存の見た目に回帰が無い(タイトル画面・UI・ポストプロセス)

---

### P2 で判明したこと(設計に無かった 2 点)

1. **(P2 時点の制約。現在は解消済み)`SkyRenderer` の生成は `OnInitialize()` ではなく `OnRegister()` から行う。**
   現行は `AppHost::Initialize` が `RegisterEngineResourceBanks()` で既定バンクを `OnInitialize` より前に
   登録する(`AppHost.cpp`)ので、`OnInitialize` で `Load` しても落ちない。以下は当時の記録(行番号も当時のもの)。
   `SkyRenderer` はキューブマップを `ResourceManager` 経由で非同期ロードするが、
   **リソースバンクの登録は `OnRegister()` が行い、Engine は `Initialize()` →
   `Register()` の順で呼ぶ**(`Engine.cpp:95-105`)。`OnInitialize` で `Load` すると
   バンクが無く `EngineAssert` で落ちる。`DeferredRenderer` が `OnInitialize` で
   作れているのは `GraphicsDevice::CreateShader` しか使わないため。
2. **ミップマップは `Resource.cpp` が自動生成する。** 生成した DDS はミップ 1 枚だが、
   ロード後は `mips=10` になっていた(オープン課題 2 はこれで解消)。

### P1 の結果(2026-09-11)

- [x] DDS ヘッダが cubemap + 全 6 面フラグを持ち、サイズが `128 + w*h*4*6` と一致
- [x] エンジンがロードして `isCubemap` の SRV を作れる
      (ログ: `512x512 isCubemap=1 array=6 mips=10 srv=ok`)
- [x] **6 面の継ぎ目で色が飛ばない**。隣接面の境界で画素を突き合わせ、**最大差 1/255**
      (丸め誤差のみ = 面の向きの取り違えが無い)

生成ツールの試作時に**雲がフィルムグレインになる失敗**をした。実数座標を直接ハッシュして
補間が効いていなかったため。格子ベースの値ノイズ + fBm に直した(ツールにコメント済み)。

---

## 4. オープン課題

1. **太陽の向きがライティングと一致しない**。生成したキューブマップは太陽を
   焼き込んでいるが、シーンのディレクショナルライトとは無関係。
   絵として破綻しない範囲で合わせるか、生成ツールにライト方向を渡すかを決める。
   **P3 で扱う**(P1/P2 の見た目の判断には影響しない)。
2. ~~**ミップマップを持たせるか**~~ → **解決(P2)**。生成 DDS はミップ 1 枚だが、
   `Resource.cpp` が読み込み時に自動生成しており `mips=10` になっていた。
3. **HDR にするか**。メイン RT は `R16G16B16A16_Float` なので、LDR の DDS を
   そのまま出すとトーンマップで少し沈む。持ち上げるなら **rgb の色調で行う**(`skyTint.a` は効かない。§2)。
   a を強度にしたいなら PS を `rgb *= tint.a` に変える(判断待ち)。
4. **ミニマップに空を出すか**。ミニマップの列は `SkyPass` を積まず、呼び出し側が独自のクリア色を置く(暗い青)。
   真上から見下ろす画なので空はほぼ映らない。P3 で判断。

5. **Mobile 構成では Sky が Forward の後ろにあり、半透明が空でなくクリア色と合成される(不具合。実描画で確認済み)。**
   `PipelinePresets.cpp` の `Mobile()` は `ForwardPass` → `SkyPass` の順(Standard は Lighting → Sky → Forward)。
   Mobile は GBuffer を持たないので `PassResourceKeys::Depth` は無いが、**それは深度バッファが無いという意味ではない**。
   メイン RT は Metal / Vulkan とも深度付きで作られ(`MetalGraphicsDeviceImpl.mm` / `VulkanGraphicsDeviceImpl.cpp` の
   `CreateOffscreen(..., hasDepth=true)`)、`SetRenderTargetCommand` → `OMSetRenderTargets` はその RT 自身の深度を
   バインドする(`MetalRenderContextImpl.mm` / `VulkanRenderContextImpl.cpp`)。したがって Sky は `ReadOnly` の
   深度テストで Forward の不透明ジオメトリに落とされ、**ジオメトリが空に隠れることはない**はず
   (初版の本項は「深度なしで上塗りする」と書いていたが誤りで、Codex の再レビューで訂正した)。
   半透明については下の実描画の確認のとおり。
   **実描画での確認(2026-11-09、Mac / Metal / Sample。環境変数で切り替える一時変更を入れて撮影し、確認後に戻した)**:
   Sample の箱を `PipelineKind::Mobile` で描き、背景(空)・箱の位置の画素を比べた。

   | 構成 | 箱の画素(代表点) | 判定 |
   |---|---|---|
   | Standard / 不透明 | (76,172,205) | 基準 |
   | Mobile / 不透明 | (76,172,205) | **Standard と一致。ジオメトリは空に隠れない**(RT 自身の深度が効く) |
   | Standard / 半透明 α0.4 | (110,170,198) ほか | 基準(箱が空と合成される) |
   | Mobile / 半透明 α0.4 | (155,187,205) ほか | **不一致。箱の色にも空(150,168,180)にも無い明るさ** |
   | Mobile / 半透明(Sky → Forward に入れ替え) | (110,170,198) ほか | **Standard と全点一致** |

   結論:
   - **空に上塗りされることはない**(半透明も深度を書くので Sky は落ちる)。当初の懸念は外れ。
   - **ただし Mobile の半透明は、空ではなくクリア色(明るい背景)と合成される。** Sky が Forward の後ろにあり、
     半透明を描く時点では背景にまだ空が無いため。見た目が白っぽくなる**実際の不具合**。
   - **直し方は Mobile を `Sky → Forward` の順にすること**。入れ替えで Standard と一致することを確認済み。
     **2026-11-10 適用済み**(`PipelinePresets.cpp` の `Mobile()`。`レンダーパイプライン設計.md` の Mobile の列も更新)。
   - 確認していないこと: Vulkan 構成、Android / iOS 実機、Forward 内の半透明が SkeletalMesh の場合
     (今回は StaticMesh の箱に一時的に `translucent` を立てた)、Particle との前後関係。
   **Mobile 構成を選ぶのは `Auto` のとき Android / iOS**(`ForCurrentPlatform`)。
   `preset.pipeline = PipelineKind::Mobile` を明示すれば Mac / Windows でも選べるので、
   デスクトップで確認するならこの指定で足りる(実機が要らない)。

---

## 5. チェックポイント

- [ ] 既存の `.fx` が無改変(`Skybox.fx` の新規追加のみ)
- [ ] `aqEngine/Assets/Shader/shader_entries.txt` に 2 行登録されている
- [x] Mobile の順序を `Sky → Forward` に直し、Mobile でも半透明が空と合成される(課題 5。2026-11-10 適用。列 `ShadowPass > SkyPass > ForwardPass > ...` をログで確認。画素は 2026-11-09 の入れ替え検証で Standard と全点一致)
- [ ] 抽象IF(`IRenderContextImpl` / `IGraphicsDeviceImpl`)への追加が 0 本
- [ ] 新規 `.h/.cpp` が `.vcxproj` / `.filters` に登録されている
- [ ] 空の生成が失敗しても**起動できる**(デカールと同じ「失敗しても続行」)
- [ ] 設計と実装の食い違いを本書へ反映し、`対象コミット` を更新した
