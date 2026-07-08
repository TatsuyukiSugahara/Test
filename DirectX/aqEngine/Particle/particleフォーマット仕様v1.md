# `.particle` フォーマット仕様 v1

> 対象: aqEngine Particle モジュール / 最終更新: 2026-07-06
> 準拠実装: `Unity/Editor/AqParticleExporter.cs`(書き出し)、`aqEngine/Resource/ParticleSystemData.h/.cpp`(読み込み)
> 本書が正。実装と食い違う場合は実装を直すか本書を改訂する。

## 1. 概要

Unity ParticleSystem のエフェクトを aqEngine で再生するための独自 JSON フォーマット。

```
Unity ParticleSystem(プレハブ / シーンオブジェクト)
   │  AqParticleExporter.cs(Tools > aqEngine > Export ParticleSystem)
   ▼
xxx.particle(本仕様の JSON)
   │  ParticleLoader(SimpleJson でパース、カーブは LUT へ焼き込み)
   ▼
ParticleSystemData(イミュータブルなリソース)
```

- **1ファイル = 1エフェクト = Nエミッタ**。エクスポータは選択ルート配下の
  全 `ParticleSystem` を走査し、各エミッタにルートからの相対トランスフォームを持たせる。
- Niagara / Cascade からの取り込みは、本仕様で表現できるサブセットを手動移植する
  (フォーマット自体は Unity 非依存)。

## 2. ファイル規約

- UTF-8(BOM なし)テキスト。拡張子 `.particle`。
- キーの順序は不問。**未知のキーはローダーが無視する。**
  後方互換のある拡張は「フィールド追加」で行い、`version` は変えない。
- **破壊的変更時のみ `version` を +1** する。ローダーは `version == 1` のみ受理し、
  不一致はロードエラーとする。
- 小数点はピリオド固定(エクスポータは `InvariantCulture` で出力)。
- 省略されたフィールドは各節に記す既定値として扱う(モジュールごと省略も可)。

## 3. 座標系・単位・色

| 項目 | 規約 |
|---|---|
| 座標系 | 左手系・Y-up(Unity / aq で一致するため無変換) |
| 長さ | メートル |
| 時間 | 秒 |
| 角度 | **度**(エクスポータが Unity のラジアン系 API を変換済み) |
| 色 | RGBA float 0.0〜1.0。Unity インスペクタの sRGB 値をそのまま格納 |

将来別 DCC に対応する場合の座標変換点はエクスポータの `ConvertVector()` に集約してある
(現状は恒等変換)。

## 4. 共通型

### 4.1 ScalarValue(値の分布)

Unity の `MinMaxCurve` 互換。**スカラー値は本仕様内すべてこの型**で表す。

| mode | フィールド | 意味 |
|---|---|---|
| `"Constant"` | `constant` | 定数 |
| `"TwoConstants"` | `min`, `max` | 2定数間ランダム(粒子ごとの固定乱数 r で補間) |
| `"Curve"` | `multiplier`, `curve` | カーブ × multiplier |
| `"TwoCurves"` | `multiplier`, `curveMin`, `curveMax` | 2カーブ間ランダム(r で補間)× multiplier |

```json
{ "mode": "Constant", "constant": 1.0 }
{ "mode": "TwoConstants", "min": 0.5, "max": 1.2 }
{ "mode": "Curve", "multiplier": 2.0, "curve": { "keys": [ ... ] } }
{ "mode": "TwoCurves", "multiplier": 1.0, "curveMin": { "keys": [...] }, "curveMax": { "keys": [...] } }
```

### 4.2 Curve(Hermite カーブ)

```json
{ "keys": [
    { "time": 0.0, "value": 1.0, "inTangent": 0.0,  "outTangent": -0.5 },
    { "time": 1.0, "value": 0.0, "inTangent": -2.0, "outTangent": 0.0 }
] }
```

- `keys` は `time` 昇順(ローダーは安全のためソートし直す)。`time` は 0〜1 を推奨。
  範囲外の評価は端の値でクランプ。
