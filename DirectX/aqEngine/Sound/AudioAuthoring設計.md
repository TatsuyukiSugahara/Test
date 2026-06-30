# オーディオオーサリング層 設計ドキュメント（Wwise ライク・データ駆動）

対象: `aqEngine/Sound/Authoring/`（新規）
前提: 既存の `SoundEngine`（voice / source / stream / bus / fade / 3D = `Mixer3D`）を**低レベル実行エンジン**として温存し、その上に**データ駆動のオーサリング層**を載せる。`SoundEngine` の抽象（`ISoundBackend` / `ISoundVoice`）は一切変更しない。
本書は **設計のみ**。実装コードは含まない（スキーマ/シグネチャ例は方針明確化用）。
姉妹文書: [Sound設計.md](Sound設計.md)（低レベルエンジンの設計）。

オーサリング形式: **JSON**（`Util/SimpleJson`、UI ドキュメントと同流儀）。将来 **ImGui ベースの専用オーサリングエディタ**を載せる前提でデータモデルを設計する。

---

## 0. 目的と設計原則

- **責務分離（最重要）**: プログラマは **イベント名を投げるだけ**（`PostEvent("Player_Footstep", entity)`）。アセット・音量・ランダム・3D・ダッキング等の挙動は**サウンドエンジニアが JSON で構成**する。再ビルド不要・ホットリロード可能にする。
- **「Kind」中心の省設定**: サウンドの種別プリセット `Kind` を 1 つ付けるだけで、バス/ポリフォニー/優先度/減衰/クールダウン/ダッキング等の既定が自動適用される。設定コストを最小化する（§4）。
- **既存資産の再利用**: 解決後の最終再生はすべて `SoundEngine` の API（`Play` / `CreateSource` / `OpenStream` / `FadeBus` 等）に落とす。Android(Oboe) でもオーサリング層はそのまま動く（プラットフォーム差は SoundEngine 内に閉じる）。
- **エディタ志向のデータモデル**: 名前は CRC32 で ID 化（実行時は ID、編集時は名前）。全要素は GUID/名前参照で疎結合にし、将来 GUI でノード編集できる構造にする。
- **段階導入**: まず「Event + Kind + Sound + Random/Switch」で実用最小を作り、RTPC / State / ダッキング / Blend を順次足す（§16）。

---

## 1. アーキテクチャ全体像

```
Game code:  aq::audio::PostEvent("Enemy_Hit", entityId)         ← 名前 or ID だけ
                    │
                    ▼
AudioDirector（新規・データ駆動層, シングルトン）
  ├─ AudioBank        … JSON 定義の保持（Events / Objects / Kinds / Buses / Switches / States / RTPC / Attenuation / Ducking）
  ├─ ObjectResolver   … Random / Sequence / Switch / Blend を解決し、最終 clip と再生パラメータを決める
  ├─ GameSyncState    … Switch / State / RTPC の現在値（グローバル + GameObject 単位）
  ├─ VoiceLimiter     … Kind 別ポリフォニー / 優先度 / virtualization / ボイススティール / クールダウン
  ├─ Ducking          … バス自動ダッキング（side-chain 風）
  └─ EventInstance[]  … 再生中イベントの実体（PlayingId で参照）
                    │  低レベル呼び出し（抽象は不変）
                    ▼
SoundEngine（既存）:  Play / CreateSource / OpenStream / SetVolume / SetPitch / Fade* / Bus / Mixer3D
```

オーサリング層は `SoundEngine` の**クライアント**であり、`SoundEngine` はオーサリング層を知らない（依存は一方向）。

---

## 2. 概念モデル（用語）

| 用語 | 意味 | Wwise の対応 |
|---|---|---|
| **Event** | ゲームが投げる名前付きトリガ。Action の列を持つ | Event |
| **Action** | Event 内の 1 操作（Play/Stop/Pause/SetSwitch…） | Event Action |
| **SoundObject** | 再生対象（単音 or コンテナのツリー） | Actor-Mixer 階層の Sound/Container |
| **Container** | 子を束ねる SoundObject（Random/Sequence/Switch/Blend） | 各 Container |
| **Kind** | サウンドの種別プリセット（既定の束）。本設計の中心 | （独自。Wwise の "Inclusion"/共有設定の簡易版） |
| **Bus** | ミキシンググループ（SE/Voice/BGM…）。階層・ダッキング | Bus |
| **Switch** | GameObject 単位の離散状態（地面=Grass/Wood…） | Switch |
| **State** | グローバルな状態（Music=Explore/Combat…） | State |
| **RTPC** | 連続パラメータ（速度・体力…）→ プロパティへカーブ写像 | RTPC / Game Parameter |
| **Attenuation** | 共有可能な 3D 距離減衰プリセット（+将来 spread/LPF） | Attenuation ShareSet |
| **Ducking** | あるバス再生中に別バスを自動で下げる | Auto-ducking |
| **GameObject** | 3D 音の発生主体（エンティティ）。位置/速度を登録 | Game Object |
| **Bank** | 定義（とアセット参照）のパッケージ単位（JSON ファイル） | SoundBank |
| **PlayingId** | PostEvent の戻り値。個別停止/操作に使う | Playing ID |

