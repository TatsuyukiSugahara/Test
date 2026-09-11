# スカイキューブ(スカイボックス)設計

> 対象コミット: feed446 / 最終更新: 2026-09-11(P1/P2 完了)

対象: `aqEngine/Rendering/Sky/`(新規)+ `Game/Assets/Shader/Skybox.fx`(新規)+
キューブマップ生成ツール。**全バックエンド(D3D11 / D3D12 / Vulkan / Metal)**で動かす。

一次資料の分担:
- **本書** … 方針決定・新規/変更ファイルの責務・フェーズ計画・チェックリスト。
- [01_レンダリング設計.md](01_レンダリング設計.md) … 描画パスの全体像。本書はそこへ 1 パス足す差分だけを書く。

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
| 挿す位置 | **ディファードライティングの直後・フォワードの直前**(`Renderer.cpp:106` の `SetRenderTargetWithDepthCommand` の直後) | その時点で「シーン RT(色)+ GBuffer0(深度)」がバインド済みなので、RT 設定を一切せずに済む。半透明より前なので順序も正しい |
| 深度 | **`DepthMode::ReadOnly`**(テスト有効・書き込み無し) | G-Buffer が書いた深度はライティング後も残っている(ライティングは `Disabled` で描くだけで書き込まない)。ジオメトリのある画素は深度テストで落ちる |
| 所有者 | **`SkyRenderer`(新規クラス)を `Renderer` が持つ** | `postProcessRenderer_` と同じ形。`DeferredRenderer` に載せると「ディファード専用機能」に見えるが、空はフォワードでも要る |
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
| `Game/Assets/Sky/SkyCube.dds` **(新規・生成物をコミット)** | 512x512 x6 面 / BGRA8 / ミップ無し。約 6MB |
| `Game/Assets/Shader/Skybox.fx` **(新規)** | フルスクリーン三角形。VS は NDC から**視線方向**を復元し、`z = 0.999999` を出す。PS は `TextureCube` をサンプルするだけ |
| `aqEngine/Rendering/Sky/SkyRenderer.{h,cpp}` **(新規)** | キューブマップ・シェーダ・サンプラの所有と `BuildCommandList`。**生成に失敗しても続行**(デカールと同じ作法。空が出ないだけ) |
| `aqEngine/Rendering/Sky/SkyCommand.{h,cpp}` **(新規)** | `IRenderCommand` 派生。`ParticleDrawCommand` と同じ構成(値コピーで保持 / `Execute` 1 本) |
| `aqEngine/Rendering/Renderer.{h,cpp}` | `skyRenderer_` の保持と、`BuildCommandList` / **`BuildCommandListViews` の両方**への 1 行追加 |
| `Game/Assets/Shader/shader_entries.txt` | `Skybox.fx VSMain vs` / `Skybox.fx PSMain ps` の 2 行を追加(**登録し忘れると Vulkan / Metal でシェーダが見つからない**。D3D は実行時コンパイルなので気付きにくい) |
| `Engine.vcxproj` / `.filters` | 新規 `.h/.cpp` の登録(`vs-project-files` スキル。**フィルターは実装前にユーザーへ確認する**) |

## 2. 視線方向の復元

VS で NDC(-1..1)から視線方向を作る。**逆ビュープロジェクション行列が要る**ので、
デカールと同じ流儀で **b0 を専用 CB(`SkyCB`)で置き換える**:

```hlsl
cbuffer SkyCB : register(b0)
{
    float4x4 invViewProjection;   // 平行移動を抜いた view * projection の逆行列
    float4   skyTint;             // rgb = 色調, a = 強度(既定 1,1,1,1)
};
```

**平行移動を抜く**のが肝。カメラ位置を含めたまま逆変換すると、空がカメラの移動に
合わせてずれる(無限遠にならない)。`view` の平行移動成分を 0 にしてから
`projection` と合成し、その逆行列を渡す。

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
      (この区間に半透明が出てこなかった)

### P3: 詰め

実装:
- 分割画面(`BuildCommandListViews`)への反映。
- ミニマップ(`OffscreenScenePass`)に空を出すかの判断。
- デバッグ UI のトグル(Rendering パネル)。

評価:
- [ ] 分割画面の全ビューで空が出る
- [ ] デバッグ UI から空の ON/OFF を切り替えられる
- [ ] 既存の見た目に回帰が無い(タイトル画面・UI・ポストプロセス)

---

### P2 で判明したこと(設計に無かった 2 点)

1. **`SkyRenderer` の生成は `OnInitialize()` ではなく `OnRegister()` から行う。**
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
   そのまま出すとトーンマップで少し沈む。強度(`skyTint.a`)で持ち上げて様子を見る。
4. **ミニマップに空を出すか**。`OffscreenScenePass` は独自のクリア色を持つ(暗い青)。
   真上から見下ろす画なので空はほぼ映らない。P3 で判断。

---

## 5. チェックポイント

- [ ] 既存の `.fx` が無改変(`Skybox.fx` の新規追加のみ)
- [ ] `shader_entries.txt` に 2 行登録されている
- [ ] 抽象IF(`IRenderContextImpl` / `IGraphicsDeviceImpl`)への追加が 0 本
- [ ] 新規 `.h/.cpp` が `.vcxproj` / `.filters` に登録されている
- [ ] 空の生成が失敗しても**起動できる**(デカールと同じ「失敗しても続行」)
- [ ] 設計と実装の食い違いを本書へ反映し、`対象コミット` を更新した