- 評価は 3次 Hermite(Unity の `AnimationCurve` と同一の意味論)。
- **ステップキー**: `|inTangent|` または `|outTangent|` が `1e18` 以上の区間は
  ステップ(前キーの値を保持)として扱う。エクスポータは Unity の Infinity タンジェントを
  `±1e30` に置換して書き出す(JSON は Infinity を表現できないため)。
- 重み付きタンジェント(weightedMode)は非対応。エクスポータが警告し、
  通常タンジェントとして出力する。
- **ローダーはロード時に 64 サンプルの LUT へ焼き込む**(multiplier も焼き込み済み)。
  実行時評価は配列参照 + 線形補間のみ。

### 4.3 ColorValue(startColor 用)

Unity `MinMaxGradient` のサブセット。v1 は `Constant` / `TwoConstants` のみ。
Gradient 系モードはエクスポータが警告のうえ定数に近似する。

```json
{ "mode": "Constant", "color": [1.0, 0.8, 0.3, 1.0] }
{ "mode": "TwoConstants", "min": [1,1,1,1], "max": [1,0.5,0.5,1] }
```

### 4.4 Gradient(colorOverLifetime 用)

```json
{
  "colorKeys": [ { "time": 0.0, "color": [1.0, 0.9, 0.5] },
                 { "time": 1.0, "color": [0.6, 0.1, 0.0] } ],
  "alphaKeys": [ { "time": 0.0, "alpha": 1.0 },
                 { "time": 1.0, "alpha": 0.0 } ]
}
```

- キー間は線形補間(Unity の Blend モード相当)。Fixed モードは非対応
  (エクスポータが警告し Blend として出力)。
- キー省略時の既定: 色 = 白、アルファ = 1.0。
- ローダーは 64 サンプルの RGBA LUT へ焼き込む。

## 5. 時間軸と乱数の意味論

ScalarValue の `Evaluate(t, r)` に渡す `t` / `r` は項目によって異なる。
**ランタイム実装(P4)はこの表に従うこと。**

| 項目 | カーブ横軸 t | 乱数 r |
|---|---|---|
| `initial.*` | **生成時**のエミッタ正規化時間(経過時間 / duration) | 粒子ごと・項目ごとに生成時 1 回 |
| `gravityModifier` | エミッタ正規化時間(毎フレーム) | 粒子ごと 1 回 |
| `emission.rateOverTime` | エミッタ正規化時間(毎フレーム) | 使用非推奨(Constant / Curve を推奨) |
| `bursts[].count` | 0 固定 | 発火ごとに 1 回 |
| `*OverLifetime` / `textureSheetAnimation.frameOverTime` | **粒子正規化年齢**(経過寿命 / 初期寿命、0〜1) | 粒子ごと 1 回 |

乱数の実装指針: 粒子ごとに 32bit シードを 1 つ持ち、項目別の r は
`Hash(シード ^ 項目ID) / UINT32_MAX` で導出する(項目間の相関を防ぐ。
シード 1 個で全項目の TwoConstants / TwoCurves が再現できる)。

## 6. ルート構造

| キー | 型 | 必須 | 説明 |
|---|---|---|---|
| `version` | int | ○ | 本仕様は `1`。不一致はロードエラー |
| `name` | string | ○ | エフェクト名(通常はルートオブジェクト名) |
| `exporter` | object | — | ツール名・バージョン・Unity バージョン・日時。ローダーは無視 |
| `warnings` | string[] | — | エクスポート時警告。ローダーは読み込んでツール表示用に保持 |
| `emitters` | object[] | ○ | エミッタ配列(1 件以上)。順序 = 描画登録順 |

## 7. エミッタ

### 7.1 基本フィールド