---

## 3. データスキーマ（JSON）

トップレベル = **AudioBank ファイル**（例: `Assets/Audio/Main.audiobank.json`）。複数 Bank に分割可能。
全要素は**名前キー**で参照する。`clip` 等の必須項目以外は省略でき、Kind と既定値が埋める。

### 3.1 全体構造
```json
{
  "version": 1,
  "kinds":        { "<KindName>":     { ...Kind... } },
  "buses":        { "<BusName>":      { ...Bus... } },
  "attenuations": { "<AttenName>":    { ...Attenuation... } },
  "switches":     { "<GroupName>":    ["Value1","Value2", ...] },
  "states":       { "<GroupName>":    ["Value1","Value2", ...] },
  "rtpc":         { "<ParamName>":    { "min":0, "max":1, "default":0 } },
  "ducking":      { "<DuckName>":     { ...Ducking... } },
  "objects":      { "<ObjectName>":   { ...SoundObject... } },
  "events":       { "<EventName>":    [ ...Action... ] }
}
```

### 3.2 Kind（§4 に詳細）
```json
"Footstep": {
  "bus": "SE",
  "spatialMode": "auto",          // auto|2d|3d（auto = GameObject 指定時のみ 3D）
  "maxVoices": 8,                 // 同時発音上限
  "voiceLimitScope": "global",    // global|gameObject（敵ごとの鳴りすぎ制御等）
  "priority": 30,                 // 0-100。枯渇時に残す優先度
  "voiceStealing": "oldest",      // none|oldest|quietest|lowestPriority|reject
  "cooldownMs": 40,               // 最小再発火間隔
  "cooldownScope": "gameObject",  // global|gameObject|object
  "loadPolicy": "auto",           // preload|stream|auto（streaming boolean の後継）
  "attenuation": "DefaultSE",     // 既定の 3D 減衰プリセット
  "duckGroup": null,              // この Kind をトリガにする Ducking 名（あれば）
  "volumeDb": 0,                  // Kind 既定の補正
  "pitch": 1.0,
  "fadeInMs": 0,                  // Kind 既定のフェードイン
  "fadeOutMs": 0,                 // Kind 既定のフェードアウト
  "stopOnGameObjectDestroy": true // 3D emitter 破棄時に停止
  // ※ virtualize（実ボイスなしでカーソルだけ進める仮想化）は将来扱い（§8 注記）
}
```

### 3.3 Bus
```json
"BGM":   { "parent": "Master", "volumeDb": 0 },
"SE":    { "parent": "Master", "volumeDb": 0 },
"Voice": { "parent": "Master", "volumeDb": 0 }
```
※ 現 `SoundEngine` のバスは固定 4 種（Master/BGM/SE/Voice）。本設計はまずこの 4 種に写像し、将来 `SoundEngine` 側を可変バス階層へ拡張する余地を残す（§12 注記）。

### 3.4 Attenuation（3D 減衰プリセット）
```json
"DefaultSE": {
  "model": "Inverse",            // None|Linear|Inverse|Exponential（Mixer3D に対応）
  "minDistance": 1.0,
  "maxDistance": 40.0,
  "curve": null,                 // 将来: 距離→ゲインの任意カーブ点列
  "spreadDeg": 0,                // 将来: 近接で音像を広げる
  "lowpassByDistance": null      // 将来: 距離でローパス
}
```

### 3.5 SoundObject / Container
共通フィールド: `type`, `kind`(省略時は親/既定), `bus`(override), `attenuation`(override), `volumeDb`, `pitch`, `loop`, `volumeRandomDb`(=[min,max]), `pitchRandom`(=[min,max])。

- **Sound**（単音）
```json
{ "type":"Sound", "kind":"BGM", "clip":"bgm/title.m4a", "loop":true }
```
**子（child）の表記は型付き union に統一する**（エディタ/バリデーション容易化, Medium）:
```json
{ "clip": "sfx/foot_grass1.wav" }   // 単音 clip への短縮（内部で type:"Sound" 相当）
{ "ref":  "Footstep_Common" }        // 既存 SoundObject 名への参照
{ "type": "Random", ... }            // インラインの SoundObject 定義
```
`"sfx/foo.wav"` のような裸文字列は受け付けない（clip か ref かを必ず明示）。

- **Random**（毎回ランダムに 1 つ）
```json
{ "type":"Random", "kind":"Footstep",
  "children":[ {"clip":"sfx/foot_grass1.wav"}, {"clip":"sfx/foot_grass2.wav"}, {"clip":"sfx/foot_grass3.wav"} ],
  "weights":[1,1,1], "avoidRepeat":1,           // 直近 N 個は再選択しない
  "volumeRandomDb":[-2,0], "pitchRandom":[0.95,1.05] }
```
- **Sequence**（順番に再生）
```json
{ "type":"Sequence", "children":[ {"clip":"..."}, {"ref":"..."} ], "loop":false }
```
- **Switch**（Switch 値で子を選ぶ）。`default` は **map の外**に出して値名衝突を避ける。
```json
{ "type":"Switch", "switchGroup":"Surface",
  "map": { "Grass": {"ref":"Foot_Grass"}, "Wood": {"ref":"Foot_Wood"} },
  "default": "Grass" }
```
- **Blend**（複数を同時レイヤ再生、各層を RTPC で音量制御）
```json
{ "type":"Blend", "layers":[
    { "child": {"ref":"Engine_Low"},  "rtpc":"EngineLoad", "curve":[[0,-60],[1,0]] },
    { "child": {"clip":"sfx/engine_high.wav"} } ] }
```

### 3.6 Switch / State / RTPC / バインディング / Ducking

**Switch**（GameObject 単位の離散状態）と **State**（グローバル状態）と **RTPC**（連続値）は「定義」と「何を変調するか（binding）」を分離して持つ。

```json
"switches": { "Surface": ["Grass","Wood","Metal"] },

"states": {
  "Music": { "values":["Explore","Combat","Result"], "default":"Explore", "transitionMs":800 }
},

"rtpc": { "PlayerSpeed": { "min":0, "max":10, "default":0 } },

"ducking": { "VoiceDuck": { "trigger":{"bus":"Voice"}, "target":{"bus":"BGM"},
                            "amountDb":-8, "attackMs":120, "releaseMs":400 } }
```

#### stateRules（State 変化時に実行する Action 列）
State が変わったとき何が起きるかを Action（§6）で明示する（BGM 切替・バス音量変更など）。
```json
"stateRules": [
  { "when": { "group":"Music", "value":"Combat" },
    "actions": [
      { "action":"Play", "target":{ "object":"BGM_Combat" }, "fadeMs":800 },
      { "action":"Stop", "target":{ "object":"BGM_Explore" }, "fadeMs":800 }
    ] },
  { "when": { "group":"Music", "value":"Explore" },
    "actions": [
      { "action":"Play", "target":{ "object":"BGM_Explore" }, "fadeMs":800 },
      { "action":"Stop", "target":{ "object":"BGM_Combat" }, "fadeMs":800 }
    ] }
]
```

#### rtpcBindings（RTPC → プロパティの連続変調 = modulator）
RTPC が「どの対象のどのプロパティを、どのカーブで」動かすかを明示する。
```json
"rtpcBindings": [
  { "rtpc":"PlayerSpeed", "target":{ "kind":"Footstep" }, "property":"volumeDb",
    "curve":[ [0,-12], [4,0], [10,3] ], "interp":"linear" },
  { "rtpc":"EngineLoad",  "target":{ "object":"Engine_Loop" }, "property":"pitch",
    "curve":[ [0,0.8], [1,1.3] ], "interp":"ease" }
]
```
- `property`: `volumeDb` / `pitch` / （将来 `lowpass` / `bus.volumeDb` 等）。
- `target`: §6 と同じ型付き（`object`/`kind`/`bus`）。GameObject 単位の RTPC は、その GameObject に属する再生インスタンスにのみ適用。
- 評価: 毎フレーム RTPC 値からカーブを引いて対象へ反映（`SoundEngine::SetVolume/SetPitch`/`FadeBus`）。

### 3.7 Event / Action
`target` は**型付きオブジェクト**（`{"object":…}` / `{"kind":…}` / `{"bus":…}`）。裸文字列は使わない（§6）。
```json
"events": {
  "Player_Footstep": [ { "action":"Play", "target":{ "object":"Footstep_Surface" } } ],
  "Play_TitleBGM":   [ { "action":"Play", "target":{ "object":"Title_BGM" }, "fadeMs":1500 } ],
  "Stop_TitleBGM":   [ { "action":"Stop", "target":{ "object":"Title_BGM" }, "fadeMs":1500 } ],
  "Stop_AllSE":      [ { "action":"Stop", "target":{ "kind":"Footstep" } } ],
  "Enter_Combat":    [ { "action":"SetState", "group":"Music", "value":"Combat" } ]
}
```
Action 種別と target 型は §6。