| キー | 型 | 既定値 | 説明 |
|---|---|---|---|
| `name` | string | `"emitter"` | エミッタ名(警告メッセージ等に使用) |
| `localPosition` | [x,y,z] | [0,0,0] | エフェクトルートからの相対位置 |
| `localRotationEuler` | [x,y,z] | [0,0,0] | 同・相対回転(度、Unity の回転順) |
| `duration` | float | 5.0 | 1 サイクルの長さ(秒)。0 以下はローダーが補正 |
| `looping` | bool | true | ループ再生 |
| `startDelay` | float | 0.0 | 再生開始までの遅延(秒) |
| `maxParticles` | int | 1000 | 同時最大粒子数(SoA バッファはこの数で確保) |
| `simulationSpace` | string | `"Local"` | `"Local"` / `"World"` |
| `gravityModifier` | ScalarValue | 0 | 重力係数(9.81 m/s² に乗算。負値で上昇) |

Transform スケールは v1 非対応(エクスポータが警告)。

### 7.2 `initial`(生成時の初期値)

| キー | 型 | 既定値 | 説明 |
|---|---|---|---|
| `lifetime` | ScalarValue | 5.0 | 寿命(秒) |
| `speed` | ScalarValue | 5.0 | 初速(m/s、shape の方向に乗算) |
| `size` | ScalarValue | 1.0 | サイズ(m、ビルボード一辺 / メッシュスケール) |
| `size3D` | object | — | **任意**。軸別サイズ `{ "x", "y", "z" }`(各 ScalarValue)。Unity の 3D Start Size。指定時は `size` より優先(ビルボードは x=横/y=縦、メッシュは xyz スケール)。ビーム板・円筒の非一様スケールに使う |
| `rotation` | ScalarValue | 0.0 | ロール角(度)。ビルボードの回転 |
| `rotation3D` | object | — | **任意**。軸別回転 `{ "x", "y", "z" }`(各 ScalarValue、度)。Unity の 3D Start Rotation。指定時は `rotation` より優先。ビルボードは z のみ使用、メッシュは ZXY 順(Unity と同順)で適用。板/円筒の向き付けに使う |
| `color` | ColorValue | 白 | 初期色 |

### 7.3 `emission`

| キー | 型 | 既定値 | 説明 |
|---|---|---|---|
| `enabled` | bool | true | |
| `rateOverTime` | ScalarValue | 10.0 | 秒あたり放出数 |
| `bursts` | object[] | [] | バースト定義 |

バースト: `{ "time", "count"(ScalarValue), "cycles", "interval", "probability" }`。
`cycles = 0` はループ中の無限リピート。ループ時の時刻交差判定は折り返しを跨ぐ
フレームでの発火漏れに注意(実装メモ)。

### 7.4 `shape`

| キー | 型 | 既定値 | 使用する type |
|---|---|---|---|
| `enabled` | bool | true | 無効時は原点から放射状 |
| `type` | string | `"Cone"` | `"Cone"` / `"Sphere"` / `"Box"` / `"Circle"` |
| `angle` | float | 25.0 | Cone(開き角、度) |
| `radius` | float | 1.0 | Cone / Sphere / Circle |
| `radiusThickness` | float | 1.0 | 0 = 表面のみ、1 = 全体から生成 |
| `boxSize` | [x,y,z] | [1,1,1] | Box |
| `position` | [x,y,z] | [0,0,0] | エミッタ内オフセット |
| `rotationEuler` | [x,y,z] | [0,0,0] | 形状の回転(度) |

Hemisphere は Sphere に、その他の形状も警告のうえ Sphere に近似される。
arc(部分円弧)は v1 非対応。

### 7.5 `velocityOverLifetime`

`enabled`, `linearX/Y/Z`(ScalarValue、m/s), `space`(`"Local"` / `"World"`)。
orbital / radial / speedModifier は v1 非対応(警告)。

### 7.6 `colorOverLifetime`

`enabled`, `gradient`(Gradient)。initial.color に**乗算**する。

### 7.7 `sizeOverLifetime`