### 3.8 完全な例（最小実用）
```json
{
  "version": 1,
  "bankId": "Main",
  "kinds": {
    "Footstep": { "bus":"SE",  "spatialMode":"auto", "maxVoices":8, "priority":30, "voiceStealing":"oldest", "cooldownMs":40, "cooldownScope":"gameObject", "attenuation":"DefaultSE" },
    "BGM":      { "bus":"BGM", "spatialMode":"2d", "maxVoices":2, "loadPolicy":"stream", "fadeInMs":1500, "fadeOutMs":1500 },
    "Voice":    { "bus":"Voice", "duckGroup":"VoiceDuck" }
  },
  "attenuations": { "DefaultSE": { "model":"Inverse", "minDistance":1, "maxDistance":40 } },
  "switches":     { "Surface": ["Grass","Wood","Metal"] },
  "ducking":      { "VoiceDuck": { "trigger":{"bus":"Voice"}, "target":{"bus":"BGM"}, "amountDb":-8, "attackMs":120, "releaseMs":400 } },
  "objects": {
    "Foot_Grass": { "type":"Random", "kind":"Footstep", "children":[ {"clip":"sfx/foot_grass1.wav"}, {"clip":"sfx/foot_grass2.wav"}, {"clip":"sfx/foot_grass3.wav"} ], "avoidRepeat":1, "volumeRandomDb":[-2,0], "pitchRandom":[0.95,1.05] },
    "Foot_Wood":  { "type":"Random", "kind":"Footstep", "children":[ {"clip":"sfx/foot_wood1.wav"}, {"clip":"sfx/foot_wood2.wav"} ] },
    "Footstep_Surface": {
      "type":"Switch", "switchGroup":"Surface",
      "map": { "Grass": {"ref":"Foot_Grass"}, "Wood": {"ref":"Foot_Wood"} },
      "default": "Grass"
    },
    "Title_BGM": { "type":"Sound", "kind":"BGM", "clip":"bgm/title.m4a", "loop":true }
  },
  "events": {
    "Player_Footstep": [ { "action":"Play", "target":{ "object":"Footstep_Surface" } } ],
    "Play_TitleBGM":   [ { "action":"Play", "target":{ "object":"Title_BGM" }, "fadeMs":1500 } ],
    "Stop_TitleBGM":   [ { "action":"Stop", "target":{ "object":"Title_BGM" }, "fadeMs":1500 } ]
  }
}
```

---

## 4. Kind 仕様（中心概念）

Kind = サウンドの**種別プリセット**。オブジェクト/イベントに Kind を 1 つ付けるだけで以下が自動適用される（各オブジェクトで個別 override 可）。

| フィールド | 効果 |
|---|---|
| `bus` | ルーティング先（SE/Voice/BGM…） |
| `spatialMode` | `auto`/`2d`/`3d`。`auto` は GameObject 指定時のみ 3D（GameObjectId と attenuation だけで判定しない） |
| `maxVoices` | 同時発音上限。超過時は `voiceStealing` で処理 |
| `voiceLimitScope` | `global`/`gameObject`（上限を全体で数えるか発生主体ごとに数えるか） |
| `priority` | ボイス枯渇時に残す優先度（0-100） |
| `voiceStealing` | `none`/`oldest`/`quietest`/`lowestPriority`/`reject`（新規拒否） |
| `cooldownMs` | 最小再発火間隔（連打・多重発音の防止） |
| `cooldownScope` | `global`/`gameObject`/`object`（足音は通常 `gameObject`） |
| `loadPolicy` | `preload`/`stream`/`auto`（常駐かストリーミングか。旧 `streaming` boolean の後継） |
| `attenuation` | 既定の 3D 減衰プリセット名 |
| `duckGroup` | この Kind の再生を**トリガ**にする Ducking 定義 |
| `volumeDb` / `pitch` | Kind 既定の補正 |
| `fadeInMs` / `fadeOutMs` | Kind 既定のフェード時間（BGM/Voice/UI の作業削減） |
| `stopOnGameObjectDestroy` | 3D emitter（発生主体）破棄時に停止するか |

> **virtualize は将来扱い**: 「実ボイスを消費せず再生カーソルだけ進める」真の仮想化は低レイヤ拡張が要る。フェーズ 1〜4 では `voiceStealing`（`oldest`/`quietest`/`lowestPriority`）と `reject` で発音上限を守る（§8）。

**解決順（後勝ち）**: `Kind 既定` → `親 Container` → `当該 Object` → `Action 上書き` → `RTPC/State による実行時変調`。
これにより「足音は Kind=Footstep にしておけば上限8・40ms クールダウン・SE バス・距離減衰が勝手に効く」状態を作る。新しいサウンドの追加は **clip を置いて Kind を指定するだけ**が基本になる。

---

## 5. SoundObject 解決（ObjectResolver）

`PostEvent` の Play Action は対象 SoundObject を**ツリー解決**して最終的な「再生指示（clip + パラメータ + 3D 設定）」へ落とす。

- **Sound** → その clip。
- **Random** → 重み付き抽選。`avoidRepeat` で直近 N を除外。`volumeRandomDb`/`pitchRandom` を適用。
- **Sequence** → 内部カウンタで次の子。`loop` で巡回。インスタンスごとに進行状態を持つ。
- **Switch** → `switchGroup` の現在値（その GameObject の値、無ければグローバル/`default`）で子を選択。
- **Blend** → 全レイヤを同時再生し、各レイヤの音量を `rtpc`+`curve` で制御（複数 PlayingSound を 1 イベントが束ねる）。

解決は **PostEvent 時に 1 回**（Random/Sequence/Switch）。Blend と RTPC 連動レイヤは毎フレーム音量更新する。

---

## 6. Event / Action 仕様

Event = Action の順次列（同時/遅延可）。共通フィールド: `target`(型付き, 下記), `delayMs`, `fadeMs`, `curve`(fade 形状)。

**target の型（High 反映）**: action ごとに読み替えず、**型付きオブジェクト**で対象種別を明示する。
```json
"target": { "object": "<ObjectName>" }   // SoundObject（Play の基本）
"target": { "kind":   "<KindName>" }     // その Kind の全インスタンス（Stop/Pause/SetVolume 等）
"target": { "bus":    "<BusName>" }      // バス（SetVolume/Pause 等）
```
- 各 action が許容する target 種別は下表。許容外は**ロード時バリデーション警告**。
- **PlayingId は静的 JSON Event では使わない**（特定の再生インスタンス指定は runtime API `StopPlaying(id)` 専用, §10）。JSON は「何を」「どの分類を」までを宣言する。

| action | 許容 target | 説明 |
|---|---|---|
| `Play` | `object` | 解決して再生。`fadeMs` でフェードイン |
| `Stop` | `object` / `kind` / `bus` | 停止。`fadeMs` でフェードアウト |
| `Pause` / `Resume` | `object` / `kind` / `bus` | 一時停止・再開 |
| `SetVolume` / `SetPitch` | `kind` / `bus` / `object` | 適用（`fadeMs` 可） |
| `SetSwitch` | （target 無し / `group`+`value`） | Switch 値を変更（GameObject 単位） |
| `SetState` | （target 無し / `group`+`value`） | グローバル State を変更（→ stateRules 発火） |
| `SetRTPC` | （target 無し / `param`+`value`） | RTPC 値を設定 |
| `Break` | `object` / `kind` | ループの自然終了（末尾まで再生して停止） |
| `Seek` | `object`（将来） | 再生位置シーク |
| `PostEvent` | （`event` 名） | 別イベントをチェーン発火 |

1 イベントに複数 Action を並べられる（例: `Enter_Combat` = SetState(Music,Combat) + Play(Stinger)）。

---

## 7. ゲーム同期（Switch / State / RTPC）とカーブ

- **Switch**（GameObject 単位）: `SetSwitch("Surface","Wood", go)`。Switch コンテナの選択に使う。GameObject 未指定はグローバル既定。
- **State**（グローバル）: `SetState("Music","Combat")`。バス音量・State 連動のミックス・BGM 選択に使う。State は**遷移時間**（フェード）を持てる。
- **RTPC**（連続値）: `SetRTPC("PlayerSpeed", v, go)`。`curve`（点列 + 補間: linear/log/exp/ease）で対象プロパティ（voice/bus/レイヤの volume・pitch・LPF）へ写像。毎フレーム評価して `SoundEngine::SetVolume/SetPitch`/`FadeBus` に反映。

カーブはオーサリングで点列定義（将来エディタでグラフ編集）。

---

## 8. ボイス管理（VoiceLimiter）

実機の発音数・CPU を守るため、Kind 単位で制御する。