`enabled`, `size`(ScalarValue)。initial.size に乗算する**倍率**。
**任意** `size3D`:軸別の倍率カーブ `{ "x", "y", "z" }`(各 ScalarValue)。Unity の Separate Axes。
指定時は `size` より優先(ビーム板が寿命中に細く/短くなる表現に使う)。

### 7.8 `rotationOverLifetime`

`enabled`, `angularVelocity`(ScalarValue、**度/秒**)。

### 7.9 `textureSheetAnimation`(フリップブック)

| キー | 型 | 既定値 | 説明 |
|---|---|---|---|
| `enabled` | bool | false | |
| `tilesX` / `tilesY` | int | 1 | 分割数 |
| `frameOverTime` | ScalarValue | 0 | **0〜1 正規化**。実フレーム = 値 × 総フレーム数 × cycles |
| `startFrame` | ScalarValue | 0 | 開始フレーム(フレーム単位) |
| `cycles` | int | 1 | 寿命中の再生周回数 |

UV 計算: `frame = floor(startFrame + frameOverTime(t) * tilesX * tilesY * cycles) % 総フレーム数`、
左上原点で行優先。Single Row / Speed / FPS モードは v1 非対応。

### 7.10 `renderer`

| キー | 型 | 既定値 | 説明 |
|---|---|---|---|
| `type` | string | `"Billboard"` | `"Billboard"` / `"StretchedBillboard"` / `"Mesh"` |
| `texture` | string | `""` | テクスチャパス(§7.10.1 の解決規約参照) |
| `mesh` | string | `""` | Mesh 時のメッシュパス(.tkm 等)。未指定なら Billboard に降格 |
| `blendMode` | string | `"Alpha"` | `"Alpha"` / `"Additive"`(Additive はソート不要) |
| `sortMode` | string | `"None"` | `"None"` / `"ByDistance"` |
| `lengthScale` | float | 2.0 | StretchedBillboard: 長さ係数 |
| `speedScale` | float | 0.0 | StretchedBillboard: 速度比例の伸び |

#### 7.10.1 texture / mesh パスの解決規約

ローダー/ランタイムは `texture` を次で解決する(`mesh` も同方針予定):

- **`"Assets/…"` 始まり** → コンテンツルート(`Game/`)相対としてそのまま(Unity パス互換)。
- **それ以外(相対)** → **その `.particle` ファイルのあるフォルダ相対**。エクスポータ(`AqParticleExporter`)は
  参照テクスチャを出力先の `Textures/` へフラットコピーし、`texture` を `"Textures/<file>"` に書き換える。
  よって `.particle` + `Textures/` をひとまとめに `Game/Assets/…` 下へ置けばそのまま解決される(自己完結バンドル)。

Mesh の実描画は P6(それまでは Billboard 降格)だが、**データ互換のため v1 から定義**する。

## 8. 非対応機能とエクスポータの警告方針

エクスポータは非対応機能を検出したら**黙って落とさず**、コンソール警告 + JSON の
`warnings` 配列の両方に記録する(「変換したのに見た目が違う」の調査コスト削減のため)。

v1 非対応(モジュールごと無視): Sub Emitters / Trails / Noise / Collision / Triggers /
Limit Velocity / Inherit Velocity / Force over Lifetime / External Forces / Lights /
Custom Data / Size・Color・Rotation by Speed / Lifetime by Emitter Speed。

v1 近似・部分対応: Separate Axes(X または Z 軸のみ使用)/ startColor の Gradient 系モード
(定数近似)/ TwoGradients(max 側のみ)/ Rate over Distance(無視)/ Shape の arc・
Randomize Direction(無視)/ Scaling Mode ≠ Local(Local 扱い)/ Custom Simulation Space
(Local 扱い)。

## 9. サンプル

`Game/Assets/Particle/FX_Explosion.particle` を参照(Sparks + Smoke の 2 エミッタ。Curve /
TwoConstants / Gradient / バースト / フリップブックを網羅したローダーテスト用データ)。