- **ポリフォニー**: `voiceLimitScope`（`global`/`gameObject`）に従ってアクティブ実ボイス数を数え、`maxVoices` 超過時に `voiceStealing` 規則で処理する。
  - `oldest` / `quietest` / `lowestPriority` … 既存ボイスを 1 つ `Stop` して空ける。
  - `reject` / `none` … 新規再生を拒否（鳴らさない）。
- **優先度**: グローバルなボイス総数上限（バックエンド/性能）に達したら `priority` の低いものから停止。
- **クールダウン**: `cooldownScope`（`global`/`gameObject`/`object`）の単位で最小再発火間隔（`cooldownMs`）を課す。足音は通常 `gameObject`（同じキャラの足音だけ間引く）。

実装は AudioDirector が `(Kind, scope キー)` 別カウンタ + アクティブリストを保持し、`SoundEngine::Stop` で制御する。**フェーズ 1〜4 は `voiceStealing` と `reject` で上限を守る**。

> **virtualization は将来扱い（Medium 反映）**: 「停止せず無音で再生カーソルだけ進め、空きが出たら実ボイスへ復帰」する真の仮想化は、低レイヤに「実ボイスを割り当てずに再生位置を進める」状態が必要（`SetVolume(0)` では実ボイスを消費したままで上限対策にならない）。`SoundEngine`／各バックエンド（XAudio2/Oboe ミキサ）への拡張を伴うため、本機能は別フェーズで扱う。

---

## 9. ダッキング（Ducking）

`ducking.<name>` = 「`trigger` バス（または Kind）が鳴っている間、`target` バスを `amountDb` 下げる。`attackMs` で下げ、`releaseMs` で戻す」。
例: セリフ（Voice）再生中は BGM を -8dB（VoiceDuck）。実装は AudioDirector が trigger のアクティブ有無を監視し、`SoundEngine::FadeBus(target, …)` で実現する。複数ダッキングは最大量を採用。

---

## 10. ランタイム API（プログラマ向け・極小）

```cpp
namespace aq { namespace audio {

	using EventId        = uint32_t;   // 名前の CRC32（既存 aqHash32 と同じ）
	using GameObjectId   = uint64_t;   // ECS の EntityID 等を流用可
	using PlayingId      = uint32_t;   // 世代付き（無効化済みは安全に no-op）

	// Bank
	void LoadBank(const char* path);
	void UnloadBank(const char* path);
	void ReloadBanks();                 // ホットリロード

	// イベント
	PlayingId PostEvent(const char* eventName, GameObjectId go = 0);
	PlayingId PostEvent(EventId event, GameObjectId go = 0);
	void      StopPlaying(PlayingId id, float fadeSeconds = 0.0f);
	void      StopAllByKind(const char* kind, float fadeSeconds = 0.0f);

	// ゲーム同期
	void SetSwitch(const char* group, const char* value, GameObjectId go);
	void SetState (const char* group, const char* value);          // グローバル
	void SetRTPC  (const char* param, float value, GameObjectId go = 0);

	// 3D GameObject
	void RegisterGameObject(GameObjectId go);
	void SetGameObjectTransform(GameObjectId go,
	                            const math::Vector3& pos, const math::Vector3& forward,
	                            const math::Vector3& up,  const math::Vector3& velocity);
	void UnregisterGameObject(GameObjectId go);

	// リスナー（カメラ）— SoundEngine の SoundListener を委譲設定
	void SetListener(const math::Vector3& pos, const math::Vector3& forward,
	                 const math::Vector3& up,  const math::Vector3& velocity);

	// 毎フレーム（Engine から駆動）
	void Update(float deltaTime);
}}
```
- 名前はすべて CRC32 ID 化（編集時=名前、実行時=ID）。コンパイル時ハッシュのマクロ（例 `AQ_AUDIO_EVENT("Player_Footstep")`）も用意。
- `AudioDirector` は内部実装、`aq::audio::` 自由関数が薄い公開窓口（既存 `Engine` の static ラッパ流儀）。

---

## 11. ECS 連携

- `AudioEventEmitterComponent`（新規） … エンティティを GameObject として登録し、イベント発火の主体にする。`PostEvent` 用のイベント名（自動再生/トリガ）も持てる。
  - **命名注記（Low 反映）**: 既存の低レイヤ `AudioSourceComponent`（§5 / Sound設計.md。clip を直接 3D 再生する）と区別するため、イベント駆動側は `AudioEventEmitterComponent` とする。低レイヤを直接使うか、イベント層を使うかでコンポーネントを使い分ける（将来どちらかへ寄せる選択もあり）。
- `AudioListenerComponent`（既存） … カメラに付与。
- `SoundSystem`（既存を拡張） … 毎フレーム、各 `AudioEventEmitterComponent` の Transform を `audio::SetGameObjectTransform` に流し、リスナーを `audio::SetListener` に反映、最後に `audio::Update(dt)` を駆動する。
- ゲームコードからは「エンティティに `AudioEventEmitterComponent` を付け、節目で `PostEvent("...", entity)`」が基本形。

---

## 12. 既存 SoundEngine へのマッピング

| オーサリング概念 | SoundEngine 呼び出し |
|---|---|
| Play（2D 単発） | `Play(clip, bus, fadeIn)` → `SoundHandle` |
| Play（3D・GameObject あり） | `CreateSource(clip, bus)` + `SetPosition/Velocity/Attenuation` |
| Play（ループ/長尺/ストリーミング Kind） | `OpenStream(path, bus)` + `Play(loop)` |
| Stop/Pause/Resume | `Stop/Pause/Resume`（handle/source/stream）、Kind→`StopAllByKind` 相当を内部実装 |
| SetVolume/Pitch（RTPC 反映） | `SetVolume` / `SetPitch`（handle/source/stream） |
| バス音量 / State 連動 | `SetBusVolume` / `FadeBus` / `FadeMaster` |
| ダッキング | `FadeBus(target, …)` |
| 3D 定位 | `SoundSource` + `Mixer3D`（距離/パン/ドップラー） |

**注記（バス階層）**: 現 `SoundEngine` のバスは固定 4 種。オーサリングの `buses` 階層はまず 4 種へ写像し、将来 `SoundEngine` 側を「可変バス＋submix 階層＋エフェクト挿入」へ拡張する（別タスク）。本設計のデータモデルは拡張後もそのまま使える形にしてある。

---

## 13. Bank / ロード / マージ規則 / ホットリロード / バリデーション

- **Bank** = JSON 1 ファイル（先頭に `"bankId"` を必須）。複数ロード可（共通定義 Bank + ステージ別 Bank 等）。`ResourceManager` 連携で clip を非同期ロード（既存 `SoundClip`）。

#### マージ・名前衝突規則（Medium 反映）
複数 Bank をまたいだ名前キー設計のため、合成規則を明確化する。

- **名前空間は種別ごとにフラット**（events / objects / kinds / buses / switches / states / rtpc / attenuations / ducking それぞれ独立した辞書）。Bank をまたいで同名は**同一論理エンティティ**を指す。
- **ロード優先度**: `LoadBank` 順、または Bank の任意 `"loadPriority"`（大きいほど後勝ち）。**重複ポリシー**は既定 `warn-and-keep-first`（先勝ち＋警告）、`"duplicatePolicy":"override"` 指定で後勝ちにできる。いずれもロード時に**衝突を警告ログ**へ。
- **CRC32 衝突**: 名前→ID 化は CRC32。**ロード時に逆引きテーブルへ登録し、異なる文字列が同一ハッシュへ落ちたら衝突エラー**を出す（名前を変えてもらう）。ID は実行時照合のみで、デバッグ/エディタ用に文字列も保持。
- **未解決参照**: 別 Bank で後から定義される前提の前方参照を許容するため、**参照解決は全 Bank ロード後に一括**で行い、未解決は警告（リリースは無音に倒す）。
- **Unload**: `UnloadBank(bankId)`。**アクティブな再生インスタンスがその Bank の定義を参照している間は unload を遅延**（defer）し、全インスタンス終了後に実体解放する。clip（`SoundClip`）は shared_ptr 参照で生存保証（[Sound設計.md §3.3a]）。即時破棄が必要なら `StopByBank(bankId)` 後に unload。

#### その他
- **ホットリロード**: ファイル監視 or 手動 `ReloadBanks()`。再生中インスタンスは可能な範囲で維持、定義差分を反映。
- **バリデーション（ロード時）**: 未解決参照（clip/bus/switch/object/kind）、循環参照（ref ループ）、必須項目欠落、target 型不一致（§6）、CRC32 衝突を**警告ログ**（`EnginePrintf`）。リリースでは安全側（無音）に倒す。
- **ID 解決**: ロード時に名前→CRC32 を確定し、実行時はハッシュ照合。

---

## 14. サウンドエンジニアのワークフロー & オーサリング/デバッグ UI

「設定しやすさ」を担保する仕掛け（将来の専用エディタへの布石）。

- **ImGui オーサリング/デバッグパネル**（既存 DebugUI に統合）:
  - **再生モニタ**: 鳴っているイベント/ボイス一覧（Kind・バス・距離・音量メータ・ボイススティール/cooldown 状態）。
  - **ゲーム同期コンソール**: Switch / State / RTPC を手で動かして即試聴。
  - **イベントブラウザ**: ツリー表示＋「Post」ボタンで手動発火。Random/Switch の解決結果を可視化。
  - **バス/ダッキング可視化**: フェーダー、ダッキング量メータ。
  - **バリデーション表示**: 未解決参照の一覧。
- **賢いデフォルト**: `clip` 以外ほぼ省略可。Kind が大半を埋める。
- **命名規約**: `Category_Action`（`Player_Footstep`, `UI_Click`, `Play_BattleBGM`）。Switch/State は `Group/Value`。
- **将来の専用エディタ**: 上記 ImGui を発展させ、ノードグラフ（コンテナツリー）・カーブエディタ（RTPC）・波形プレビューを持つスタンドアロン化を視野。データモデルは GUID/名前参照で疎結合にしてあるため移行容易。

---

## 15. ファイル構成（想定）

```
aqEngine/Sound/Authoring/
  AudioAuthoring設計.md       … 本書
  Audio.h                     … ゲーム向け薄い公開 API（aq::audio::PostEvent 等）
  AudioDirector.h/.cpp        … 窓口シングルトン。全サブシステムを所有・駆動
  AudioBank.h/.cpp            … JSON ロード・定義保持（ResourceManager 連携）
  AudioDefs.h                 … Event/Action/Object/Kind/Bus/Switch/State/RTPC/Attenuation/Ducking のデータ構造
  ObjectResolver.h/.cpp       … Random/Sequence/Switch/Blend 解決
  EventInstance.h/.cpp        … 再生中イベント（PlayingId, 束ねた SoundHandle/Source/Stream）
  GameSync.h/.cpp             … Switch/State/RTPC 値（global + per-GameObject）
  VoiceLimiter.h/.cpp         … Kind 別ポリフォニー/優先度/virtualization/cooldown
  Ducking.h/.cpp              … バス自動ダッキング
  Curve.h                     … RTPC/フェードのカーブ評価
aqEngine/Sound/Authoring/Debug/
  AudioAuthoringPanel.cpp     … ImGui オーサリング/デバッグ UI
aqEngine/Sound/Component/
  AudioEventEmitterComponent.h … ECS: GameObject 登録 + イベント発火（低レイヤ AudioSourceComponent とは別物）
```

---

## 16. フェーズ計画

1. **基盤（実用最小）**: `AudioBank`(JSON ロード) + `AudioDirector` + `Event(Play/Stop)` + `Kind`（bus/減衰/cooldown）+ `Sound`。`PostEvent` → `SoundEngine` 再生。ImGui 再生モニタ。
2. **バリエーション**: `Random` / `Sequence` コンテナ + ポリフォニー/ボイススティール/cooldown（足音・ヒット音が実用に）。
3. **Switch**: `Switch` コンテナ + `SetSwitch`（GameObject 単位、地面別足音）。
4. **3D 統合**: `AudioEventEmitterComponent` + GameObject 登録 + `SoundSystem` 連動（既存 3D を流用）。
5. **RTPC**: 連続パラメータ + カーブ + Blend レイヤ（速度→音量、エンジン音等）。
6. **State + ダッキング**: グローバル State（戦闘/探索 BGM 切替）+ バス自動ダッキング（セリフ優先）。
7. **オーサリング UI 強化 → 専用エディタ**: イベントブラウザ・カーブエディタ・解決可視化。将来スタンドアロン化。

各フェーズで `SoundEngine` の抽象は不変。フェーズ 1〜4 で「名前で発火・Kind 省設定・3D・バリエーション・地面別」までが揃い、サウンドエンジニアの日常作業の大半をカバーする。

---

## 17. 命名・ID 規約

- 名前 → **CRC32**（既存 `aq::util::ComputeCrc32` / `aqHash32`）で ID 化。実行時は ID 照合。
- 文字列はデバッグ/エディタ用に Bank 内へ保持（リリースでも名前引きできるよう小さなテーブルを持つ）。
- イベント: `Category_Action`／Switch・State: `Group` と `Value`／RTPC・Kind・Bus・Object: 一意名。

---

## 18. 将来拡張

- **バスエフェクト**（リバーブ/EQ/コンプ）と submix エフェクトチェーン（`SoundEngine` のバス拡張と連動）。
- **可変バス階層**（現 4 種固定 → ツリー）。
- **距離 LPF / spread / オクルージョン**（`Mixer3D` 拡張）。
- **HDR オーディオ**（相対ラウドネスでの自動ボイス管理）。
- **動画音声**（[Sound設計.md §11]）の Voice バス/ダッキング統合。
- **スタンドアロン オーサリングエディタ**（本データモデルをそのまま編集）。
