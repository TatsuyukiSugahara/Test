# UIアニメーション統合設計

> 対象コミット: 3ff20bf(P6 まで)/ 最終更新: 2026-11-10 / **再開するときは §16 から読む**
> ⚠ 鮮度: P7(`7bf1bc7` / `e49c34a`)は §11・§16.1 に反映済み。§18(P8)は Codex レビュー 2 回を経て 2026-11-10 に実装・Mac 評価済み(§11 P8)。§19(P9 相対値)は Codex レビューを経て 2026-11-10 に実装・Mac 評価済み(§11 P9)。§20(P10)は Codex レビューを経て 2026-11-10 に実装・Mac 評価済み(§11 P10)。§16.1 以外の §16 は 2026-09-17 時点。

UI アニメーションを「JSON だけで動き、UI Editor 1 つで設定できる」状態にする。
2026-09-16 に階層統合(`Clip → ClipTrack → PropTrack → Keyframe` の 4 階層を
`Clip → Track → Keyframe` の 3 階層へ)だけを対象に初版を書き、
2026-09-17 の 5 回のレビューで **プロパティ競合規則・状態遷移・エディタ統合・画面遷移** まで
範囲を広げた。P0〜P5 は 2026-09-17 に実装し Mac(Metal / Debug)で評価済み。§12 の後続改善は P4(プリセット)から順に本書へ取り込む(P5 = ベクタトラック)。

本書の構成は実施順に並べてある。**途中から再開する人は先に §16(到達点と再開手順)を読む。**

| 部 | フェーズ | 内容 |
| --- | --- | --- |
| 第 1 部 | P0 | 編集・保存基盤(共通選択 / 保存 / Reload / texturePath) |
| 第 2 部 | P1 / P2 / P2B | データ構造・排他モデル / レイヤー評価と基本自動フック / Exit 待機遷移 |
| 第 3 部 | P3 | 統合エディタ(Animation タブ / タイムライン / プレビュー) |
| 第 4 部 | P4 / P5 | プリセット(§14)/ ベクタトラック(§15) |
| 第 5 部 | — | 到達点と再開手順(§16) |
| 第 6 部 | P7 | オートキー(§17) |
| 第 7 部 | P8 | Ease 拡充(§18) |
| 第 8 部 | P9 | 相対値(§19) |
| 第 9 部 | P10 | Click 演出後の遷移(§20) |
| 後続 | — | Pause |

---

## 0. 目的と UX 原則

「UI の設定がやりづらい / 特にアニメーションが設定しづらい」への対応。
以下を原則とし、以降の決定はすべてこれに従う。

1. **入口は UI Editor 1 つ。** 選択は Hierarchy だけが持つ。別エディタを探させない
2. **設定できるものは必ず動く。** 実装が無い選択肢はエディタに出さない(出すなら Disabled と理由)
3. **内部用語を通常画面に出さない。** condition / conditionParam / ハッシュ / serial は Advanced とデバッグ表示だけ
4. **挙動は決定的。** 暗黙の上書き順・後勝ちを作らない。競合は規則で解決する
5. **1 UIObject の表示責務は 1 種類。** 複数の表示要素は子 UIObject に分ける

---

## 1. 調査で分かったこと

### 1.1 ClipTrack は自前の長さを持っていない

| 型 | 持っているもの |
| --- | --- |
| `UIAnimationClip` | `name` / `duration` / `clipTracks[]` |
| `UIClipTrack` | `name` / `condition` / `conditionParam` / `loopFrom` / `loopSkipFirst` / `restoreOnComplete` / `tracks[]` |
| `UIAnimationTrack` | `property` / `keyframes[]` |
| `UIKeyframe` | `time` / `value` / `ease` |

`UIClipTrack` に `duration` はなく、[UIAnimationComponent.cpp](../aqEngine/UI/Component/UIAnimationComponent.cpp) の
`Update()` は全 ClipTrack を `currentClip_->duration` で評価している。
同一クリップ内のイントロとホバーループが強制的に同じ秒数になる。

### 1.2 ClipTrack が存在する理由は「同時実行の受け皿」

`currentClip_` は単数で、`Play()` は `runtimes_` を毎回クリアする。クリップは同時に 1 本しか走らない。
「出現アニメを流しつつ、ホバー中は光らせる」の受け皿として Clip の内側にもう 1 層が要った。
同時実行を Clip 側へ移せば、この層は役目を失う。

### 1.3 移行対象が存在しない

- `UIAnimationComponent::Play()` / `TriggerTrack()` / `SetCondition()` の呼び出しはリポジトリ内に 0 件
- `"animation"` セクションを持つ UI アセットも 0 件(`Game/Assets/UI/` 配下)
- Image / NineSlice / CircleGauge を同じノードに 2 種以上付けたアセットも 0 件

現在のゲーム UI の演出は [AquaDashScreens.cpp](../Game/Application/UI/AquaDashScreens.cpp) の
`OnUpdate()` で `elapsed_` を自前で回す手書きで、アニメ機構は使われていない。
**JSON 互換と UIObject モデルを変えるコストが 0 なのは今だけ**で、これが本書を先にやる根拠になる。

### 1.4 統合で消す既存の不具合と暗黙ルール

ランタイム([UIAnimationComponent.cpp](../aqEngine/UI/Component/UIAnimationComponent.cpp))

- クリップの完了判定を `condition == Default` のトラックだけが担う。Default を作り忘れたクリップは永久に終わらない
- 同一プロパティを複数トラックが動かすと `runtimes_` の順で後勝ち。Restore も他のトラックを見ずに書き戻す
- Bool の判定が `finished` を見ないため、非ループの Bool トラックは条件が真の間 **0 秒から永久に再スタート**する。
  そのたびに Snapshot を取り直すので Restore も意味を失う
- Trigger は `finished` になると再発火できない(`finished` を戻す経路が `Play()` しかない)
- Bool 解除時は時刻だけ戻し、値は戻さない
- `loopSkipFirst` はヘッダ / シリアライザ / エディタのチェックボックスにあるが、ランタイムで一度も参照されない

エディタ・保存([UIAnimationEditor.cpp](../aqEngine/UI/Debug/UIAnimationEditor.cpp) / [UIEditorDebugPanel.cpp](../aqEngine/UI/Debug/UIEditorDebugPanel.cpp))

- Animation Editor の `Save` が選択オブジェクトのクリップを JSON の**ルートノード**に書く。子を選んで保存すると別ノードに付く
- UI Editor と Animation Editor が別々の選択ハンドルと Object Picker と保存パスを持つ
- `UIAnimationSerializer::LoadAll()` が既存クリップをクリアせず上書き追加する。削除済みクリップが残る
- `texturePaths_` は UI Editor で Load Tex したときだけ埋まり、ドキュメントロード時のパスは引き継がれない。
  **ロード直後に Save JSON すると全ノードの `texture` キーが落ちる**
- NineSlice のテクスチャパスは `objId + 0x01000000` でキーをずらして Image との共存に備えている
- UI Editor に Load は無い(Save JSON だけ)
- エディタが `selClipTrackIdx_` / `selPropTrackIdx_` の二重インデックスで選択を持つ

フレーム順と画面遷移([Application.cpp](../aqEngine/Core/Application.cpp) / [UIScreenManager.cpp](../aqEngine/UI/Screen/UIScreenManager.cpp))

- 1 フレームの順は **入力更新 → 各画面 OnUpdate → UIAnimationSystem::Update → FlushPendingOps → CollectRenderItems**
- `Pop` / `Replace` は `OnExit()` → `OnDestroy()` → ルート破棄を同じ Flush で連続して行う。Exit 演出を入れる場所が無い
- クリック callback が `Pop()` を積むと同フレームの Flush でルートが消え、その後の描画収集は対象を持たない。
  **クリック演出は 0 フレームも描画されない**
- `Push` は Flush 内で `OnCreate()` → `OnEnter()` を呼ぶ。Flush はアニメ更新の後・描画収集の前なので、
  Enter 演出を OnEnter 直後に起動しても最初のサンプルは次フレーム。**1 フレームだけ素の姿が描画される**
- ゲーム側の画面は `OnEnter()` で子 UIObject を名前で探し、**生ポインタ**をメンバに持つ
  ([AquaDashScreens.cpp](../Game/Application/UI/AquaDashScreens.cpp))。ルートだけ差し替えると全部ダングリングになる
- 登録済みの `documentPath` は `UIScreenManager::ScreenEntry` の中で private。エディタから取れない

---

# 第 1 部: 編集・保存基盤

## 2. UIObject モデル

### 2.1 描画コンポーネントの排他

> **UIObject の表示責務を 1 種類に限定し、プロパティ名と基準値を一意にする。
> 複数の表示要素が必要な場合は子 UIObject へ分割する。**

`UIImageComponent` / `UINineSliceComponent` / `UICircleGaugeComponent` は 1 UIObject に 1 つまで。
`UITextComponent` / `UIButtonComponent` / `UICanvasComponent` は対象外(共存可)。

これはアニメーション都合の変更ではなく **UIObject モデルの決定**である。
現状はレンダラ([UIBatchRenderer.cpp](../aqEngine/UI/Rendering/UIBatchRenderer.cpp))が 3 種を独立に描画し、
UI Editor も共存を前提にキーをずらしている。意図的に許していた共存を、§0 の原則 5 で閉じる。
共存アセットは 0 件なので移行コストは無い。

排他を成立させる検証箇所(**5 つすべて**に入れる):

| 箇所 | 動作 |
| --- | --- |
| UI Editor の `+ Component` メニュー | 描画コンポーネントを 1 つ持っていたら他 2 種を出さない |
| `UIDocumentLoader` | 2 つ目以降の描画コンポーネントを拒否し、警告ログを出す |
| `UIDocumentSerializer` | 2 つ以上持つノードを検出したら警告ログ(保存は先頭 1 つ) |
| デバッグビルド | `UIObject::AddComponent` で 2 つ目の描画コンポーネント追加を `assert` |
| 既存アセット検査 | P1 のチェック項目として `Game/Assets/UI/` を走査する |

排他が決まると、`UIAnimationTrack::Apply()` の 3 連 `if` と `ReadFrom()` の「最初の 1 つ」の非対称も消える。
両方とも「持っている 1 つに読み書きする」に統一する。

### 2.2 texturePath はコンポーネントが持つ

`UIImageComponent` / `UINineSliceComponent` / `UICircleGaugeComponent` に authoring 用の文字列を 1 本足す。

```cpp
class UIImageComponent : public IUIComponent
{
public:
    std::string                                     texturePath; // 保存・編集用の正本
    std::shared_ptr<graphics::IShaderResourceView>  texture;
};
```

規則:

- `texturePath` が authoring 上の正本
- `UIDocumentLoader` は `texturePath` を保存してから `texture` をロードする
- UI Editor で変更したときは両方を更新する
- `UIDocumentSerializer` は `texturePath` を保存する
- ゲームコードが `texture` ポインタだけを直接差し替えても、保存パスは自動更新されない(仕様として明記)

これで `UIEditorDebugPanel::texturePaths_`、`objId + 0x01000000` のキー生成、
`UIDocumentSerializer::Save()` の `texturePaths` 引数を削除できる。
**§2.1 の排他とは独立に有効**で、排他化しなくてもキーずらしは不要になる。

## 3. 共通選択・保存・再ロード

### 3.1 UIEditorSession

UI Editor と Animation 編集が共有する状態を 1 つにまとめる。

```cpp
struct UIEditorSession
{
    UIObjectHandle selectedObject;   // 選択元は Hierarchy だけ
    std::string    screenName;       // 先頭画面の名前
    std::string    documentPath;     // 登録済みドキュメントパス(保存先)
    bool           dirty = false;    // 未保存の編集あり
};
```

- `UIEditorDebugPanel::selectedHandle_` を Session へ移す
- Animation Editor 独自の Object Picker(`targetHandle_` / `objList_`)を削除する。Animation は選択中の UIObject だけを編集する
- 選択変更・削除・Reload 時は Session の選択を一度クリアし、Animation 側の選択(Clip / Keyframe)とプレビューも同時に捨てる

### 3.2 保存

保存操作は UI Editor 上部の 1 か所だけ。

| 操作 | 動作 |
| --- | --- |
| Save | `Session.documentPath` へ UI ツリー全体を `UIDocumentSerializer::Save()` で保存する。パスは登録済みで固定 |
| Save As | 明示的な別操作。パスを入力して保存する。`documentPath` は変えない |
| Reload | 未保存確認のあと、先頭画面を `ReloadTopDocument()` で即時再生成する |

- Animation Editor 独自の `JSON Path` / `Save` / `Load` は削除する
- ノード単位の `"animation"` 出力は `UIDocumentSerializer::SerializeNode()` に既にあるため、
  名前パスで JSON ノードを探す実装は書かない(同名兄弟の問題ごと消える)
- `ref` はロード時に展開され UIObject に元ファイル情報が残らないため、**保存は展開済みの形で書く**。
  再利用構造が失われることを、Save ボタン付近に常時表示する(「ref は展開して保存されます」)
- `dirty` が真のとき、ウィンドウタイトルに `*` を出し、Reload と画面遷移前に確認する

### 3.3 再ロード

再ロードは**画面まるごと再生成の一択**。ルートだけ差し替える経路は作らない(§1.4 の生ポインタ)。

`UIScreenManager` に 2 つ足す。

```cpp
std::string_view GetDocumentPath(std::string_view screenName) const; // 登録済みパス。無ければ空
void             ReloadTopDocument();                                // 先頭画面を同名で即時再生成
```

- `ReloadTopDocument()` は内部で同名の `Replace` を積む。`OnExit → OnDestroy → ルート破棄 → CreateScreen → OnCreate → OnEnter` が一通り走る
- **§9 の Exit 待機は通さない**(エディタの再ロードに演出は要らない)。画面遷移用 `Replace()` とは別 API にする理由がこれ
- Replace は pending なので、Reload ボタンを押した時点で以下を即座にクリアする:
  Session の選択 Handle / 選択 Clip / Keyframe 選択 / プレビュー状態 / 基準値キャッシュ / エディタが持つ生ポインタ。
  `dirty` も同時に落とす(再生成後のドキュメントは保存済み状態)
- **ゲーム側が `OnEnter()` の後に一度だけ呼ぶ setter は再実行されない。** AquaDash のタイトルは
  `SetStageThumbnail()` を状態機械から 1 回だけ呼ぶため、Reload 後はサムネイルが `OnEnter()` の
  初期値(非表示)に戻る。エディタの仕様であり、画面側が再実行を望むなら `OnEnter()` で自前の値を
  引き直す(P0 評価で確認)

---

# 第 2 部: データとランタイム

## 4. データ構造

### 4.1 Clip と ClipTrack を統合する

`UIClipTrack` を廃止し、条件・ループ・完了時動作を `UIAnimationClip` が直接持つ。

```cpp
enum class UIClipCondition : uint8_t { Manual, Bool, Trigger };

enum class UIAnimationFinishMode : uint8_t
{
    Hold,     // 完了時の最終値を基準値へ確定する
    Restore,  // 基準値を変えずにレイヤーを外す(起動前の値へ戻る)
};

struct UIAnimationClip
{
    std::string            name;                  // 表示・保存用。非空・重複禁止
    std::string            groupName;             // 表示・保存用。空 = name と同じ
    uint32_t               group          = 0u;   // 実行時の識別子。0 = 未解決
    float                  duration       = 0.f;  // クリップごとに持つ
    UIClipCondition        condition      = UIClipCondition::Manual;
    uint32_t               conditionParam = 0u;   // Bool / Trigger の識別子(ハッシュ)
    std::string            conditionParamName;    // 表示・保存用
    float                  loopFrom       = -1.f; // -1 = ループなし
    UIAnimationFinishMode  finish         = UIAnimationFinishMode::Hold;

    std::vector<UIAnimationTrack> tracks;
};
```

- `UIAnimationTrack` と `UIKeyframe` は変更しない
- `loopSkipFirst` は**削除**する(§1.4。`loopFrom` が「初回は 0 から、2 周目以降は途中から」を表現している)
- `restoreOnComplete` は `finish` に置き換える。意味が伝わる名前にするため
- 旧 `UITrackCondition::Default` は `Manual` に置き換わる

### 4.2 起動条件

| condition | 起動 | 用途 |
| --- | --- | --- |
| `Manual` | `Play(group)` を呼んだとき | 画面の出現・退場など、ゲーム側が任意のタイミングで出すもの |
| `Bool` | `GetCondition(param)` が真の間 | ホバー、フォーカス、押下中の継続演出 |
| `Trigger` | `Trigger(param)` で 1 回 | クリック、被弾などの単発演出 |

`Bool` / `Trigger` のクリップは**常駐して毎フレーム評価される**。`Play()` を呼ばなくても動く。
`group` は `Manual` だけで意味を持つ。**`Bool` / `Trigger` クリップの group はエディタに表示も編集もさせない。**

### 4.3 識別子は 32bit ハッシュ

**規則は 1 本だけ。**

```
groupName を省略したクリップは group = aqHash32(name)
Play(x) = group == x の Manual クリップを全部起動する
```

- JSON には `"group": "Open"` / `"conditionParam": "Hover"` と**文字列で書く**(手編集できる形を維持する)
- ロード時に `aqHash32()`([Util/CRC32.h](../aqEngine/Util/CRC32.h))で `group` / `conditionParam` を埋める
- 保存とエディタ表示は `groupName` / `conditionParamName` を使う
- `Update()` が触るのは `uint32_t` だけ。文字列比較は 1 回も走らない

スケルタルアニメの [AnimationComponent](../aqEngine/Component/AnimationComponentSystem.h) が
`Play(uint32_t nameHash)` / `0 = なし` を既に採っており、UI 側だけ文字列比較を回す理由がない。

### 4.4 JSON フォーマット(統合後)

```json
{
  "animation": {
    "clips": [
      { "name": "OpenSlide", "group": "Open", "duration": 0.4,  "condition": "Manual", "finish": "Hold",
        "tracks": [ { "property": "PositionY", "keyframes": [] } ] },
      { "name": "OpenFade",  "group": "Open", "duration": 0.25, "condition": "Manual", "finish": "Hold",
        "tracks": [ { "property": "ColorA", "keyframes": [] } ] },
      { "name": "Hover", "duration": 0.8, "condition": "Bool", "conditionParam": "Hover",
        "loopFrom": 0.0, "finish": "Restore",
        "tracks": [ { "property": "ColorA", "keyframes": [] } ] }
    ]
  }
}
```

- `Play(aqHash32("Open"))` で 2 本が**それぞれの長さ**で走る
- `Hover` は `Play` を呼ばずに条件だけで動く。`OpenFade` と同じ `ColorA` を動かすが、
  起動要求が別なので §6 の serial で決着する(禁止されるのは**同じ起動単位の中**の重複だけ)
- `"group"` を省略したクリップは自分の名前がグループになる
- 旧キー `clipTracks` / `restoreOnComplete` / `loopSkipFirst` は読み捨てる(移行対象が 0 件なので互換コードは書かない)

### 4.5 authoring 検証

エディタは保存前に、ローダはロード時に、以下を検証する。エディタは違反箇所を赤字で示し保存を止める。ローダは警告して該当クリップを捨てる。

| 規則 | 理由 |
| --- | --- |
| Clip 名は非空、同一コンポーネント内で重複なし | vector 化で名前がキーでなくなるため、明示的に検証する |
| `Bool` / `Trigger` の `conditionParam` は非空 | ハッシュ 0 は「未解決」と衝突する |
| `aqHash32()` の結果が 0、または同一コンポーネント内で別文字列が同じハッシュ | CRC32 衝突。名前を変えさせる |
| `duration > 0` | 0 除算とゼロ長ループ防止 |
| `0 <= loopFrom < duration` | 範囲外はループ長が負になる |
| キーフレーム時刻は `duration` 以下。エディタは `duration` へ clamp、ローダは警告して clamp | 到達しないキーを黙って持たせない |
| 同一 Clip 内で同じ `property` の Track は 1 本 | 同一クリップ内の後勝ちを作らない |
| **同じ起動単位**(同 group の Manual 同士 / 同 param の Bool 同士 / 同 param の Trigger 同士)で同じ `property` を使うクリップは 1 本 | 同 serial の競合を作らない(§6.2) |
| `Exit` グループの Manual クリップに `loopFrom >= 0` を許さない | グループ完了が来ず、画面遷移が止まる(§9) |

## 5. ランタイム構造

### 5.1 runtime は clips と 1:1 の並列 vector

```cpp
struct ClipRuntime
{
    float    time             = 0.f;
    bool     active           = false;
    uint32_t activationSerial = 0u;   // 0 = 未起動
};
std::vector<UIAnimationClip> clips_;     // private
std::vector<ClipRuntime>     runtimes_;  // clips_[i] に対応。ポインタは持たない
```

runtime はクリップの中身へのポインタを持たない。毎フレーム `clips_[i].tracks` を読み直す。
そのため **Track / Keyframe / duration / loop / finish の編集に再構築は要らない。**

再構築契約:

| 変更 | 必要な処理 |
| --- | --- |
| Clip 追加 / 削除 / 並べ替え、JSON Load | `StopAll()` + 全 runtime 再構築 |
| condition / group の変更 | 対象 runtime だけ初期化(`ResetClipRuntime(i)`) |
| Track 追加・削除、Keyframe 編集、duration / loopFrom / finish 変更 | 不要 |

契約を通さずに壊せないよう、`clips` を private にし、構造変更は次の API に限定する。

```cpp
const std::vector<UIAnimationClip>& GetClips() const;

size_t AddClip(UIAnimationClip clip);
void   RemoveClip(size_t index);
void   MoveClip(size_t from, size_t to);
void   ReplaceAllClips(std::vector<UIAnimationClip> clips);   // JSON Load 用。既存を全部捨てる

void   SetClipCondition(size_t index, UIClipCondition condition, uint32_t param, std::string_view paramName);
void   SetClipGroup(size_t index, std::string_view groupName);

UIAnimationClip& EditClip(size_t index);  // Track / Keyframe / duration / loop / finish の編集用。
                                          // condition / group はここから触らない(setter を使う)
```

mutable 参照を返す `EditClip()` は構造を動かさない編集に限る。
condition / group は setter を通さないと runtime 初期化が抜けるため、エディタはこの 2 つを必ず setter で変える。

### 5.2 公開 API

```cpp
void Play(uint32_t group);                     // group の Manual クリップを全部起動
void Stop(uint32_t group);                     // group の Manual クリップを停止
void StopAll();                                // 全 Manual クリップを停止(Bool / Trigger は対象外)
void SetCondition(uint32_t condition, bool v);
void Trigger(uint32_t trigger);
bool IsGroupPlaying(uint32_t group) const;     // group の Manual クリップが 1 本でも active
bool IsPlaying() const;                        // Manual クリップが 1 本でも active
```

- `TriggerTrack()` は廃止する。階層を消した後に ClipTrack を連想させる名前を残さない
- `currentClip_` は廃止する
- 画面単位の完了判定はツリーを歩く自由関数を `UIAnimationSystem` に置く:
  `bool IsAnimationGroupPlaying(const UIObject* root, uint32_t group);`
- グループ完了の通知はポーリング(`IsGroupPlaying`)で足りる。コールバックは足さない

### 5.3 状態遷移

**起動時は共通で「時刻 0 のサンプルをその場で適用する」。** 次フレームまで素の姿が見える 1 フレーム(§1.4)を消すため。

Manual

| イベント | 動作 |
| --- | --- |
| `Play(group)`(非再生中) | 先頭から再生。新しい serial を発行 |
| `Play(group)`(再生中) | 時刻を 0 に戻し、新しい serial を発行 |
| 自然完了(`finish == Hold`) | 最終値を基準値へ確定し、レイヤーを外す |
| 自然完了(`finish == Restore`) | 基準値を変えずにレイヤーを外す |
| `Stop(group)` / `StopAll()` | 途中値を確定せずレイヤーを外す(Restore 相当) |
| ループ中 | 自然完了しない。`Stop` でだけ終わる |

Bool

| イベント | 動作 |
| --- | --- |
| false → true | 先頭から再生。新しい serial を発行 |
| true のまま継続(非ループ) | 再生し終えたら**最終値を保持したままレイヤーに残る**。再スタートしない |
| true のまま継続(ループ) | ループを続ける |
| `SetCondition(true)` の重複呼び出し | 何もしない |
| true → false | `finish` に関係なくレイヤーを外す。下位のレイヤーか基準値が見える |
| `Stop` | 対象外 |

Trigger

| イベント | 動作 |
| --- | --- |
| `Trigger(param)`(非再生中) | 先頭から再生。新しい serial を発行 |
| `Trigger(param)`(再生中) | 時刻を 0 に戻し、新しい serial を発行 |
| 自然完了 | `finish` に従う(Hold: 基準値確定 / Restore: レイヤーを外す) |
| 完了後の再発火 | 可(現行の「`finished` で二度と鳴らない」を廃止) |
| `Stop` | 対象外 |

途中値を保持したまま止めたい要求が出たら `Pause()` を別途足す。通常の `Stop` は Restore 相当で固定する。

## 6. プロパティ競合の解決(レイヤー方式)

### 6.1 なぜ直接 Apply / Restore しないか

runtime ごとにプロパティへ直接書き、Restore で snapshot を書き戻す方式は、以下で破綻する。

- 並走時は `runtimes_` の順で後勝ちになる(暗黙の順序)
- condition で固定順を付けても「Hover 中に Exit を開始したら Exit を見せたい」と
  「Enter 中に Hover したら Hover を見せたい」を同時に満たせない。**condition は起動方法であって描画優先度ではない**
- snapshot は「起動時点の値」であって「下のクリップが確定した値」ではない。
  Open が Alpha 0 → 1 を流している途中(0.4)で Hover が起動し、Open が 1 で完了したあと Hover を離すと 0.4 に戻る

したがって「最後に起動したクリップを優先」を **activationSerial** で表し、プロパティごとに勝者を 1 本選ぶ。

### 6.2 activationSerial

- コンポーネントごとに単調増加の `uint32_t` カウンタを持つ
- **1 回の起動要求に 1 つの serial** を割り当てる。単位は次の 3 つ:
  - 1 回の `Play(group)`(グループ内の全 Manual クリップが同じ serial)
  - 1 回の `Trigger(param)`
  - 1 つの Bool 条件の false → true
- 同じプロパティでは、active なレイヤーのうち **serial が最大のクリップ**を採用する
- 同じ serial で同じプロパティを持つクリップは §4.5 で authoring エラーにする。
  万一ロード時に残った場合は `clips` の並び順で後勝ちとし、警告ログを出す
- 上位のクリップが終了すると、下にある active なクリップが再び見える
- 将来必要なら詳細設定として `priority` を足せる。今は足さない

`Exit` などの名前をコアで特別扱いしない。serial 上は Exit 開始後に起きた Hover が勝つのが正しい。
Exit に割り込ませたくない場合は自動フック側で入力を止める(§9)。

### 6.3 1 フレームの 2 段階処理

```
1. 状態更新   全 runtime の起動判定・時刻・ループ・完了を更新する。プロパティには触らない
2. 適用       プロパティごとに勝者を 1 本選び、その値を 1 回だけ書く。勝者が無ければ基準値
```

### 6.4 基準値のライフサイクル(プロパティ単位)

1. そのプロパティのレイヤーが **0 本から 1 本になった瞬間**に、UIObject の現在値を基準値として取得する
2. レイヤーが存在する間、ゲームコードの直接書き込みは次回の適用で上書きされる(仕様)
3. 隠れている下位のクリップが `Hold` で完了した場合も、基準値への確定は行う(上に別レイヤーがあっても)
4. 最後のレイヤーが外れたら基準値を適用する
5. レイヤーが 0 本になった後は基準値キャッシュを破棄する
6. 次回起動時は、その時点の UIObject の値を改めて取得する(アニメ外でゲームコードが変えた値が新しい基準値になる)

3 により §6.1 の「0.4 に戻る」は解決する。Open が 1 で完了した時点で基準値が 1 に確定し、Hover を離すと 1 が見える。

### 6.5 動作例(P2 の評価項目)

| 手順 | 期待 |
| --- | --- |
| Open(Manual, Alpha 0→1)の途中で Hover 開始 → Open 完了 → Hover 解除 | Open の最終値 1 |
| Hover 中に Manual グループを `Play` | Manual が Hover より上に載る |
| Click(Trigger, Restore)が終わる | Hover が active なら Hover の表示に戻る |
| Bool を true のまま維持 | 非ループは最終値で止まり、再スタートしない |
| Trigger を再生中と完了後に再発火 | どちらも先頭から再生する |
| Enter で Play 後の起動フレーム | 素の姿が 1 フレームも出ない |

---

## 7. 責務表

### 変更ファイル

| ファイル | 変更 |
| --- | --- |
| [UI/Animation/UIClipTrack.h](../aqEngine/UI/Animation/UIClipTrack.h) | **削除**。`UIClipCondition` は `UIAnimationClip.h` へ |
| [UI/Animation/UIAnimationClip.h](../aqEngine/UI/Animation/UIAnimationClip.h) | §4.1 の構造。`UIAnimationFinishMode` を追加 |
| [UI/Animation/UIAnimationTrack.cpp](../aqEngine/UI/Animation/UIAnimationTrack.cpp) | `Apply` / `ReadFrom` を「持っている 1 つに読み書き」へ統一(§2.1) |
| [UI/Component/UIAnimationComponent.h](../aqEngine/UI/Component/UIAnimationComponent.h) / [.cpp](../aqEngine/UI/Component/UIAnimationComponent.cpp) | `clips` を private の `std::vector` へ。§5.1 の並列 runtime と編集 API、§5.2 の公開 API、§5.3 の遷移、§6 のレイヤー評価と基準値。`currentClip_` / `TriggerTrack` 廃止 |
| [UI/Animation/UIAnimationSystem.h](../aqEngine/UI/Animation/UIAnimationSystem.h) / .cpp | `IsAnimationGroupPlaying(root, group)` を追加 |
| [UI/Animation/UIAnimationSerializer.h](../aqEngine/UI/Animation/UIAnimationSerializer.h) / [.cpp](../aqEngine/UI/Animation/UIAnimationSerializer.cpp) | `SaveClipTrack` / `LoadClipTrack` を削除。`group` / `conditionParam` を文字列で入出力しロード時にハッシュ。`finish` の入出力。`LoadAll` は `ReplaceAllClips` で全置換。§4.5 の検証 |
| [UI/Component/UIImageComponent.h](../aqEngine/UI/Component/UIImageComponent.h) / NineSlice / CircleGauge | `texturePath` を追加(§2.2) |
| [UI/UIObject.h](../aqEngine/UI/UIObject.h) / .cpp | デバッグビルドで描画コンポーネント排他を `assert`(§2.1) |
| [UI/Resource/UIDocumentLoader.cpp](../aqEngine/UI/Resource/UIDocumentLoader.cpp) | `texturePath` を埋めてからロード。描画コンポーネント排他の検証 |
| [UI/Resource/UIDocumentSerializer.h](../aqEngine/UI/Resource/UIDocumentSerializer.h) / [.cpp](../aqEngine/UI/Resource/UIDocumentSerializer.cpp) | `texturePaths` 引数を削除し、コンポーネントの `texturePath` を書く。排他違反の警告 |
| [UI/Screen/UIScreenManager.h](../aqEngine/UI/Screen/UIScreenManager.h) / [.cpp](../aqEngine/UI/Screen/UIScreenManager.cpp) | `GetDocumentPath()` / `ReloadTopDocument()`(§3.3)。Exit 待機の状態機械(§9) |
| [UI/Screen/UIScreen.h](../aqEngine/UI/Screen/UIScreen.h) | Enter / Exit グループの自動再生を `UIScreenManager` から呼ぶための入口(§8.1) |
| [UI/Input/UIInputSystem.cpp](../aqEngine/UI/Input/UIInputSystem.cpp) | Hover / Pressed / Focused / Click を同じ UIObject の `UIAnimationComponent` へ橋渡し(§8.1)。Exit 待機中の入力停止(§9) |
| [UI/Debug/UIEditorDebugPanel.h](../aqEngine/UI/Debug/UIEditorDebugPanel.h) / [.cpp](../aqEngine/UI/Debug/UIEditorDebugPanel.cpp) | `UIEditorSession` を持つ。上部に Save / Save As / Reload。Inspector に `[Properties] [Animation]` タブ。下部に Timeline。`texturePaths_` 削除。`+ Component` の排他 |
| [UI/Debug/UIAnimationEditor.h](../aqEngine/UI/Debug/UIAnimationEditor.h) / [.cpp](../aqEngine/UI/Debug/UIAnimationEditor.cpp) | 独立パネル(`IDebugRenderable`)をやめ、UI Editor から呼ばれる Animation タブ / Timeline の描画関数群にする。Object Picker / Save / Load / `selClipTrackIdx_` / `ctNameBuf_` 共有バッファを削除 |
| [UI/Debug/TextStyleEditorPanel.h](../aqEngine/UI/Debug/TextStyleEditorPanel.h) / .cpp | Text Inspector の `[Edit]` から同じウィンドウ内のタブ / ポップアップとして開く。トップメニューの入口は残さない |
| UI/Debug/UIEditorSession.h | **新規**。§3.1 |

`.vcxproj` からは `UIClipTrack.h` の登録を外し、`UIEditorSession.h` を足す(`vs-project-files` スキルに従う)。

---

## 8. 自動フック(P2)

「JSON だけで動く」を成立させる部分。エディタの `Play when` に出す選択肢は、ここで動くものだけにする(§10.3)。

### 8.1 UIButton と画面 Enter の橋渡し

| 発生元 | 呼び出し | 備考 |
| --- | --- | --- |
| `UIScreenManager` の Push / Replace で `OnEnter()` が返った直後 | ルート以下の全 `UIAnimationComponent` に `Play(aqHash32("Enter"))` | **OnEnter 完了後**に起動する。ゲーム側が OnEnter で初期値を変えた後に基準値を取るため |
| `UIButtonComponent::isHovered` の変化 | 同じ UIObject の `SetCondition(aqHash32("Hover"), v)` | 入力更新がアニメ更新より先なので同フレームで反映される |
| `isPressed` の変化 | `SetCondition(aqHash32("Pressed"), v)` | |
| `isFocused` の変化 | `SetCondition(aqHash32("Focused"), v)` | |
| `FireClick` | `Trigger(aqHash32("Click"))` | callback が遷移を積んだ場合の扱いは §9 |

橋渡し先は「同じ UIObject に `UIAnimationComponent` があれば」に限る。無ければ何もしない。
`UIButtonComponent` に新しいフィールドは足さない。`UIInputSystem` が状態変化を検出して呼ぶ。

---

## 9. Exit 待機付き画面遷移(P2B)

### 9.1 なぜ必要か

§1.4 のとおり、現行の `Pop` / `Replace` は `OnExit()` と同じ Flush でルートを破棄する。
Exit 演出と、クリック後に遷移する演出は 1 フレームも描画されない。
`Play when: Exit` / `Click` をエディタに出す以上、**グループ完了判定と遅延 Pop / Replace は必須要件**である。

### 9.2 状態機械

```
Pending(op が積まれた)
  ↓
ExitStart   : 入力停止 → OnExit() → ルート以下に Play(aqHash32("Exit"))
  ↓          Exit グループのクリップが 1 本も無ければ ExitDone へ直行
ExitPlaying : 画面の OnUpdate() は止め、UIAnimationSystem だけ更新する
  ↓          IsAnimationGroupPlaying(root, Exit) が false になるまで待つ
ExitDone    : OnDestroy() → ルート破棄 → 次画面生成(Push / Replace のとき) → OnCreate() → OnEnter() → Enter 起動
```

- 入力停止: `ExitStart` で `UIInputSystem` がその画面への Hover / Pressed / Focused / Click を止め、
  Bool 条件をすべて false にする。以後、破棄まで新しい Bool 条件を送らない(これで Exit 中に Hover が割り込まない)
- `OnUpdate()` を止める理由: Exit 中にゲーム状態を動かし続けないため。アニメーションだけ進める
- タイムアウト: `ExitPlaying` が既定 2.0 秒を超えたら警告ログを出して `ExitDone` へ進む(ループ Exit の保険。§4.5 で検証もする)
- **ExitPlaying 中に積まれた新しい op は pending 列に残し、ExitDone 後に順に処理する。** 待機中の遷移は捨てない、打ち切らない
- `Back` は従来どおり `OnBack()` の結果で Pop に変換され、その Pop が上の状態機械を通る
- `ReloadTopDocument()`(§3.3)はこの状態機械を**通さない**

### 9.3 Click 演出後の遷移

クリック callback が `Pop()` / `Replace()` を積むと、同フレームの `ExitStart` で Exit が始まる。
Click の Trigger は Exit と別 serial で先に起動しているので、Exit グループが ColorA を持たなければ Click 演出はそのまま最後まで見える。
「Click 演出が終わってから Exit を始める」は P2B では作らず、**P10(§20)で足した**(ExitStart の前に ClickWait を挟む)。

---

# 第 3 部: 統合エディタ

## 10. UI Editor

### 10.1 完成形

```
┌ UI Editor ──────────────────────────────────────────┐
│ Screen: Title   Assets/UI/Title.json   Save  Save As  Reload │
├──────────────┬──────────────────────────────────────┤
│ Hierarchy    │ Inspector                            │
│ Canvas       │ [Properties] [Animation]             │
│ ├ Background │                                      │
│ └ StartButton│ 選択 UIObject の設定だけを表示         │
├──────────────┴──────────────────────────────────────┤
│ Animation Timeline / Preview(Animation タブ選択時)   │
└─────────────────────────────────────────────────────┘
```

Animation Editor は独立したエディタとして残さない。選択共有だけでは「どのエディタで何を設定するのか」が残るため。

### 10.2 選択

- 選択元は左の Hierarchy だけ(§3.1 の Session)
- Animation は選択中の UIObject の `UIAnimationComponent` だけを編集する。無ければ「Add Animation」ボタンを出す
- 選択変更時はプレビューを停止し、基準値へ戻す

### 10.3 Animation タブの通常表示

内部用語を見せない。

```
Animation: HoverGlow
Play when: Hover
Duration:  0.20 sec
After finish: Return
Loop: On
```

`Play when` から内部設定を自動で埋める。

| 表示 | 内部設定 | 表示できる時期 |
| --- | --- | --- |
| Enter | Manual / group = `Enter` | P2 完了後 |
| Exit | Manual / group = `Exit` | **P2B 完了後**。それまでは Disabled と「画面遷移対応後に利用可能」 |
| Hover | Bool / param = `Hover` | P2 完了後 |
| Pressed | Bool / param = `Pressed` | P2 完了後 |
| Focused | Bool / param = `Focused` | P2 完了後 |
| Click | Trigger / param = `Click` | P2 完了後 |
| Manual | Manual / カスタム group | P2 完了後 |

- `After finish` は `Hold` を「Keep」、`Restore` を「Return」と表示する
- condition / conditionParam / group 名の自由入力とハッシュ値は **Advanced** に畳む
- Bool / Trigger のクリップでは group を表示しない(§4.2)
- §4.5 の検証違反はその場で赤字にし、Save を止める

### 10.4 タイムライン

- 左 = Clip 一覧、右 = プロパティ行。ClipTrack 階層は存在しない
- 左パネルは `groupName` 見出しごとに Manual クリップをまとめる(見た目の 2 段、データは平ら)。Bool / Trigger は `Play when` 見出しの下
- Clip 行に `Play when` / Duration / Loop を出す
- 同じ起動単位で同じプロパティを使ったら、その場で警告(§4.5)
- プレビューは `ApplyScrub` 直書きではなく `Play()` / `Trigger()` / `SetCondition()` 経由にし、
  ループ・条件・`finish` を実挙動で確認できるようにする。プレビュー中の勝者 Clip を色またはアイコンで示す
- activationSerial は通常 UI に出さない。デバッグ用ツールチップに現在の勝者と serial を出す程度
- 選択インデックスは 1 本(`selClipIdx_` / `selTrackIdx_` / `selKeyframeIdx_`)。`selClipTrackIdx_` は廃止
- Clip の追加 / 削除 / 並べ替えは §5.1 の API を通す。編集中の Keyframe ドラッグで再生が止まらないことを確認する

### 10.5 TextStyle の入口

TextStyle は共有アセットなので編集機能は別のままでよい。入口だけ Text Inspector に置く。

```
Text Style: Assets/Styles/UI.textstyle.json  [Edit]
```

`[Edit]` で同じウィンドウ内のタブまたはポップアップを開く。トップメニューから別エディタを探させない。

---

## 11. フェーズ計画

### P0: 編集・保存基盤(第 1 部)

実装

- `UIEditorSession` を新設し、`UIEditorDebugPanel::selectedHandle_` を移す
- Animation Editor の Object Picker / JSON Path / Save / Load を削除し、選択は Session から取る
- `texturePath` をコンポーネントへ(§2.2)。ローダ・シリアライザ・UI Editor を追従させ、`texturePaths_` とキーずらしを削除
- `UIScreenManager::GetDocumentPath()` / `ReloadTopDocument()`(§3.3)
- UI Editor 上部に Save(固定パス)/ Save As / Reload。`dirty` 表示と未保存確認。ref 展開の注記

評価

- [x] 子オブジェクトを選んでクリップを保存 → 再ロードで同じ子に付く(`Sub` に `NewClip`。Mac 2026-09-17)
- [x] ドキュメントをロード → 何も触らず Save → `texture` キーが全ノードで保持される(AquaDash Title の 11 ノードで一致)
- [x] Image と NineSlice を別ノードに持つ画面で保存 → 両方のパスが正しい(NineSlice ノードを一時追加して確認)
- [x] Reload 後に UI Editor と Animation の選択が空で、ゲーム側の `OnEnter()` が再実行されて生ポインタが有効
- [x] Reload に Exit 演出が挟まらない(P2B 実装後に再確認)
- [x] 未保存状態で Reload を押すと確認が出る
- [x] Windows / D3D11 でビルドが通り、警告が増えていない(P6 で消化。D3D11 / D3D12 / Vulkan の Debug が通り、C4099 を潰して新規警告 0)

評価で直したもの

- ImGui の既定フォントに日本語グリフが無く、注記と確認文が `???` になった → エディタ内の文言は英語に統一
- Reload 後も `*` が残っていた → `ClearForReload()` で `dirty` を落とす
- Text の `Content` / `Style Path` が別オブジェクトの値を表示していた(移行前からの不具合。`static` バッファの同期条件が
  `prevSelectedHandle_` 更新後に評価され一度も成立しない)→ メンババッファにして `DebugRender()` の選択変更検出で同期
- `UIEditorSession.h` のフィルターが `UI` になっていた → `UI\Debug`

### P1: データ構造と排他モデル(第 2 部)

実装

- `UIClipTrack.h` を削除し、`UIClipCondition` / `UIAnimationFinishMode` を `UIAnimationClip.h` へ
- `UIAnimationClip` を §4.1 の形に。`loopSkipFirst` / `restoreOnComplete` を削除
- `UIAnimationComponent::clips` を private の `std::vector` にし、§5.1 の編集 API を用意する(runtime は P2)
- `UIAnimationSerializer` を §4.4 に合わせ、`group` / `conditionParam` をロード時にハッシュ化。`LoadAll` は全置換。§4.5 の検証
- 描画コンポーネント排他を §2.1 の 5 箇所へ
- `UIAnimationTrack::Apply` / `ReadFrom` を「持っている 1 つ」へ統一
- `.vcxproj` の更新

P1 着手時に確定した補足(2026-09-17)

- **ランタイムは暫定版。** 本命は P2 で書くが、P1 の間もエンジンはビルド・動作する必要がある。
  `UIAnimationComponent::Update()` は旧ロジックを新構造へ写す(クリップ単位で条件評価・ループ・完了。
  各クリップが自分の `duration` を使う。`finish == Restore` は起動時スナップショットへ戻す。
  レイヤー / serial / 基準値は持たない)。公開 API は §5.2 の署名にし、`runtimes_` は §5.1 の
  並列 vector と再構築契約で持つ(`activationSerial` は 0 のまま)。`conditions_` のキーは `uint32_t`
- **Animation Editor は 3 階層へ最小限追従させる。** P3 で UI Editor に統合するが、P2 の評価に
  エディタが要る。ClipTrack の層を消し、condition / group / loopFrom / finish をクリップ側の UI に出す。
  選択はインデックス(`selClipIdx_` / `selTrackIdx_` / `selKeyframeIdx_`)。`selClipTrackIdx_` /
  `ctNameBuf_` / `condParamBuf_` の共有バッファは削除する。見た目や配置は変えない。
  condition / group の変更は必ず `SetClipCondition()` / `SetClipGroup()` を通す
- **§4.5 の検証は `UIAnimationSerializer::Validate()` に置く**(新規ファイルは作らない)。
  `static bool Validate(const std::vector<UIAnimationClip>& clips, std::vector<std::string>& errors)`。
  `LoadAll()` は全クリップを読んでから `Validate()` にかけ、違反したクリップを捨てて残りを
  `ReplaceAllClips()` で渡す。エディタからは P3 で呼ぶ。`Exit` グループの判定は `aqHash32("Exit")`
  との比較(§8.1 のグループ名を `UIAnimationClip.h` に定数として置く: `kUIAnimGroupEnter` / `kUIAnimGroupExit`)
- **描画コンポーネント排他の判定方法。** `IUIComponent` に仮想関数は足さない。`UIObject.h` に
  `template<class T> inline constexpr bool kIsUIRenderComponent = false;` を置き、Image / NineSlice /
  CircleGauge を前方宣言して特殊化で `true` にする。`UIObject::HasRenderComponent()`(非テンプレート、
  `UIObject.cpp` で 3 種を見る)を足し、`AddComponent<T>()` は `if constexpr (kIsUIRenderComponent<T>)`
  でデバッグビルドの `assert(!HasRenderComponent())` を通す。ローダ・シリアライザ・`+ Component` も
  `HasRenderComponent()` で判定する
- **警告は `EnginePrintf()`** で `[UIAnim]` / `[UIDocument]` のタグを付けて出す(エンジンに他のログ手段が無い)

評価

- [x] `groupName` が空のとき `group == aqHash32(name)` になる(`LoadClip` / `SetClipGroup` / 改名時。エディタは `(= name)` と表示。Mac 2026-09-17)
- [x] `conditionParam` が空の Bool クリップをロードすると警告が出て捨てられる(`[UIAnim] clip discarded: ...` を確認)
- [x] 同一 Clip 内の `property` 重複、同一 group 内の `property` 重複が検証で止まる(後発側が捨てられる)
- [x] `Game/Assets/UI/` 配下に描画コンポーネントを 2 種以上持つノードが 0 件(63 ノード走査、ref 展開後も 0 件)
- [x] 手書き JSON で 2 種以上を付けたノードをロードすると 2 つ目が拒否され、警告が出る(`[UIDocument] node 'DupRender': ... rejected`)
- [x] Windows / D3D11 でビルドが通り、警告が増えていない(P6 で消化。D3D11 / D3D12 / Vulkan の Debug が通り、C4099 を潰して新規警告 0)

P1 の実装で決めたこと(設計書に無かった判断)

- Clip 名の重複は **先に出現した方を残し、後発を違反にする**。同じ起動単位の `property` 重複も後発側が違反
- ハッシュ衝突の検証は `group` と `conditionParam` を **別の名前空間**として集計する(照合先が `Play()` と `SetCondition()` / `Trigger()` で別)
- キーフレーム時刻が `duration` を超えるものは検証エラーにせず、`LoadAll()` が警告して clamp する
- `Validate()` に加えて、違反クリップの index を返す `ValidateDetailed()` を用意した(`LoadAll` が捨てる対象を決めるのに使う)
- 暫定 `Update()` は非ループの Bool クリップが真の間 0 秒から再スタートする旧不具合(§1.4)を **意図的に残している**。P2 のレイヤー評価で消す
- `UIDocumentSerializer` の `"animation"` 出力条件は `GetClips().empty()` に追従した

### P2: レイヤー評価と基本自動フック(第 2 部)

実装

- §5.1 の並列 runtime と再構築契約、§5.2 の公開 API(`TriggerTrack` / `currentClip_` 廃止)
- §5.3 の状態遷移。起動時の時刻 0 サンプル適用
- §6 の activationSerial / 2 段階処理 / 基準値ライフサイクル
- `IsGroupPlaying()` / `IsAnimationGroupPlaying(root, group)`
- §8.1 の自動フック(Enter / Hover / Pressed / Focused / Click)

P2 着手時に確定した補足(2026-09-17)

- **runtime の状態。** `ClipRuntime { float time; bool active; bool completed; uint32_t activationSerial; }`。
  `active` = レイヤーが載っている、`completed` = 非ループで終端に達した(Bool の「最終値を保持したまま残る」用。
  Manual / Trigger は完了と同時に `active = false` になるので `completed` は Bool 専用)。P1 の `snapshot` / `triggered` は削除
- **serial の発行。** コンポーネントに `uint32_t nextSerial_ = 0`。`Play(group)` / `Trigger(param)` /
  Bool の false → true でそれぞれ `++nextSerial_` を 1 回。同じ `Play(group)` の全クリップは同じ serial
- **基準値。** `std::unordered_map<UIAnimatedProperty, float> baseValues_`。§6.4 のとおり、適用段階で
  「active なレイヤーが 1 本以上あるプロパティ」を集め、初めて現れたプロパティは UIObject の現在値を取り、
  レイヤーが消えたプロパティは基準値を書いてから捨てる。勝者は serial 最大(同 serial は `clips_` の後ろが勝つ。
  ロード時に §4.5 が弾いているので実行時のログは出さない)
- **Hold の確定。** 非ループ Manual / Trigger が `finish == Hold` で完了したら、そのクリップの各 Track について
  `baseValues_[p] = track.Sample(duration)` を書いてから `active = false`(上に別レイヤーがあっても行う。§6.4-3)。
  Restore と `Stop()` は基準値に触らず `active = false` だけ
- **2 段階処理の入口は 1 つ。** `Update(dt)` = 状態更新(`AdvanceRuntimes(dt)`)+ 適用(`ApplyLayers()`)。
  **`Play()` / `Trigger()` / `SetCondition()` は状態を変えた直後に `ApplyLayers()` を呼ぶ**(§5.3 の
  「時刻 0 のサンプルをその場で適用」。Push の Flush が OnEnter 後に Play する経路でも、そのフレームの描画に間に合う)。
  `SetCondition()` は値が変わらなければ何もしない(毎フレーム呼ばれても無駄がない)
- **Bool の遷移は `SetCondition()` の中で起こす**(false → true で起動、true → false で解除)。加えて
  `AdvanceRuntimes()` でも「条件が真なのに非 active」を拾って起動する(クリップの追加やロード後に条件が既に真の場合)
- **Trigger は再生中でも即座に先頭から**(`time = 0`、新 serial)。pending フラグは持たない
- **構造変更(AddClip / RemoveClip / MoveClip / ReplaceAllClips)** は StopAll + 全 runtime 再構築のあと
  `ApplyLayers()` を呼び、Bool / Trigger のレイヤーも外れた状態で基準値を書き戻す
- **定数**(`UIAnimationClip.h`): `kUIAnimGroupEnter` / `kUIAnimGroupExit`(P1 済)に加えて
  `kUIAnimCondHover = aqHash32("Hover")` / `kUIAnimCondPressed` / `kUIAnimCondFocused` / `kUIAnimTriggerClick = aqHash32("Click")`
- **`UIAnimationSystem`** に `static void PlayGroup(UIObject* root, uint32_t group)`(ルート以下の全コンポーネントに `Play`)と
  `static bool IsAnimationGroupPlaying(const UIObject* root, uint32_t group)` を足す
- **フック点。** `UIScreenManager::FlushPendingOps()` の Push / Replace で `OnEnter()` が返った直後に
  `UIAnimationSystem::PlayGroup(root, kUIAnimGroupEnter)`。`UIInputSystem` は `FireHoverEnter / Exit` で
  `SetCondition(kUIAnimCondHover, v)`、`isPressed` の代入箇所で `SetCondition(kUIAnimCondPressed, v)`、
  `FireFocusEnter / Exit` で `SetCondition(kUIAnimCondFocused, v)`、`FireClick` で **callback より前に**
  `Trigger(kUIAnimTriggerClick)`。`ResetButtonState()` は 3 条件を false にする。橋渡し先は同じ UIObject の
  `UIAnimationComponent` があるときだけ。`UIButtonComponent` にフィールドは足さない
- **エディタとの関係。** Animation Editor のプレビューは引き続き独自スナップショットで直接書く。runtime の
  レイヤーが載っているプロパティはフレームごとに runtime が先に書き、プレビューが後から上書きする(描画順)。
  P3 で統合するまでは、プレビュー中は Enter などのクリップを再生しない運用にする

評価

- [x] §6.5 の 6 項目が実機で期待どおり(1・3・4・5 は Mac で画素計測。2 は `Play()` の呼び手が無いため、6 は 1 フレームの撮影が不能なためコードで確認。2026-09-17)
- [x] Bool / Trigger クリップが `Play()` なしで動く。手書き JSON 1 枚で「出現 + ホバー光り」が動く(Open + Hover + Pressed + Click の 4 本を JSON だけで)
- [x] `Play(group)` でグループ内の Manual クリップが全部、それぞれの長さで走る(コード確認: `Play` は group 一致を全部起動し、`AdvanceRuntimes` は各クリップの `duration` を見る)
- [x] 完了判定がグループ単位で、Default トラック特例が消えている(`IsGroupPlaying` = group の Manual が 1 本でも active)
- [x] `Stop(group)` / `StopAll()` で途中値が捨てられ、基準値に戻る(コード確認: `active = false` のあと `ApplyLayers()` が基準値を書く。呼び手はまだ無い)
- [x] `Update()` から `std::string` の比較が消えている(0 箇所)
- [x] エディタで Clip を追加・削除・並べ替えた直後に再生しても runtime 参照が壊れない(Click 再生中に + Clip × 2、- Clip × 2。落ちず、直後の Hover も効く)
- [x] Keyframe ドラッグ中に再生が止まらない(`isPlaying_ = false` は対象変更 / 終端 / スクラブ / 一時停止 / Reset だけ)
- [x] `UIButtonComponent` の hover / press / click が JSON だけでアニメになる(C++ 0 行)
- [x] Enter が `OnEnter()` の後に起動し、ゲーム側の初期値を基準値に取っている(`PlayGroup` は `OnEnter()` の直後。基準値は最初の `ApplyLayers()` で `ReadFrom`)
- [x] Windows / D3D11 でビルドが通り、警告が増えていない(P6 で消化。D3D11 / D3D12 / Vulkan の Debug が通り、C4099 を潰して新規警告 0)

P2 の実装で決めたこと・直したこと

- `Play()` / `Trigger()` は対象が 0 本でも `ApplyLayers()` を呼ぶ。`Stop()` / `StopAll()` / `SetCondition()` は状態が変わったときだけ呼ぶ
- `AdvanceRuntimes()` の Bool 追随(条件が真なのに非 active)は runtime ごとに serial を発行する(`SetCondition()` 経由の通常経路は起動単位で 1 つ)
- 最後のレイヤーが外れたプロパティの基準値は、そのプロパティの Track を持つクリップが残っていなくても書き戻す(一時 Track で `Apply`。`RemoveClip` 直後に途中値が残らない)
- **`UICanvasComponent::clientSize` が一度も更新されていなかった**(コメントは「毎フレーム更新」)。既定 1920x1080 のままなので
  ウィンドウが 1920x1080 以外だとマウスの HitTest がずれ、Mac(1280x752)では UIButton の hover が一度も当たらない。
  `UIInputSystem::HitTest()` で `Engine` の画面サイズを毎フレーム入れるようにした(描画側は `resolution` をウィンドウ全体へ
  引き伸ばすだけなので、これで一致する)。P2 の範囲外だが自動フックの評価に必須だった

### P2B: Exit 待機付き画面遷移(第 2 部)

実装

- §9.2 の状態機械を `UIScreenManager` に。入力停止、`OnUpdate` 停止、タイムアウト、op 列の保持
- Exit グループが無い画面は即時遷移
- `ReloadTopDocument()` が状態機械を通らないことを確認

P2B 着手時に確定した補足(2026-09-17)

- **状態は `UIScreenManager` のメンバ 3 つ。** `bool exitWaiting_`、`float exitElapsed_`、`PendingOp exitOp_`(待機中の Pop / Replace)。
  待機対象は常に先頭画面(Pop / Replace は先頭にしか作用しない)
- **`PendingOp::Type` に `Reload` を足す。** `ReloadTopDocument()` は `Replace` ではなく `Reload` を積み、
  従来どおり同じ Flush で `OnExit → OnDestroy → 破棄 → 生成 → OnCreate → OnEnter → Enter 起動` を行う(Exit 待機を通さない)
- **`FlushPendingOps()` の流れ。** `exitWaiting_` の間は何もしない(op は `pendingOps_` に残す)。Pop / Replace を処理するとき:
  1. `ExitStart`: `screen->exiting_ = true` → `UIContext::Get().GetInputSystem().ClearState()`(Hover / Pressed / Focused を落とし、
     Bool 条件を false にする)→ `OnExit()` → `UIAnimationSystem::PlayGroup(root, kUIAnimGroupExit)`
  2. `IsAnimationGroupPlaying(root, kUIAnimGroupExit)` が false(Exit クリップ無し)なら、その場で `ExitDone` へ
  3. true なら `exitWaiting_ = true`、`exitOp_` に op を保存し、**残りの op を `pendingOps_` の先頭へ戻して** Flush を抜ける
- **`ExitDone`**: `OnDestroy()` → ルート破棄 → `stack_.pop_back()` → Pop なら新しい先頭に `OnResume()`、Replace なら次画面を
  `CreateScreen → OnCreate → OnEnter → Enter 起動`。そのあと同じフレームで `FlushPendingOps()` を続け、待機中に積まれた op を順に処理する
- **`Update()` の流れ。** (1) `exiting_` でない画面だけ `OnUpdate(dt)`(2) `UIAnimationSystem::Update`(3) `exitWaiting_` なら
  `exitElapsed_ += dt` し、Exit グループが止まったか `exitElapsed_ >= kExitTimeoutSec` なら `ExitDone`(タイムアウト時は
  `EnginePrintf("[UIScreen] ...")` で警告)(4) `FlushPendingOps()`(5) 画面変化があれば `ClearState()`
- **タイムアウトは `static constexpr float kExitTimeoutSec = 2.0f;`**(`UIScreenManager.h`)。§13 の未決を 2.0 秒で確定する
- **`UIScreen` に `bool IsExiting() const`** を足す(private `exiting_`、`UIScreenManager` が friend で書く)。
  `UIInputSystem::HitTest()` は `IsExiting()` の画面ではヒットを返さず(`blocksRaycast` なら走査を止める)、
  `UpdateFocus()` は対象画面が `IsExiting()` なら何もしない。これで Exit 中の Hover / Click が割り込まない
- **`Push` は待機の対象外**(Enter 側の演出は Push 先が持つ)。`Back` は従来どおり `OnBack()` の結果で Pop に変換され、その Pop が状態機械を通る

評価

- [x] Exit クリップのある画面を Pop → Exit が最後まで描画されてから破棄される(タイトルの `Replace("Loading")` で 1.5 秒の
  フェードを画素計測: +0.4s で alpha 0.69、+0.9s で 0.21、+1.8s で Loading。Mac 2026-09-17)
- [x] Exit クリップの無い画面を Pop → 従来と同じフレームで破棄される(+0.3s で Loading)
- [x] Exit 中にマウスを動かしても Hover が割り込まない(Exit 中にホバーしても Hover クリップの ColorB が出ない)
- [x] Exit 中に画面の `OnUpdate()` が呼ばれない(コード確認: `exiting_` の画面はスキップ)
- [x] Exit 中に Push を積む → Exit 完了後に順に処理される(コード確認: 待機中は Flush が op を残し、`CompleteExit()` 直後の
  同フレームで残りを処理する)
- [x] ループする Exit クリップを置くと検証で止まる(P1 の `Validate`)。検証を迂回した場合はタイムアウトで進む
  (エディタで Loop を付けて Space → `[UIScreen] exit animation timed out (2.0s): 'AquaDashTitle'` が出て遷移)
- [x] クリックで遷移するボタンの Click 演出が見える(コード確認: `FireClick` は callback より前に Trigger し、callback が積んだ
  Replace は同フレームの `BeginExit` で Exit 待機に入るので画面が残る。Exit クリップが無い画面では従来どおり同フレームで消える。
  ゲーム側にクリックで遷移するボタンがまだ無いため実測なし)
- [x] エディタの Reload に Exit 演出が挟まらない(Exit クリップがある画面で Reload → +0.3s で白のまま)
- [x] Windows / D3D11 でビルドが通り、警告が増えていない(P6 で消化。D3D11 / D3D12 / Vulkan の Debug が通り、C4099 を潰して新規警告 0)

P2B の実装で決めたこと

- `BeginExit()` はスタック非空を呼び出し元が保証する。Exit 待機の開始も「画面変化あり」として扱い `ClearState()` を呼ぶ(冪等)
- ゲーム側の状態機械(AquaDash の `TitleState`)は `Replace("Loading")` を積んだ直後に自分の状態も進める。UI の Exit 待機中も
  ゲーム側はロードを始めるので、Loading 画面が出る頃にはロードが進んでいる(待機が体感を悪くしない)

### P3: 統合エディタ(第 3 部)

実装

- Inspector に `[Properties] [Animation]` タブ。§10.3 の通常表示と Advanced
- 下部 Timeline を §10.4 の形に。選択インデックス 1 本化。プレビューを `Play()` 経由に
- `Play when` の表示範囲を §10.3 の表に従って制御(Exit は P2B 完了まで Disabled)
- Animation Editor を独立パネルから UI Editor の一部へ。`IDebugRenderable` 登録を外す
- TextStyle の入口を Text Inspector へ(§10.5)

P3 着手時に確定した補足(2026-09-17)

- **`UIAnimationEditor` は `IDebugRenderable` をやめ、`UIEditorDebugPanel` が値で持つ部品にする**(ファイル名は変えない)。公開 API:
  `void DrawAnimationTab(UIObject* obj)`(Inspector の Animation タブの中身: Clip 一覧 + 選択 Clip の設定 + Advanced + 検証エラー)/
  `void DrawTimelinePanel(UIObject* obj, float dt)`(下部: タイムライン + Keyframe Inspector + プレビュー)/
  `void OnTargetChanged(UIObject* prevObj)` / `void Reset()`(Reload 直前)/
  `static bool ValidateTree(const UIObject* root, std::vector<std::string>& errors)`(Save 前の検証。ツリーの全 `UIAnimationComponent`)
- **Inspector は `ImGui::BeginTabBar` の `[Properties] [Animation]`。** Animation タブで `UIAnimationComponent` が無ければ「Add Animation」ボタン。
  下部 Timeline は Animation タブが選ばれていて、かつコンポーネントがあるときだけ、固定高さ(260px)の子ウィンドウで出す
- **`Play when` の選択肢と内部設定**(§10.3 の表)。`Enter` / `Exit` = Manual + group 固定、`Hover` / `Pressed` / `Focused` = Bool + param 固定、
  `Click` = Trigger + param 固定、`Manual` = Manual + 自由 group(Group 欄を出す)、どれにも当てはまらない組み合わせは `Advanced` と表示する。
  変更は必ず `SetClipCondition()` / `SetClipGroup()` を通す。`Exit` は P2B 済みなので有効。**Disabled にする仕組みは残す**
  (`kExitTransitionReady = true` の定数と、false のときの `(available after screen transition support)` の理由表示)
- **Advanced(既定で畳む)**: condition の Combo(Manual / Bool / Trigger)、conditionParam と group の自由入力、ハッシュ値の読み取り専用表示(`0x%08X`)
- **`After finish` の表示は `Keep`(Hold)/ `Return`(Restore)。** Duration は `%.2f sec`
- **検証。** `UIAnimationSerializer::ValidateDetailed()` を選択コンポーネントに対して毎フレーム掛け、違反クリップは一覧で赤字、
  選択中クリップの違反はタブ内に赤字で列挙する。UI Editor の Save は `ValidateTree()` が false なら保存せず
  `Save blocked: N animation error(s)` を status に出す。**`Validate` のメッセージは英語に改める**(ImGui の既定フォントに日本語グリフが無い)
- **Timeline 左の一覧は 2 段**: Manual は `groupName`(空なら name)の見出しごと、Bool / Trigger は `Play when` の見出しごと。データは平ら。
  Clip 行に Play when / Duration / Loop を出す
- **プレビューはランタイム経由。** `Play`(Manual: `Play(group)`、Trigger: `Trigger(param)`、Bool: `SetCondition(param, true)`)/
  `Stop`(Manual: `Stop(group)`、Bool: `SetCondition(param, false)`、Trigger: 何もしない)。スクラブ(時刻を指定して値を直書き)は
  キー編集の補助として残し、`ApplyScrub` は従来どおり編集側のスナップショットで戻す。プレビュー中の勝者は
  `UIAnimationComponent::FindWinnerClip(property)` で引き、Track 行の先頭に `*` を出す。ツールチップに `serial` と `time`
  (`IsClipActive` / `GetClipTime` / `GetClipSerial`。この 4 つの問い合わせ API を P3 で足す)
- **TextStyle の入口。** `TextStyleEditorPanel` も `IDebugRenderable` をやめ、`UIEditorDebugPanel` が値で持つ。Text Inspector の
  `Style Path` の横に `[Edit]`。押すと `Open(path)` → `ImGui::BeginPopupModal("TextStyle Editor")` の中に既存の一覧 / プロパティ /
  プレビューを描く(`RenderPopup()` を UI Editor が毎フレーム呼ぶ)。Close ボタンで閉じる
- **`Core/Application`** から `uiAnimationEditor_` / `textStyleEditorPanel_` のメンバと登録を外す。トップメニューの `UI` には `UI Editor` だけ残る
- `.vcxproj` / `.filters` は変更なし(ファイルの増減が無い)

評価

- [x] UI Editor だけで、ボタンに Hover / Click アニメを付けて保存し、再起動後に動く(Mac 2026-09-17: Add Animation → + Clip →
  Play when: Hover → + Track → ColorA → 右クリックでキー追加 → Save。再起動後にホバーで alpha 0 になるのを画素計測。Click も同じ経路)
- [x] condition / conditionParam / ハッシュ値が通常表示に出ない(Name / Play when / Duration / Loop / After finish / Advanced(畳み)だけ)
- [x] `Play when: Exit` が P2B 未完了時は Disabled で理由が出る(`kExitTransitionReady` を false にしたときの経路。P2B 済みなので今は有効)
- [x] 同じ起動単位で同じプロパティを使うと赤字になり Save が止まる(Clip 名と行が赤字、`Save blocked: 2 animation error(s)`)
- [x] プレビューでループ・条件・`finish` の実挙動が確認できる。勝者 Clip が見える(Play / Stop はランタイム経由。Track 行の `*` が勝者)
- [x] `ctNameBuf_` / `condParamBuf_` の全トラック共有が構造変更で消えている(static バッファ 0 件)
- [x] トップメニューに UI Animation Editor / TextStyle Editor の独立項目が無い(`UI` メニューは `UI Editor` だけ)
- [x] Windows / D3D11 でビルドが通り、警告が増えていない(P6 で消化。D3D11 / D3D12 / Vulkan の Debug が通り、C4099 を潰して新規警告 0)

P3 の実装で決めたこと・直したこと

- `UIAnimationComponent::AddClip()` が `group == 0` のクリップを受けたら §4.3 の規則で埋める(エディタの `+ Clip` が即エラーにならないように)
- `Validate()` のメッセージは英語(ImGui にそのまま出す)
- `+ Track` / `-` / property の Combo はタイムライン右ペインの先頭に置いた(Track を編集する場所に寄せた)
- TextStyle の `[Edit]` はモーダルポップアップ。既存の一覧 / プロパティ / プレビューをそのまま中に描く
- UI Editor の既定サイズ(700x560)では Animation タブ + Timeline で縦が足りない。ウィンドウを広げれば使える。既定サイズの拡大は後続で

---

### P4: プリセット(第 4 部、§14)

実装

- Animation タブに `+ Preset` メニュー(FadeIn / FadeOut / PopIn / SlideIn / Blink / Shake)。選ぶと §14 の規則で Clip を生成して
  `AddClip()` → `SetClipCondition()` / `SetClipGroup()` を通し、選択して dirty を立てる
- 生成はエディタ内の関数群(`UIAnimationEditor.cpp` の無名 namespace)。新規ファイルは作らない
- 現在値は `UIAnimationTrack::ReadFrom(obj)` で読む(相対値モードは §12 の後続)

評価

- [x] 空の AnimationComponent に 6 種をそれぞれ単独で挿入すると検証エラーが 0 件で、Save できる(FadeIn と PopIn は
  どちらも Enter で ColorA を使うので、同時に挿入すると §4.5 どおり赤字になる。これは仕様。Mac 2026-09-17:
  FadeIn / Shake / FadeOut / SlideIn / Blink は赤字なし、PopIn と 2 本目の FadeIn は赤字で `Save blocked`)
- [x] FadeIn を挿入して Save → 再起動で Enter 時にフェードインする(Reload 直後の連続撮影で alpha 0.25 → 0.69 → 0.95 → 1.0)
- [x] Shake を挿入 → クリックで揺れ、終わると元の位置に戻る(左端の白の量が揺れて静止時の値に戻る)
- [x] 現在値を基準に生成される(Position X が -600 の Shake のキーが -600 / -592 / -608 …)
- [x] 同名クリップがあれば連番になる(`FadeIn1`)
- [x] Windows / D3D11 でビルドが通り、警告が増えていない(P6 で消化。D3D11 / D3D12 / Vulkan の Debug が通り、C4099 を潰して新規警告 0)

P4 の実装で気づいたこと

- Duration を伸ばしてもキーの時刻は動かない(0.3 秒のキーのまま末尾でホールド)。キーの時間を Duration に比例して伸ばす操作は §12 の後続
- ~~Play when が Enter / Exit のときも Group 欄が出る(§10.3 は Manual のときだけ)~~ → **P6 で修正(§16.5)**

---

### P5: ベクタトラック(第 4 部、§15)

実装

- Timeline の行を「Row」の列として組み立てる(§15)。X / Y の両 Track を持つ組は 1 行(`Position` / `Scale` / `SizeDelta`)にまとめる
- Row 上のキー操作(追加 / ドラッグ / 削除 / 右クリック)は組の両 Track に同時刻で効く
- Keyframe インスペクタは Row のキーを「時刻 + X 値 + Y 値 + Ease」で編集する
- `+ Track` に `Position (X,Y)` / `Scale (X,Y)` / `SizeDelta (X,Y)` を足す(両 Track を追加)
- データ(`UIAnimationTrack` / `UIKeyframe`)と JSON は変えない

評価

- [x] PositionX / PositionY の両方を持つクリップで Timeline が 1 行 `Position` になる(Mac 2026-09-17。`ColorA` は単独行のまま)
- [x] `Position` 行でキーを追加 → JSON で PositionX / PositionY の両方に同時刻のキーが入る(t=0.488 に X=-502.4 / Y=251.2。値は `Sample`)
- [x] `Position` 行のキーをドラッグ → 両方の時刻が動く(1.0 → 0.791 が両 Track に)
- [x] Keyframe インスペクタに X / Y の値が並び、Ease は両方に効く(Time / X / Y / Ease。ツールチップにも t / X / Y)
- [x] 片方しか無いクリップ(SlideIn プリセット)は従来どおり `PositionX` の単独行
- [x] 既存の JSON がそのままロードできる(形式不変。フィクスチャをそのまま読んでいる)
- [x] Windows / D3D11 でビルドが通り、警告が増えていない(P6 で消化。D3D11 / D3D12 / Vulkan の Debug が通り、C4099 を潰して新規警告 0)

P5 の実装で決めたこと・直したこと

- キー追加の値は「Track にキーがあれば `Sample(時刻)`、無ければ現在値」に統一した(単独行も同じ)
- Keyframe インスペクタの Time を編集し終えたら自動で並べ替える(組の両キーが未整列のまま残らないように)
- Track 一覧の `-` で erase したあと `continue` していて次の Track を 1 本飛ばしていた既存バグを `break` に直した
- 寸法の応急処置: Timeline 領域は `max(260px, 45%)`、行キャンバスの子ウィンドウは内容分の高さを持ち右ペインがスクロールする
  (§13 の寸法修正の一部。UI Editor の既定サイズ拡大と Group 欄の非表示は未着手)

---

### P7: オートキー(第 6 部、§17)

実装

- Preview Controls に `Rec` トグル(§17.5)。ON の瞬間に録画基準値を取る
- 録画中は Inspector のタブに関わらず Timeline を出す(Properties タブで値をいじりながら行を見られるように)
- Properties タブの録画対象ウィジェット(§17.3)の編集で、選択 Clip のスクラブ時刻へキーを入れる
- 対象 Track が無ければ作り、スクラブ時刻 > 0 なら 0 秒にも基準値のキーを置く(§17.4)
- データ(`UIAnimationClip` / `UIAnimationTrack` / JSON)は変えない

評価

- [x] Clip 未選択では Timeline 下部に `Rec` が出ない(Preview Controls ごと描かれない)
- [x] Rec ON → スクラブを 0.5 秒へ → Position をドラッグ → PositionX / PositionY に 0 秒と 0.5 秒のキーが入る
      (保存 JSON で `PositionX [(0, -600), (0.5, -530)]` / `PositionY [(0, 300), (0.5, 300)]`。0 秒が録画開始時の値)
- [x] ドラッグし続けてもキーは 1 本のまま(14 ステップのドラッグで 0.5 秒に 1 本)
- [x] 既に Track があるプロパティでは 0 秒キーが勝手に増えない
      (Color をいじると既存の `ColorA` は `[(0, 1), (0.5, 0.588), (1, 0.2)]` で 0 秒は元のまま。
       新規の `ColorR` / `ColorG` / `ColorB` は 0 秒 + 0.5 秒の 2 本)
- [x] Rec ON のまま Properties タブへ移っても Timeline が出たままで、打ったキーがその場で見える
      (Properties タブ先頭に `● REC  RecClip  @ 0.50s` の赤帯)
- [x] Rec OFF にしても値は戻らず、`Reset` を押すと録画開始前の値へ戻る(下の「評価で直したもの」を参照)
- [x] 選択オブジェクトを変えると Rec が落ちる(Timeline も消え、戻ると Rec は OFF・スナップショットも無い)
- [x] スクラブ再生中に Rec を ON にすると再生が止まる(0.422 → 0.572 で停止し、1.2 秒後も 0.572)
- [x] 録画対象外のウィジェット(Pivot / UV Rect / Flip / texturePath)ではキーが増えない(Track 一覧が 6 本のまま)
- [x] Windows / D3D11・D3D12・Vulkan の Debug ビルドが通り、警告が増えていない(新規警告 0。残りは Bullet と RenderThread 経由の既存分)

評価は Windows / D3D11 / Debug で実施(2026-09-17)。段取りは §16.6。

P7 の評価で直したもの

- **`Reset` が録画で作った Track のプロパティを戻さなかった。** `TakeSnapshot()` は「その時点で Clip にある Track」しか
  控えないため、録画中に新しくできた `PositionX` / `PositionY` は復元対象から漏れていた(Color は元から Track があるので戻った)。
  Rec を ON にした時点で録画基準値を `snapshot_` へ足すようにして直した。既に入っている分は先の値を優先する

P7 の実装で決めたこと・直したこと

- `Rec` の Disabled 分岐は到達しないので入れない。Preview Controls は `selClipIdx_ >= 0` のときしか描かれない
- `Rec` を ON にしたらスクラブ再生を止める(打つ時刻が動かないように)
- 赤帯の `●` は `"â"` のバイトエスケープで書く。ソースは UTF-8 / BOM 無しで、
  MSVC が narrow リテラルを実行時コードページで解釈するため。フォントのグリフ範囲に Geometric Shapes が要る
- 録画基準値は Rec OFF で捨てる(次の ON で取り直す)
- Color は R / G / B / A の 4 プロパティをまとめて渡す。描画コンポーネントは排他なので解決先は一意

---

### P8: Ease 拡充(第 7 部、§18)

実装

- `EaseType` に `Back` / `Elastic` / `Bounce` の In / Out / InOut を足す(§18.1)
- `Bezier` を `Smoothstep` へ改名する。読み込みは `"Bezier"` も受ける(§18.2)
- `EvaluateEase()` を公開し、エディタが同じ関数で曲線を描けるようにする
- Keyframe インスペクタに曲線プレビューと「この Ease が効く区間」の表示を足す(§18.3)
- Ease の選択肢をグループ分けし、最後のキーでは Disabled にする(§18.4)

評価

自動で確認する(一時フックで値を書き出して照合。フックはコミットしない):
- [x] `EvaluateEase` の 14 種 × `t` ∈ {0, 0.25, 0.5, 0.75, 1} が §18.1 の参照値と 1e-4 以内で一致する(端点 0 / 1 を含む)。
      最大差 4.4e-5(参照値の 4 桁丸め分)。14 種とも `E(0) = 0`、`E(1) = 1`(7 桁表示で一致)
- [x] `BackOut` の最大値が 1.0 を超える(0.001 刻みで 1.10000)
- [x] **中間キーで値が飛ばない**: 3 キー(0 秒 = 0、0.5 秒 = 10、1 秒 = 20)の Track を 14 種それぞれで作り、`Sample(0.5)` が 10.000(許容差 1e-4)、
      `Sample(0.5 ± ε)` と 10 の差が ε に比例して縮む(ε = 1e-4 → 1e-5 で差が 1/5 以下、ε = 1e-5 で 0.01 未満)。
      Elastic は端点の傾きが急で、ε = 1e-4 では正しい実装でも差が約 0.014 になる(Codex 再レビューで検算)ので、固定の閾値にしない。
      **実装後の基準の修正(2026-11-10)**: `ElasticIn` / `ElasticInOut` は比例して縮まない(比 0.35 / 0.97)。原因は実装ではなく式で、
      端点の分岐で `E(0) = 0` にした直後の `t → 0+` が `−2^−10·sin(−10.75·c4)` ≈ **−0.00049**(`ElasticInOut` は ≈ +0.00009)になり、
      区間の 0.05% 以下の段差が残る(`ElasticOut` / `ElasticInOut` の `t → 1−` 側も同じ大きさ)。目には見えないので、
      Elastic 系だけ「ε = 1e-5 での差が区間の 0.05% 以内」を合格にした。結果: 13 種は `Sample(0.5)` = 10.000000、比 0.100 以下(InOut 系は差 0)、
      `ElasticIn` / `ElasticOut` は ε = 1e-5 で 0.0049(区間 10 の 0.049%)、`ElasticInOut` は 0.00085(0.0085%)
- [x] **保存 → 再読込**: 14 種を 1 本ずつ持つ Clip を Save し、JSON の `ease` が 14 種の名前で書かれ、Reload 後の各キーの `ease` が元と一致する
      (`SaveClip` → 文字列化 → `ParseString` → `LoadClip` で 14/14 一致)
- [x] **旧名**: `"ease": "Bezier"` のキーが `Smoothstep` として読め、`Sample()` が改名前のビルドと同じ値(上の 5 点。許容差 1e-6)。保存すると `"Smoothstep"` で書かれる
      (改名前の式 `t²(3 − 2t)` との差 0。保存し直した JSON に `Bezier` は無く `Smoothstep` がある)
- [x] **プリセットの回帰**: §14 の全プリセットを同じノード(初期値を固定)に作って Save した JSON が、P8 の前後でバイト一致する(Ease 名は既存 4 種のみなので変わらないはず)。
      `MakePresetClip` の 6 種(ノード無し = 既定値)を `SaveClip` して書き出し、P8 前(`5d1c6b7` の worktree)と後で `cmp` 一致(3906 バイト)
- [x] Mac(Metal)の Debug / Release ビルドが通り、警告が増えていない(変更した 6 ファイルからの警告 0)

エディタで確認する(撮影または保存 JSON):
- [x] Ease の選択肢が 14 種出て、Linear / 基本 / Back / Elastic / Bounce の間に区切りがある(撮影)
- [ ] グループをまたいで選んでも、選んだ名前が保存 JSON に出る(添字ずれが無い: **各グループの先頭と末尾を 1 つずつ**選んで確認)。
      **3 件だけ確認**: Combo で `Smoothstep`(基本の末尾)と `BounceOut`、右クリックで `BackIn`(Back の先頭)を選んで Save し、JSON にその名前が出た。
      残りの組み合わせは未確認(Combo / メニューとも同じ `EASE_TABLE` を走査して `entry.type` を書くので、添字ずれは構造上起きない)
- [x] `BackOut` のプレビュー曲線が [0, 1] の箱の上へはみ出す。`BounceOut` は箱の中で 3 回跳ね、`ElasticOut` は箱の上下へ振れて収束する(撮影。`BackIn` は箱の下へ沈む)
- [x] Timeline の右クリックメニューの `Ease` も同じ 14 種・同じ区切りで、選んだ名前が保存 JSON に出る(`BackIn` で確認。Ease が軸で違うキーではどれにもチェックが付かない)
- [ ] 最後のキーを選ぶと Combo と右クリックの `Ease` が Disabled になり、ツールチップに理由が出る。
      **Combo は確認**(ColorA の最後のキーで Disabled、ホバーで "Last key: there is no segment after this key, so Ease has no effect.")。**右クリック側は未撮影**
- [x] プレビュー下に区間の始点・終点の時刻が出る。X/Y の行で次のキー時刻が軸で違うときは軸別に 2 行出る。Ease が軸で違うと `(mixed)` が出る
      (t = 0 は次のキーが両軸 1.0 なので `0.00s -> 1.00s` の 1 行、t = 1 は `X: 1.00s (last key)` / `Y: 1.00s -> 2.00s`。どちらも `(mixed)` + `X/Y differ`)
- [x] X だけ最後のキーで Y に次のキーがある時刻では、Combo が有効のまま X の区間が `(last key)` になる(t = 1。t = 2 は Y だけで最後なので Disabled)

ユーザー確認(この Mac ではできない):
- [ ] Windows / D3D11・D3D12・Vulkan の Debug ビルドが通り、警告が増えていない

評価は Mac(Metal / Debug)で実施(2026-11-10)。数値は Sample に一時フック(環境変数で `EvaluateEase` / `Sample` / Serializer / `MakePresetClip` の結果を書き出す)、
エディタは Game のタイトル画面 JSON に一時ノード(X/Y でキー時刻と Ease が違う Clip)を足して撮影した。どちらもコミットしていない。

P8 の実装で決めたこと・直したこと

- **`BOTTOM_PANE_H` は 185**(設計の 150 では、曲線 90px + `X/Y differ` + 軸別の区間 2 行が入らず、下の行が切れてスクロールバーが出た。撮影で確認して直した)
- 区間表示の矢印は `->`(ImGui の既定フォントに `→` のグリフが無く `?` になる)
- 曲線プレビューはフィールド群の右(+250px)に `SetCursorPos` で置き、描いたあとカーソルを戻す。折れ線は 64 分割、描画領域でクリップ
- 区間の次キーは `xi + 1`(キーが時刻順である前提。未ソートのときは既存の `Sort` と同じ前提)
- Elastic 系の中間キーの基準を直した(上のチェックリスト。式由来の 0.05% 以下の段差)

---

### P9: 相対値(第 8 部、§19)

設計レビュー(Codex、2026-11-10。ランタイム側とエディタ側に分けて依頼):
- ランタイム: 1 回目 中 3・軽 2 → 全件反映 → 2 回目「指摘なし」。反映したもの: 下の Hold が相対の基準値を動かす挙動を仕様として明記 /
  基準値未取得の経路は `Play()` / `Trigger()` が即 `ApplyLayers()` を呼ぶため通らないので防御扱いにし評価から外した / 基準値の取り直し条件(Bool 残存・`StopAll`・`RemoveClip`)/
  バイト一致の対象を「P9 前後の Save 結果」に限定 / Restore と Hold を区別できる評価値
- エディタ: 1 回目 重大 2・中 2・軽 1 → 2 回目 中 2・軽 1 → 3 回目「指摘なし」。反映したもの: スナップショットの 3 規則
  (無効化で中身も消す・`RestValue` で使用前に補完・スナップショットを手放す経路で録画も止める)、Clip 選択変更の `SelectClip` への集約(`+ Clip` / `+ Preset` を含む)、
  オートキーの期待値を静止値基準で再定義、値表示の対象(ホバーツールチップ X/Y・単独行、右クリック見出し)
- 不採用は無し

評価は Mac(Metal / Debug)で実施(2026-11-10)。数値は Game に一時フック(`AQ_TEST_UI_RESAVE` で 3 画面とプリセットを保存し直す、
`AQ_TEST_RELATIVE_OUT` で UIObject + `UIAnimationComponent` を組んで `Update` を回す)、エディタはタイトル画面 JSON に一時ノード
(RelTest: X 絶対 / Y 相対 / Active、Other: X 絶対)を足して操作した。いずれもコミットしていない。評価中に画面がスリープして一度中断した。

P9 の実装で決めたこと(実装担当の判断を採用):
- `Active` / `TextCharCount` の `Rel` は、**既に ON のときは押せる**(手書き JSON やプロパティ変更で残った検証エラーを外せるように)。OFF のときだけ Disabled
- Clip の選択が範囲外になって解除される経路(`DrawAnimationTab` / `DrawTimelinePanel`)も `SelectClip` を通す
- `+ Preset` はプリセットを作る前に `DiscardSnapshot` を呼ぶ(スクラブ中の値をプリセットが拾わないように)
- 対象の変更でスクラブ時刻が 0 に戻り、Clip のテキスト欄も同期し直す(`SelectClip` を通る副作用)
- プロパティの Combo を変えても、相対 Track のキーは換算しない(設計に記述なし)
- Hold の注意書きの行数を `ComputeTimelineHeight` に足した


実装

- `UIAnimationTrack` に `bool relative = false` を足す。JSON は `"relative": true`(false のときは書かない。§19.2)
- ランタイム: 相対 Track の適用値を「基準値 + `Sample(t)`」にする。`CommitHold` も同じ式で確定し、基準値が未取得なら先に取る(§19.3)
- 検証: `Active` / `TextCharCount` の相対 Track を authoring エラーにする(§19.4)
- エディタ: Track 行の `Rel` トグル(切り替え時にキー値を換算して見た目を保つ)、スクラブ・キー追加・オートキー・インスペクタの換算(§19.5)
- プリセット(§14)は絶対値のまま。変えない

評価

自動で確認する(一時フックで UIObject + `UIAnimationComponent` を組み、`Update(dt)` を回して値を書き出す。フックはコミットしない):
- [x] **レイアウトに追従する**: PositionX = 100 のノードに、相対 Track(0 秒 = −50、0.5 秒 = 0、Manual / Hold)。`Play` 直後の適用値 50、完了後 100。
      ノードを 300 へ動かしてから再度 `Play` → 250 から始まり 300 で止まる(許容差 1e-4)。結果 50 / 100 / 250 / 300
- [x] **Restore と Hold の区別**: 最後のキーが +100 の相対 Track(0 秒 = 0、0.5 秒 = +100)。Restore なら完了後 100 に戻り、
      Hold なら `Play` のたびに 200 → 300 → 400 と進む(Hold の累積は仕様。§19.3)。結果 Restore 100、Hold 200 / 300 / 400
- [ ] **レイヤーの上に載る**: 絶対 Track(PositionX 0 → 500、Bool、ループ)が active な間に、相対クリップ(+20 一定)を `Play` すると、
      適用値は **基準値 + 20**(下のレイヤーの現在値 + 20 ではない)。相対クリップ完了後は下のレイヤーの値に戻る
- [x] **下の Hold が基準値を動かす**: 基準値 100。相対 +20(Bool、ループ)が勝者の間に、下で絶対 Track(0 → 500、Manual / Hold、serial はより古い)が完了すると、
      表示が 520 になる。相対を外すと 500。結果 120 → 520 → 500
- [x] **基準値の取り直しの条件**: (a) すべてのレイヤーが外れた後にノードを 300 へ動かして `Play` → 基準 300。
      (b) 同じプロパティに Bool のクリップが残っている間に `StopAll()` → 300 へ動かす → `Play` → 基準は古い 100 のまま(§6.4-2。仕様)。結果 (a) は上のレイアウト追従で 250 / 300、(b) は 105(= 100 + 5)
- [x] **保存 → 再読込**: 相対 Track が `"relative": true` で書かれ、再読込で `relative` が戻る。絶対 Track には `"relative"` キーが出ない(`"relative"` は 1 回だけ、再読込で 1 / 0)
- [x] **既存 JSON の回帰**: `Title.screen.json` / `InGame.screen.json` / `Result.screen.json` を読み込んで Save した結果が、P9 の前後でバイト一致する。
      §14 のプリセットの JSON も P9 の前後でバイト一致する(P8 と同じ方法)。`51d872a` の worktree と比べて 4 ファイルとも `cmp` 一致
- [x] **検証**: `Active` / `TextCharCount` に `relative` を立てた Clip が `Validate` でエラーになり、メッセージにプロパティ名が出る(`property 'Active' cannot be relative` / `'TextCharCount'`)
- [x] Mac(Metal)の Debug / Release ビルドが通り、警告が増えていない

エディタで確認する(撮影または保存 JSON):
- [x] Track 行に `Rel` が出る。`Active` / `TextCharCount` の行では Disabled で、ツールチップに理由が出る(Disabled は撮影で確認。ツールチップの文言は未撮影)
- [x] **切り替えで見た目が変わらない**: PositionX = −600 のノードで、絶対キー(−600, −400)の Track の `Rel` を ON にすると、保存 JSON のキーが (0, +200) になる。
      OFF に戻すと (−600, −400) に戻る。どちらの状態でもスクラブ 0.5 秒の画面上の位置が同じ(撮影)。Properties の値で比較し、どちらも (−500, 320)。OFF はスクラブ中に行い、スナップショットの −600 で換算された
- [x] **スクラブ**: 相対 Track をスクラブすると、ノードが「スクラブ開始前の位置 + キー値」に動き、`Reset` で元の位置へ戻る(相対 Y が 300 + 20 = 320。Reset 後・選択変更後に保存した位置が元の値)
- [x] **キー追加**: 空の相対 Track に Timeline からキーを足すと、値は 0(スクラブしていない状態で)。保存 JSON で `(0.407, 0)`
- [x] **オートキー**: PositionX = 100 のノード、相対 Track(0 秒 = 0、0.5 秒 = +20)。スクラブ 0 秒のまま Rec ON(静止値 100)→ スクラブを 0.5 秒へ(画面 120)
      → Position を +30 ドラッグ(画面 150)→ 保存 JSON の 0.5 秒のキーが **+50**(= 150 − 静止値 100)。
      Rec OFF → ON をやり直しても静止値は 100 のまま(スナップショットを保持しているため)。新しく作られる Track は従来どおり絶対値。
      **実施は値を変えて行った**: 静止値 300 の相対 Y(0 秒 = 0、0.5 秒 = +20)で Rec ON → 0.5 秒(320)→ ドラッグで 355 → キー **+55**。
      同時に記録された絶対の X は −500(絶対値のまま)。Rec OFF → ON のやり直しは未実施
- [ ] **スナップショットの規則**(§19.5 の 1〜3)。**(a)(b)(d) は確認、(c)(c') は未実施**:
      (a) スクラブ → Reset → ノードを 130 へ動かす → Rel を切り替える: 換算に 130 が使われる(古い 100 ではない)。**確認**: Other(400)で相対 (0, +100) をスクラブ → Reset → 458 へ移動 → Rel OFF → (458, 558)。
      (b) A(PositionX = 100)をスクラブ → B(PositionX = 400)を選ぶ → B で Rel を切り替える: 換算に 400 が使われる。**確認**: RelTest(−600)をスクラブ → Other(400)で Rel ON → (0, +100)。RelTest の保存位置は −600 のまま(復元された)。
      (c) スクラブ中に PositionY の相対 Track を足してスクラブを数回動かす: PositionY が累積せず「静止値 + Sample」に毎回一致し、Reset で元へ戻る。
      (c') スクラブを止めた状態(スナップショットあり)で PositionY の Track を足し、スクラブせずにすぐ `Rel` を切り替える / キーを足す: 換算に PositionY の現在値が使われる。
      (d) Rec 中に Reset を押すと Rec が OFF になる。一覧で Clip を変えても、`+ Clip` / `+ Preset` で新しい Clip に選択が移っても OFF になり、スナップショットも破棄される。**確認**: Reset と `+ Clip` で Rec が OFF、Reset ボタンが消える。一覧での変更と `+ Preset` は未実施
- [ ] Keyframe インスペクタの値ラベルが相対 Track では `X (offset)` / `Value (offset)` になる。
      Timeline のホバーツールチップ(X/Y 行・単独行)と右クリックの見出し(単独行)で、相対の値に `+` / `-` の符号が付く。X だけ相対の行では X だけに付く。
      **確認したのは** `Value (offset)` と単独行ホバーの `v=+0.000`。X/Y 行の軸別表示と右クリックの見出しは未撮影

ユーザー確認(この Mac ではできない):
- [ ] Windows / D3D11・D3D12・Vulkan の Debug ビルドが通り、警告が増えていない

---

### P10: Click 演出後の遷移(第 9 部、§20)

設計レビュー(Codex、2026-11-10): 1 回目 重大 1・中 2 → 全件反映 → 2 回目「指摘なし」。反映したもの: ClickWait の起点 op を残り op と一緒に
pending 列の先頭へ戻し、`clickWaitDone_` はその op だけが消費する / 非アクティブな階層の Click は待たない(更新されず必ずタイムアウトするため)/
タイムアウト・1 回消費・非アクティブの評価項目。不採用は無し。

実装

- `UIAnimationComponent::IsTriggerPlaying(trigger)` と `UIAnimationSystem::IsTriggerPlaying(root, trigger)` を足す(§20.3)
- `UIScreenManager` に ClickWait 状態(`clickWaiting_` / `clickElapsed_` / `clickWaitDone_`)を足し、Pop / Replace の `BeginExit` の前で待つ(§20.2)
- `UIScreen` に入力ロックを足し(`IsInputLocked()` = Exit 待機中または ClickWait 中)、`UIInputSystem` の判定を `IsExiting()` から置き換える(§20.4)
- 検証: Click(Trigger の `conditionParam` が `Click`)のクリップに `loopFrom >= 0` を許さない(§20.5)

評価

確認の方法: Mac(Metal / Debug)の Game で、タイトル画面 JSON に一時ノード(ボタン + Click クリップ)と、ルートに Exit クリップを足す。
`UIInputSystem::FireClick` に一時フック(環境変数で、callback の後に `Replace(<次画面>)` を積む)と、`UIScreenManager` に状態遷移のログ
(ClickWait 開始 / 終了、ExitStart、ExitDone と、クリックからの経過時間)を入れる。どちらもコミットしない。時刻の許容差は ±1 フレーム(60 FPS で ±17 ms)。

- [x] **待ってから Exit**: Click クリップ 0.6 秒、Exit クリップ 0.3 秒。クリックから ExitStart まで 0.6 秒、ExitDone まで 0.9 秒(ログ)。結果 0.585 / 0.884 秒(1 フレーム以内)
- [x] **Click クリップが無ければ従来どおり**: Click クリップを外すと、クリックと同じフレームで ExitStart(P2B と同じ)。ClickWait のログが出ない
- [x] **待っている間の入力**: ClickWait 中にもう一度クリックしても Click の Trigger も `Replace` も起きない(ログ)。Hover の Bool は ClickWait 開始で false になる。
      3 回クリックして記録は 2 回(ClickWait 中の 1 回が無視された)。Hover の false はコード上(`ClearState`)の確認のみ
- [x] **待っている間も画面は動く**: ClickWait 中は画面の `OnUpdate` が呼ばれ続け、ExitStart 以降は止まる(ログ)。0.584 秒の待ちで 35 回
- [x] **後から積んだ操作**: ClickWait 中にゲームコードが `Push` を積んでも、Click → Exit → 次画面 → `Push` の順に処理される(ログ)。
      `Replace` と同じ callback で `Push` を積んで確認(ExitDone の 2 ms 後に `push`)
- [x] **タイムアウト**: ループしない 2.5 秒の Click クリップ。クリックから 2.0 秒でタイムアウトの警告ログが出て、**同じフレームで** ExitStart(再び ClickWait に入らない)。結果 2.001 秒
- [x] **`clickWaitDone_` は 1 回で消える**: 上の遷移先の画面にも Click クリップ付きのボタンを置き、そこから `Replace` すると、改めて ClickWait に入る(ログ)
- [x] **非アクティブな階層は待たない**: callback でボタン自身を非アクティブにする一時フックを入れると、ClickWait に入らず同じフレームで ExitStart(ログ)
- [x] **検証**: Click のクリップにループを付けると `Validate` でエラー(メッセージにクリップ名)。読み込み時に `[UIAnim] clip discarded: clip 'ClickPop': Click clips must not loop`
- [x] **回帰**: 既存の 3 画面の表示と、タイトル → ゲームの遷移(Space。Click を使わない経路)が従来どおり(撮影とログ。ClickWait のログが出ない)。
      タイトルと Loading を撮影。InGame / Result の画面は今回撮っていない(P10 の変更はスタック操作と入力ロックだけで、描画には触れない)
- [x] Mac(Metal)の Debug / Release ビルドが通り、警告が増えていない

評価は Mac(Metal / Debug)の Game で実施(2026-11-10)。タイトル画面 JSON に一時ノード `Btn`(ボタン + Click クリップ + Exit クリップ)を足し、
`FireClick` の一時フック(環境変数で `Replace("AquaDashTitle")` / `Push("Loading")` / ボタンの非アクティブ化)と `UIScreenManager` の時刻ログで確かめた。いずれもコミットしていない。

**評価で見つけた既存の課題**(P10 では直さない。README「既知の課題」に記録): Exit グループのクリップが**非アクティブなノード**にあると、
`IsAnimationGroupPlaying` は非アクティブな階層も調べるが更新はされないので、Exit 待機が必ず 2.0 秒のタイムアウトまで続く。
P10 の `IsTriggerPlaying` で避けたのと同じ穴(callback でボタン自身を非アクティブにし、同じボタンに Exit クリップを付けた構成で観測)。

ユーザー確認(この Mac ではできない):
- [ ] Windows / D3D11・D3D12・Vulkan の Debug ビルドが通り、警告が増えていない

---

## 12. 後続改善(本書の範囲外。この順で続ける)

本書が入ってから着手する。逆順にすると全部書き直しになる。

1. ~~**プリセット**~~ → **P4 として本書に取り込んだ(§14)**
2. ~~**ベクタトラック**~~ → **P5 として本書に取り込んだ(§15)**
3. ~~**オートキー**~~ → **P7 として本書に取り込んだ(§17)**
4. ~~**Ease 拡充**~~ → **P8 として本書に取り込んだ(§18)**
5. ~~**相対値モード**~~ → **P9 として本書に取り込んだ(§19)**
6. ~~**Click 演出完了後の遷移**~~ → **P10 として本書に取り込んだ(§20)**
7. **`Pause()`** — 途中値を保持したまま止める要求が出たら

---

## 13. 決めていないこと

- Exit 待機のタイムアウトは 2.0 秒で確定(P2B、`kExitTimeoutSec`)。変える要求が出たら setter を足す
- `Save As` は P0 で作った(ポップアップでパス入力)。使われなければ後で外す
- P1 のエディタ追従で Cond / Finish は enum 名(Manual / Bool / Trigger、Hold / Restore)のまま出している。§10.3 の「Play when」「Keep / Return」への言い換えは P3
- `priority`(§6.2)を足す条件。serial で足りなくなった実例が出るまで足さない
- ~~エディタの寸法(2026-09-17 ユーザー指摘)~~ → **P6 で対応済(§16.1)**

---

# 第 4 部: プリセット

## 14. プリセットの生成規則(P4)

`+ Preset` は「今の値」を基準に Clip / Track / Keyframe を一式作る。作ったあとは普通の Clip で、手で直せる。
相対値モード(§12-5)が入るまでは、キー値は生成時点の絶対値で焼き込む。

| プリセット | Play when | Duration | finish | Loop | Track(キー) |
| --- | --- | --- | --- | --- | --- |
| `FadeIn` | Enter | 0.30 | Keep | なし | ColorA: 0 → cur(EaseOut) |
| `FadeOut` | Exit | 0.30 | Keep | なし | ColorA: cur → 0(EaseIn) |
| `PopIn` | Enter | 0.25 | Keep | なし | ScaleX / ScaleY: cur×0.8 → cur(EaseOut)、ColorA: 0 → cur(Linear) |
| `SlideIn` | Enter | 0.30 | Keep | なし | PositionX: cur − 200 → cur(EaseOut) |
| `Blink` | Focused | 0.60 | Return | あり(0 から) | ColorA: cur → cur×0.3(0.30)→ cur(0.60)(EaseInOut) |
| `Shake` | Click | 0.30 | Return | なし | PositionX: cur → cur+8(0.05)→ cur−8(0.10)→ cur+8(0.15)→ cur−8(0.20)→ cur(0.30)(Linear) |

- `cur` はそのプロパティの現在値(`UIAnimationTrack::ReadFrom`)。対象が描画コンポーネントを持たず ColorA を読めないときは 1.0 とみなす
- クリップ名はプリセット名。同名があれば `FadeIn1`、`FadeIn2` … と連番
- `Play when` の内部設定は §10.3 の表どおり(Enter / Exit = Manual + group、Focused = Bool、Click = Trigger)
- 挿入した結果が §4.5 に違反する(同じ起動単位で同じプロパティを既に使っている等)ときは、普通の編集と同じく赤字になる。挿入自体は止めない
- 数値(0.30 秒、200px、±8px、×0.8、×0.3)はプリセットの既定値で、生成後に Timeline で直す前提。パラメータ UI は作らない

---

## 15. ベクタトラック(P5)

**データは変えない。** `UIAnimationTrack` は 1 プロパティ 1 本のまま、JSON も従来どおり。まとめるのはエディタの表示と操作だけ。
ランタイム・検証・シリアライザは何も知らない(§4.5 の「同一 Clip 内で同じ property は 1 本」もそのまま効く)。

### 15.1 Row

Timeline の行は毎フレーム `clip.tracks` から組み立てる。

```cpp
struct TimelineRow
{
    const char*        label;       // "Position" / "PositionX" / "ColorA" ...
    int                trackIdx[2]; // 組なら X, Y の Track index。単独なら {idx, -1}
    bool               isVector;
};
```

| 組 | X | Y |
| --- | --- | --- |
| `Position` | `PositionX` | `PositionY` |
| `Scale` | `ScaleX` | `ScaleY` |
| `SizeDelta` | `SizeDeltaX` | `SizeDeltaY` |

- 両方の Track がクリップにあるときだけ組の行になる。片方だけなら従来の単独行(`PositionX` 等)
- Color(R / G / B / A)は今回は組にしない。要望が出たら 4 成分の組を同じ仕組みで足す
- 組の行の描画順は X 側の Track index の位置

### 15.2 キー操作

組の行のキーは「時刻」で同一視する。

| 操作 | 動作 |
| --- | --- |
| 表示 | 両 Track の時刻の和集合を 1 列に描く。両方に在る時刻は塗りつぶし、片方だけの時刻は枠だけ |
| 追加(右クリック / Ctrl+Click / `+ Key`) | 両 Track にその時刻のキーを追加。値は各 Track の `Sample(時刻)`(キーが無ければ現在値) |
| ドラッグ | その時刻を持つ両方のキーを同じ新時刻へ動かす(片方だけなら在る方だけ) |
| 削除 | その時刻を持つ両方のキーを消す |
| Keyframe インスペクタ | `Time`(両方に効く)/ `X` / `Y`(それぞれの値)/ `Ease`(両方に効く)。片方だけの時刻では無い側を Disabled |

- 選択は `selRowIdx_` + `selKeyTime_`(時刻)で持つ。単独行でも同じ仕組みで扱い、旧 `selTrackIdx_` / `selKeyframeIdx_` は Row 化に合わせて置き換える
- `+ Track` の組の項目は、既に片方がある場合は無い方だけ足す
- 勝者マーク `*` は組の行では「X か Y のどちらかがこの Clip の勝者」で出す

---

# 第 5 部: 到達点と再開手順

## 16. 到達点と再開手順(2026-09-17 時点)

### 16.1 何がどこまで終わっているか

| フェーズ | 内容 | 状態 | コミット |
| --- | --- | --- | --- |
| P0 | 編集・保存基盤(Session / texturePath / Save / Reload) | 完了(Mac 評価済) | `b88b5fe` |
| P1 | 3 階層化・ハッシュ化・検証・描画コンポーネント排他 | 完了(Mac 評価済) | `4a04c3a` |
| P2 | レイヤー評価(serial / 基準値)・自動フック(Enter / Hover / Pressed / Focused / Click) | 完了(Mac 評価済) | `733f120` |
| P2B | Exit 待機付き画面遷移(タイムアウト 2.0 秒) | 完了(Mac 評価済) | `0412284` |
| P3 | 統合エディタ(Inspector タブ / Timeline / 検証赤字 / Save 停止 / TextStyle 入口) | 完了(Mac 評価済) | `100161b` |
| P4 | プリセット(§14) | 完了(Mac 評価済) | `3d283eb` |
| P5 | ベクタトラック(§15) | 完了(Mac 評価済) | `367b967` |
| P6 | Windows 回帰 + エディタ寸法(§16.5) | 完了(Windows / D3D11 で評価済) | `3ff20bf` |
| P7 | オートキー(§17) | 完了(Windows / D3D11 で評価済) | `7bf1bc7` + 追従 |
| P8 | Ease 拡充(§18) | 完了(Mac / Metal で評価済。Windows のビルド確認と一部のエディタ確認が残る) | `b8541b8` |
| P9 | 相対値(§19) | 完了(Mac / Metal で評価済。Windows のビルド確認と一部のエディタ確認が残る) | `9a9350d` |
| P10 | Click 演出後の遷移(§20) | 完了(Mac / Metal で評価済。Windows のビルド確認が残る) | 本コミット |

**P0〜P5 で共通に残っていた「Windows / D3D11 でビルドが通り、警告が増えていない」は P6 で消化した。**
`Game/DirectX.vcxproj` の Debug / x64 を `AqGraphicsApi=D3D11` / `D3D12` / `Vulkan` の 3 通りビルドして全部成功。
新規の警告は `UIEditorDebugPanel.h` の C4099(`UITransformComponent` を `struct` で前方宣言していたが実体は `class`)1 件だけで、
前方宣言を `class` に直して消した。残りの C4244(Bullet)・C4267(`RenderThread.cpp` 経由の `<memory>`)は P0 以前からある既存分。

**Windows での目視も 2026-09-17 に実施した**(D3D11 / Debug、1280x720)。UI Editor・Animation タブ・Timeline・
TextStyle ポップアップがいずれも出ることを確認済み。段取りは §16.6。

残っているもの:

- 1920x1080 以外のウィンドウで UIButton の hover が当たること(P2 で `UIInputSystem::HitTest` の `clientSize` 更新を直した)。
  UI Editor は出したが、ボタンの hover 自体はまだ触っていない
- UWP(DebugXbox)と Release はこの PC ではビルド不可(ツールセット未導入 / Bullet の Release lib 無し)。別環境で見る
- **ゲーム窓のリサイズに ImGui が追従しない**(§16.6)。UI Editor とは別件だが、目視のたびに引っかかる

### 16.2 残っている作業(この順で)

1. ~~**P8: Ease 拡充(§18)**~~ — 2026-11-10 に完了。残りは Windows 3 構成のビルド確認、右クリックの Disabled の撮影、全グループの先頭・末尾の選択確認
2. **§12 の続き** — ~~相対値~~(P9)→ ~~Click 演出後の遷移~~(P10)→ Pause。各々を §11 に「Pn: 計画 + 評価チェックリスト」と
   新セクション(§14 / §15 と同じ形)として書いてから実装する
3. **ゲーム側の実例** — `Play(group)` / `Stop()` を呼ぶコードと、クリックで遷移するボタン(§9.3 の実測)はまだ無い

### 16.3 再開手順

1. 本書 §11 で次のフェーズを決め、「Pn 着手時に確定した補足」を書く(P1〜P5 が例。判断が要る点を先に潰しておくと実装の差し戻しが減る)
2. 実装は設計書のパスと「設計書どおりに実装せよ。設計変更は行わず食い違いは報告せよ」を付けて委譲する。触ってよいファイルと
   触ってはいけないファイルを明示し、共有ファイルは 1 人に集約する。ビルドは委譲先で行わず本体でまとめて行う
3. 差分を本体でレビューし、規約逸脱・設計との食い違いを直す
4. ビルド(Mac: `source ~/.local/aq-mac-env.sh` のあと `cmake --build --preset macos-ninja-metal-debug` と `-release`。
   エディタは `AQ_DEBUG_IMGUI` が Debug 構成にしか付かないので **Debug で確認する**)
5. 評価は §11 のチェックリストを 1 つずつ。埋まらない項目は理由を書いて残す
6. 設計書のチェックを埋め、「Pn の実装で決めたこと」を書き、`対象コミット` を更新してコミット(`<Engine>` タグ、1 フェーズ 1 コミット)

### 16.4 Mac での評価の段取り(P0〜P5 で使った方法)

- **起動**: `cd DirectX/Game && nohup ../build/macos-ninja-metal/bin/Debug/Game.app/Contents/MacOS/Game &`(cwd が Game でないとアセットが出ない)。
  Debug は起動に約 45 秒。ウィンドウは別 Space にいることがあるので `CGWindowListOptionAll` で探し、`screencapture -x -o -l<id>` で撮る
- **テストデータ**: `Game/Assets/UI/AquaDash/Title.screen.json` に一時的にノードを足す(例: 位置 (-600, 300) / 200x100 の `white.png` + `button` を持つ `Btn`
  と、確認したい `animation`)。確認後は `git checkout` で戻す。起動中のアプリは `Game/imgui.ini`(tracked)も書き換えるので同様に戻す
- **UI Editor を開く**: デバッグメニュー `UI > UI Editor`。左上の FPS オーバーレイがメニューを覆ってクリックを吸うことがあるので、
  右端の `Profiling` をクリックしてから左へホバーで移る
- **合成入力の注意**: ImGui は「移動 → 同フレームでクリック」を拾わないことがある。近く → 対象へ 2 段階で移動し、0.3 秒待ってからクリック。
  Combo の項目位置は開いた直後の撮影で決める。DragFloat への数値入力は Mac の自前 ImGui バックエンドでは文字が入らないので、
  値は JSON 側で用意するか横ドラッグで動かす
- **計測**: 撮影画像の領域平均 RGBA を読む(2x なので座標は原寸)。「白 × alpha + 背景 × (1 − alpha)」で期待値を手計算する。
  Enter の再生を見直すときは UI Editor の `Reload`、Exit は Space でのタイトル→ローディング遷移。0.3 秒級の演出は撮影の遅延(約 0.2 秒)を超えるので
  Duration を伸ばすか連続撮影する
- **エディタ操作の結果は保存 JSON で確認する**のが確実(キー追加・ドラッグ・プリセットは Save 後の JSON を見る)
- Reload 後はゲーム側の一度きりの setter(`SetStageThumbnail` 等)が再実行されない(§3.3)。サムネイルが消えるのは仕様

### 16.5 P6: Windows 回帰 + エディタ寸法

§13 に「後で直す」と積んであったエディタの寸法と、Play when の Group 欄の出し方を直した。新しい設計判断は無く、
§10.3 に書いてある挙動へ実装を寄せただけなので、独立した設計セクションは作らない。

| 直したもの | 変更 | 場所 |
| --- | --- | --- |
| UI Editor の既定サイズ | 700x560 → **1000x800**(`ImGuiCond_FirstUseEver` なので既存の `imgui.ini` があるとそちらが勝つ) | `UIEditorDebugPanel.cpp` |
| Timeline の高さ | `max(260px, 45%)` の固定比率 → **内容から算出**。`UIAnimationEditor::ComputeTimelineHeight()` が Track 本数と表示 Row 数から必要高さを出し、呼び出し側が残り高さの 60% で頭打ちにする | `UIAnimationEditor.cpp` / `UIEditorDebugPanel.cpp` |
| TextStyle ポップアップの幅 | `ImGuiWindowFlags_AlwaysAutoResize` を外した(このフラグが付いていると `SetNextWindowSize` が無視され、480x720 の指定が効いていなかった)。初回 **720x760**、以後は手で広げられる | `TextStyleEditorPanel.cpp` |
| Group 欄の出し方 | `clip.condition == Manual` で判定していたため Enter / Exit(内部は Manual + 予約 group)でも出ていた。`ComputePlayWhenIndex(clip) == PlayWhen_Manual` に変更し、§10.3 どおり Manual のときだけ出す | `UIAnimationEditor.cpp` |
| C4099 | `UIEditorDebugPanel.h` の `RenderAnchorPicker(struct UITransformComponent*)` を、`class` の前方宣言 + `UITransformComponent*` に直した | `UIEditorDebugPanel.h` |

**ComputeTimelineHeight() の内訳**(ImGui のスタイル値に依存するのでフレーム中に呼ぶ):

```
"Tracks" 行 + Track 本数 × FrameHeightWithSpacing + Separator + 操作ヒント 1 行
  + RULER_H + 表示 Row 数 × ROW_H + 10
  + WindowPadding.y × 4 + ItemSpacing.y × 2 + BOTTOM_PANE_H(120)
→ 240〜560px にクランプ
```

Clip 未選択のときは右ペインが "Select a clip" だけなので可変分は 0 になり、下限の 240px が出る。

**評価チェックリスト**

- [x] Windows / D3D11・D3D12・Vulkan の Debug ビルドが通る
- [x] 新規の警告が 0(C4099 を潰した時点で、残るのは P0 以前からある既存分だけ)
- [x] UI Editor を初めて開いたウィンドウが 1000x800(Windows / D3D11 で実測 995x800)。
      **ただし 1280x720 のゲーム窓には縦が入らない**。この窓で使うなら 1000x690 程度まで縮める必要がある
- [x] Track を増やすと Timeline が伸び、Clip 未選択では最小(240px)に縮む
      (Clip 未選択で約 245px、Track 1 本で約 300px、6 本で頭打ち)
- [x] TextStyle ポップアップでラベルの長い項目(`Font (atlas.json)`)が切れず、手で広げられる
      (グリップを引いて 477 → 637px。`AlwaysAutoResize` があった頃はリサイズできなかった)
- [x] Play when を Enter / Exit にしたとき Group 欄が消え、Manual に戻すと出る

> 既定サイズは `ImGuiCond_FirstUseEver` なので、`Game/imgui.ini` にそのウィンドウのサイズが残っていると効かない。
> UI Editor は `###uieditor` を付けた P0 以降キーが `[Window][UI Editor###uieditor]` に変わったため古い `[Window][UI Editor]` は当たらないが、
> TextStyle ポップアップは `[Window][TextStyle Editor]` がそのまま残っており、**720x760 ではなく ini の値(480x620)で開く**。
> 既定サイズを確かめるときは ini の該当ブロックを消してから開く(`imgui.ini` は tracked なので確認後に戻す)。

---

# 第 6 部: オートキー

## 17. オートキー(P7)

### 17.1 やること

**録画(Rec)中に Properties タブの値をいじると、その場でキーが入る。**
今はキーを 1 本入れるのに「Row を選ぶ → `+ Key` → Keyframe インスペクタで値を打ち直す」の 3 手が要る。
DragFloat への数値入力はプラットフォームによっては通らない(§16.4)ので、値は実質ドラッグでしか入れられない。
録画中は「見た目を合わせる操作」がそのままキーになる。

### 17.2 どこで操作するか

`Rec` トグルは Timeline 下部の Preview Controls、スクラブの並び。値をいじるのは Inspector の **Properties タブ**。
この 2 つは通常は同時に見えない(Timeline は Animation タブのときだけ出る)ため、
**録画中は Inspector のタブに関わらず Timeline を出す**。打ったキーがその場で行に出ないと、録画の意味がない。

```
┌ Inspector ───────────────┐
│ [Properties] [Animation]  │
│ ● REC  Enter  @ 0.35s     │ ← 録画中だけ Properties タブの先頭に赤帯
│ Transform                 │
│  Position [-500][ 250][0] │ ← ここをドラッグするとキーが入る
├───────────────────────────┤
│ Timeline(録画中は常時)   │
│  Position ◇────◇          │
└───────────────────────────┘
```

### 17.3 録画対象のプロパティ

| Properties タブのウィジェット | 打つプロパティ |
| --- | --- |
| Transform / Position | `PositionX` `PositionY`(`PositionZ` は既に Track があるときだけ) |
| Transform / Size | `SizeDeltaX` `SizeDeltaY` |
| Transform / Rotation | `Rotation` |
| Transform / Scale | `ScaleX` `ScaleY` |
| Transform / Active | `Active` |
| Image / NineSlice / CircleGauge の Color | `ColorR` `ColorG` `ColorB` `ColorA` |
| 同 Fill Amount | `FillAmount` |
| NineSlice の Border Left / Right / Top / Bottom | `NineSliceBorderLeft` ほか 3 つ |

- 表に無いウィジェット(Pivot / Anchor / UV Rect / Flip / texturePath / Text の各項目 / Canvas)は
  対応する `UIAnimatedProperty` が無い。従来どおり `dirty` を立てるだけで、キーは入らない
- `PositionZ` は深度ソート用なので**自動では Track を作らない**。既に Track があるときだけ打つ
- `TextCharCount` は Properties タブに編集欄が無いので対象外(タイプライター演出は Timeline から手で打つ)

### 17.4 キーの入り方

原則 4「挙動は決定的」に従い、後勝ちや暗黙の上書きを作らない。

1. **時刻**はスクラブ時刻 `scrubTime_`。実再生(`Play`)の時刻は使わない
2. **値**は編集後の現在値(`UIAnimationTrack::ReadFrom(obj)`)。
   `+ Key` が使う `Sample(時刻)` とは違う — `+ Key` は「曲線を変えずにキーを挿す」ためのもので、録画はその逆
3. **同時刻(許容差 `1e-5`)にキーがあれば値だけ上書きし、`ease` は保つ。**
   ドラッグ中は毎フレーム同じ時刻へ書くので、1 回のドラッグで増えるキーは 1 本
4. 対象 Track が Clip に無ければ**作る**。作るのは §17.3 の表にあるプロパティだけ
5. **新規 Track で、かつスクラブ時刻 > 0 のときは、0 秒にも「録画開始時の値」でキーを置く。**
   これが無いと Track がキー 1 本だけになり、「0 秒から動く」ではなく「常にその値」になってしまう。
   既に Track があるプロパティには 0 秒キーを足さない(既存の曲線を勝手に変えない)
6. §4.5 の検証(同じ起動単位でのプロパティ重複)は変えない。引っかかれば従来どおり赤字が出る

### 17.5 録画の開始と終了

- `Rec` は Timeline 下部の Preview Controls に置く。**この領域は Clip 選択中しか描かれない**ので、
  Clip が無いときは Rec そのものが出ない(Disabled 分岐は要らない)
- ON の瞬間に**録画基準値**を取る。スクラブ用の `snapshot_`(Clip の Track ぶんだけ)とは別に、
  §17.3 の全プロパティの現在値を `recBaseline_` に持つ。17.4-5 の 0 秒キーはここから取る
- 同時に既存の `TakeSnapshot()` も取り、`Reset` で録画開始前へ戻せるようにする
- **ON の瞬間にスクラブ再生を止める**(`isPlaying_ = false`)。再生したままだとキーを打つ時刻が動く
- **OFF にしても値は戻さない。**戻したいときは既存の `Reset` ボタン
- 選択オブジェクトの変更(`OnTargetChanged()`)と Reload(`Reset()`)で録画は落ちる

### 17.6 責務

| ファイル | 追加・変更 |
| --- | --- |
| `UI/Debug/UIAnimationEditor.h` | `isRecording_` / `recBaseline_` を追加。公開 API に `IsRecording()` と `RecordEdit()` |
| `UI/Debug/UIAnimationEditor.cpp` | Preview Controls に `Rec` トグル。`RecordEdit()` の実装。無名 namespace に「同時刻上書き」の `SetKeyAtTime()` |
| `UI/Debug/UIEditorDebugPanel.h` | `MarkEdited()` の宣言 |
| `UI/Debug/UIEditorDebugPanel.cpp` | §17.3 のウィジェットの `MarkDirtyIfEdited()` を `MarkEdited(obj, { ... })` へ差し替え。録画中は Timeline を常時表示。Properties タブ先頭の REC 帯 |

公開 API は 3 本。

```cpp
// 録画中か (UI Editor が Timeline を出すかの判定にも使う)
bool IsRecording() const;

// 録画中なら props のキーをスクラブ時刻へ入れる。録画していなければ何もしない
void RecordEdit(UIObject* obj, std::initializer_list<UIAnimatedProperty> props);

// Properties タブ先頭の赤帯の文字列 (Clip 名 + スクラブ時刻)。録画していなければ空
std::string GetRecordingLabel(const UIObject* obj) const;
```

`UIAnimationClip` / `UIAnimationTrack` / JSON・ローダ・シリアライザ・ランタイムには触らない。
**エディタの中だけで閉じる変更**であり、データ形式の互換は保たれる。

### 17.7 やらないこと

- **実再生(`Play`)の時刻に対する録画**。キーを打つ時刻は常にスクラブ時刻で、実再生の時刻は使わない。
  実再生は時刻がランタイム側にあり、どのフレームの値を採るかが決まらないため。
  実再生中に Properties をいじること自体は止めない(止めるにはランタイムの再生状態を見る必要があり、エディタの外へ出る)
- **Timeline からの録画**(Keyframe インスペクタの値編集)。あれは既にキーを直接いじる操作なので対象外
- **新規 Clip の自動生成**。Clip は先に作っておく

---

### 16.6 Windows での評価の段取り(P6 / P7 で使った方法)

Mac 側は §16.4。Windows は PowerShell から画面キャプチャと合成入力で操作した。引っかかった点だけ残す。

- **起動**: `Start-Process Game.exe -WorkingDirectory DirectX\Game`(cwd が `Game` でないとアセットが出ない)。
  終了は `CloseMainWindow()`。ビルドと構成は [project-windows-build-env] の memory が正本
- **キャプチャ**: `GetClientRect` + `System.Drawing` の `CopyFromScreen`。撮った画像の座標が
  そのままクライアント座標になるので、クリック位置は画像から直接読める
- **入力**: `SetCursorPos` + `mouse_event`。ImGui は「移動 + 同フレームのクリック」を拾わないことがあるので、
  近く → 対象の 2 段階で移動して 0.25 秒待ってから押す(Mac と同じ)
- **ゲーム窓をリサイズしても ImGui は追従しない。** バックバッファだけ広がって ImGui の `DisplaySize` は
  元のままなので、描画が引き伸ばされるだけで入力座標もずれる。エディタウィンドウを大きく見たいときは
  **`imgui.ini` に `[Window][UI Editor###uieditor]` の `Pos` / `Size` を書いてから起動する**
- **PowerShell 5.1 は BOM 無し UTF-8 の `.ps1` を ANSI として読む。** 日本語コメントを書くならヘルパスクリプトは
  **BOM 付き UTF-8** で保存する(壊れた文字が改行を飲み込んでパースエラーになる)
- **PowerShell の関数引数は型を書く。** `function DragX($x, $y, $dx)` だと `$dx` が文字列のままで、
  `$dx * $i` が文字列の繰り返しになりドラッグ量が壊れる。`[int]$dx` と書く
- **Preview Controls はボタン列の幅が再生中と停止中で変わる。** 停止中は `|< > >|` の 3 個、再生中は `||` の 1 個なので、
  その右にある `+ Key` / `Rec` / `Reset` の x 座標が 50px 以上ずれる。座標は毎回撮り直して決める
- **テストノードの `animation` は `components` の中ではなくノード直下**(`name` / `components` / `animation` / `children` が同列)。
  `components` の中に書くと黙って無視され、Animation タブが「This object has no UIAnimationComponent.」になる
- **エディタ操作の結果は保存 JSON で確認する**のが確実(Mac と同じ)。`Save` を押してから
  `Assets/UI/AquaDash/Title.screen.json` の該当ノードを読む
- `Title.screen.json` と `imgui.ini` は tracked。確認前にコピーを取り、終わったら戻す

---

# 第 7 部: Ease 拡充

## 18. Ease 拡充(P8)

### 18.1 足すイージング

今は `Linear` / `EaseIn` / `EaseOut` / `EaseInOut` / `Bezier` の 5 種しかなく、
「行き過ぎて戻る」「弾む」といった UI でよく使う動きが作れない。`Back` / `Elastic` / `Bounce` を
In / Out / InOut の 3 方向ぶん足して 14 種にする。

| グループ | 種別 | 用途 |
| --- | --- | --- |
| — | `Linear` | 等速 |
| 基本 | `EaseIn` / `EaseOut` / `EaseInOut` / `Smoothstep` | 加速・減速 |
| Back | `BackIn` / `BackOut` / `BackInOut` | 行き過ぎて戻る。ポップイン向け |
| Elastic | `ElasticIn` / `ElasticOut` / `ElasticInOut` | ばね。強調表示向け |
| Bounce | `BounceIn` / `BounceOut` / `BounceInOut` | 弾む。落下の着地向け |

`EaseType` は `uint8_t` の平らな enum のまま増やす。JSON は名前で読み書きするので、
**既存ファイルは触らずに読める**(知らない名前は `Linear` にフォールバックする既存の挙動もそのまま)。

「曲線 × 方向」の 2 フィールドに分ける案は採らない。JSON の `ease` が 1 文字列で済む形を壊すのと、
`Linear` / `Smoothstep` が方向を持たず表に穴が空くため。選択肢の多さは §18.4 の区切りで吸収する。

**式と端点**。入力 `t` は `Sample()` が渡す [0, 1](区間の端では `t = 0` / `1` が来る。中間キーの時刻では前の区間を `t = 1` で評価する)。
どの種別も **`E(0) = 0`、`E(1) = 1`** を満たすこと(満たさないと中間キーの時刻でキー値と食い違い、値が飛ぶ)。式は一般的な easings 系の定義に合わせる:

| 種別 | 式(`t` ∈ [0, 1]) |
| --- | --- |
| 既存 5 種 | 現行のまま(`EaseIn = t²`、`EaseOut = t(2 − t)`、`EaseInOut` は 2 次の区分、`Smoothstep = t²(3 − 2t)`) |
| `BackIn` / `BackOut` | `c1 = 1.70158`、`c3 = c1 + 1`。In = `c3·t³ − c1·t²`、Out = `1 + c3·(t−1)³ + c1·(t−1)²` |
| `BackInOut` | `c2 = c1 × 1.525`。`t < 0.5`: `(2t)²·((c2+1)·2t − c2) / 2`、それ以外: `((2t−2)²·((c2+1)·(2t−2) + c2) + 2) / 2` |
| `ElasticIn` / `ElasticOut` | `c4 = 2π/3`。**`t == 0` → 0、`t == 1` → 1 を先に分岐**(式そのものは端点で厳密に 0 / 1 にならない)。In = `−2^(10t−10)·sin((10t − 10.75)·c4)`、Out = `2^(−10t)·sin((10t − 0.75)·c4) + 1` |
| `ElasticInOut` | `c5 = 2π/4.5`。端点の分岐は同じ。`t < 0.5`: `−2^(20t−10)·sin((20t − 11.125)·c5) / 2`、それ以外: `2^(−20t+10)·sin((20t − 11.125)·c5) / 2 + 1` |
| `BounceOut` | `n1 = 7.5625`、`d1 = 2.75` の 4 区分の放物線(`t < 1/d1`、`< 2/d1`、`< 2.5/d1`、それ以外) |
| `BounceIn` / `BounceInOut` | In = `1 − BounceOut(1 − t)`、InOut = `t < 0.5` で `(1 − BounceOut(1 − 2t)) / 2`、それ以外 `(1 + BounceOut(2t − 1)) / 2` |

参照値(評価で使う。許容差 1e-4):

| 種別 | 0 | 0.25 | 0.5 | 0.75 | 1 |
| --- | --- | --- | --- | --- | --- |
| `Linear` | 0 | 0.2500 | 0.5000 | 0.7500 | 1 |
| `EaseIn` | 0 | 0.0625 | 0.2500 | 0.5625 | 1 |
| `EaseOut` | 0 | 0.4375 | 0.7500 | 0.9375 | 1 |
| `EaseInOut` | 0 | 0.1250 | 0.5000 | 0.8750 | 1 |
| `Smoothstep` | 0 | 0.1563 | 0.5000 | 0.8438 | 1 |
| `BackIn` | 0 | -0.0641 | -0.0877 | 0.1826 | 1 |
| `BackOut` | 0 | 0.8174 | 1.0877 | 1.0641 | 1 |
| `BackInOut` | 0 | -0.0997 | 0.5000 | 1.0997 | 1 |
| `ElasticIn` | 0 | -0.0055 | -0.0156 | 0.0884 | 1 |
| `ElasticOut` | 0 | 0.9116 | 1.0156 | 1.0055 | 1 |
| `ElasticInOut` | 0 | 0.0120 | 0.5000 | 0.9880 | 1 |
| `BounceIn` | 0 | 0.0273 | 0.2344 | 0.5273 | 1 |
| `BounceOut` | 0 | 0.4727 | 0.7656 | 0.9727 | 1 |
| `BounceInOut` | 0 | 0.1172 | 0.5000 | 0.8828 | 1 |

(`Smoothstep` の 0.25 / 0.75 は 0.15625 / 0.84375。`BackOut` の最大値は約 1.100。値は 2026-11-10 に上の式から計算した。)

**`Back` / `Elastic` は [0, 1] の外へ出る。** 値が範囲外になると困るプロパティ(`ColorA` / `FillAmount` /
`Active`)でも `Sample()` はクランプしない。これは既存の線形補間でも同じ(キー値が範囲外なら範囲外が出る)なので、
挙動を変えずにそろえる。行き過ぎさせたくないプロパティでは選ばない、という運用にする。

### 18.2 `Bezier` を `Smoothstep` へ改名する

`Bezier` の実体は `t * t * (3 - 2t)` = smoothstep で、制御点を持たない。
名前が「設定できそう」に見えて実際には設定できないので、原則 2 に反する。
**曲線は変えず、名前だけ実体に合わせる。**

- `EaseToStr()` は `"Smoothstep"` を書く
- `StrToEase()` は `"Smoothstep"` と `"Bezier"`(旧名)の両方を受ける
- 制御点付きの本物の三次ベジェは P8 では作らない。キーごとに 2 つの制御点と曲線編集 UI が要り、
  §12 の「相対値」より大きい。要求が出たら別フェーズにする

### 18.3 曲線プレビュー

Keyframe インスペクタの右に、選択キーの Ease を [0, 1] で描く。`ImDrawList` で折れ線を引くだけ。

```
┌ Keyframe ────────────────────────────────┐
│ Time  [0.200]        ┌──────────────┐    │
│ X     [-500.0]       │        ___---│    │  ← 曲線。枠が [0,1] の箱
│ Y     [ 250.0]       │     _-'      │    │     Back / Elastic は箱の外へ出る
│ Ease (to next)       │ _-'          │    │
│  [BackOut      ▼]    └──────────────┘    │
│                       0.20s  →  0.50s    │  ← この Ease が効く区間
└──────────────────────────────────────────┘
```

- 曲線は `EvaluateEase()` をそのまま呼んで描く。エディタ側に式を書き写さない
  (書き写すと実挙動とプレビューがずれる)。そのため `ApplyEasing()` を
  `UIAnimationTrack.h` の `EvaluateEase()` として公開する
- 縦のレンジは [-0.4, 1.4] を確保し、[0, 1] の箱を枠線で描く。`Back` / `Elastic` の
  行き過ぎが箱からはみ出して見えることが、このプレビューの一番の価値
- 曲線の下に **区間の始点と終点の時刻**を出す。次のキーが無ければ `(last key)` と出す
- 描画領域は 150x90 程度。これを入れるため `##kfinspector` の幅を 300 → 420、
  `BOTTOM_PANE_H` を 120 → 150 にする(`ComputeTimelineHeight()` の固定分も連動する)。**実装では 185 にした**(§11 P8 の「実装で決めたこと」)

### 18.4 「左キーが右への区間を支配する」を出す

`UIKeyframe::ease` は**そのキーから次のキーまでの区間**に効く。
選んだキー自身の入り方だと誤解されやすいので、UI で明示する。

- ラベルを `Ease` から **`Ease (to next)`** にする
- **選択キーがその Track の最後のキーのときは Combo を Disabled** にし、
  ツールチップに「最後のキーなので効く区間が無い」と出す(原則 2)
- プレビュー下の区間表示(§18.3)で始点・終点の時刻を見せる
- Timeline の右クリックメニューの `Ease` も同じ 14 種・同じグループ分けにし、**最後のキーでは同じ理由で Disabled** にする
  (今は 5 種のリテラル配列が 2 か所にある。`UIAnimationEditor.cpp` の `EASE_MENU_LABELS` と `EASE_LABELS`)

**表は `EaseType` を明示して持つ**。現状の Combo / メニューは **enum の数値をそのまま配列の添字に使っている**
(`static_cast<int>(curEase)` と `static_cast<EaseType>(ei)`)。グループ分けで並べ替えると別の Ease が入るので、次の形にする:

```cpp
struct EaseEntry { EaseType type; uint8_t group; };      // 表示名は UIAnimationSerializer::EaseToStr(type) から取る
static constexpr EaseEntry EASE_TABLE[] = { { EaseType::Linear, 0 }, { EaseType::EaseIn, 1 }, ... };   // 14 件
```

- Combo とメニューは表を走査し、**現在値は `entry.type == curEase` で判定し、選択時は `entry.type` を書く**。添字と enum の数値を結びつけない
- 表示名は JSON 名と同じ(`EaseToStr`)。名前の表を 2 つ持たない
- `static_assert(std::size(EASE_TABLE) == 14)` で件数を固定する。enum の数値は JSON に出ない(名前で保存)ので並び順は自由だが、追加は末尾にする

**X/Y を 1 行にまとめた行(§15.1)の規則**。X と Y はキー時刻も Ease も別々に持てる(例: X が `{0, 1}`、Y が `{0, 2}`)。
選択時刻にキーを持つ軸を「対象軸」とし(`xi >= 0` / `yi >= 0`。既存の判定)、次のように決める:

- **書き込み**: 従来どおり対象軸すべてに同じ Ease を書く
- **表示する Ease**: 対象軸の Ease が同じならその名前。違えば Combo のプレビュー文字列を `(mixed)` にし、曲線は X(X に無ければ Y)で描いて `X/Y differ` と添える
- **区間表示**: 対象軸ごとに次のキー時刻を求める。同じなら `0.20s → 0.50s` の 1 行、違えば `X: 0.00s → 1.00s` / `Y: 0.00s → 2.00s` の軸別 2 行。次のキーが無い軸は `(last key)`
- **Disabled**: **対象軸のすべてで最後のキー**のときだけ。一部の軸だけ最後なら有効のままにし、その軸の区間表示が `(last key)` になる(書いても効かないが害は無い)

選択肢は `BeginCombo` + `Selectable` で描き、グループの間に `ImGui::Separator()` を入れる。

### 18.5 責務

| ファイル | 追加・変更 |
| --- | --- |
| `UI/UITypes.h` | `EaseType` に 9 種追加、`Bezier` → `Smoothstep` へ改名 |
| `UI/Animation/UIAnimationTrack.h` | `EvaluateEase(float t, EaseType ease)` を公開宣言 |
| `UI/Animation/UIAnimationTrack.cpp` | `ApplyEasing()` を `EvaluateEase()` にし、9 種の式を足す |
| `UI/Animation/UIAnimationSerializer.cpp` | `EaseToStr()` / `StrToEase()` に 9 種 + `"Bezier"` の旧名受け |
| `UI/Debug/UIAnimationEditor.h` | 曲線プレビューの private ヘルパ宣言 |
| `UI/Debug/UIAnimationEditor.cpp` | Ease の表を 1 か所に集約、Combo のグループ分け、最後のキーの Disabled、曲線プレビュー、`##kfinspector` と `BOTTOM_PANE_H` の寸法 |

ランタイムの評価順・レイヤー・JSON の構造は変えない。**キー 1 本あたりのデータも増えない。**

### 18.6 やらないこと

- **制御点付きの三次ベジェ**(§18.2)
- **Timeline の行に曲線を描くこと**。行は 22px しかなく、キーの菱形と重なる
- **プロパティごとの値クランプ**(§18.1)。`Back` / `Elastic` の行き過ぎは仕様として残す
- **既存プリセット(§14)の Ease 差し替え**。`PopIn` を `BackOut` にすると見栄えは良くなるが、
  P8 は「選べるようにする」までにして、プリセットの作り直しは別に判断する

---

# 第 8 部: 相対値

## 19. 相対値(P9)

### 19.1 やること

今のキー値は絶対値で、ノードの配置を変えるとアニメの位置がずれる。例えば SlideIn を (−600 → −400) で作ったあと、
レイアウト調整でノードを −500 へ動かすと、アニメは −600 から始まり −400 で止まり、配置した位置へ来ない。
Track ごとに「キー値は**基準値からの差分**」と指定できるようにし、配置を変えてもアニメが付いてくるようにする。

**基準値は §6.4 の基準値をそのまま使う**。そのプロパティにレイヤーが 1 本も載っていないときの UIObject の値で、
「アニメが無いときの姿」= レイアウトで置いた値である。新しい基準を作らない。

**レイヤー同士の加算合成はしない**。相対クリップが絶対クリップの上に載ったとき、相対の値は
「基準値 + 差分」であって「下のレイヤーの現在値 + 差分」ではない。§6 の「プロパティごとに勝者 1 本」を変えない。
下のレイヤーへの加算(揺れを重ねる等)が要るなら別フェーズで「加算レイヤー」として設計する。

### 19.2 データと JSON

| 対象 | 変更 |
| --- | --- |
| `UIAnimationTrack` | `bool relative = false` を足す |
| JSON の Track | `"relative": true`。**false のときは書かない**(同じ入力を P9 の前と後で Save した結果がバイト一致する。入力ファイル自体との一致ではない: Save は旧名 `Bezier` の書き換えや時刻の補正を従来どおり行う) |

```json
{ "property": "PositionX", "relative": true,
  "keyframes": [ { "time": 0, "value": -100, "ease": "EaseOut" }, { "time": 0.4, "value": 0, "ease": "Linear" } ] }
```

「乗算(Scale を基準値 × 0.8 から等)」は入れない。Scale / Color の差分でも用は足りるので、
要求が出たら `relative` を enum に広げる(JSON は `"relative": true` を `"mode": "Offset"` 相当として読み続ける)。

### 19.3 ランタイム

`UIAnimationComponent::ApplyLayers()` の手順 4 で、勝者の Track が相対なら `基準値 + Sample(t)` を書く。
手順 2 で勝者が付いた時点の基準値は必ずあるので、追加の取得は要らない。

**`CommitHold()`**(Hold で完了したときの基準値の確定。§6.4-3)も同じ式にする: 相対 Track は `基準値 + Sample(duration)` を新しい基準値にする。
基準値が無いときは先に UIObject の現在値を取ってから足す。ただしこれは**防御のためのコード**で、今の経路では通らない:
`Play()` / `Trigger()` は起動直後に `ApplyLayers()` を呼んで基準値を取り、`Update` 内で起動する Bool は `CommitHold()` を呼ばない。評価項目にはしない。

**Hold の確定は、上に載っている相対レイヤーにも効く**。`CommitHold()` は勝者かどうかに関係なく基準値を書き換えるので、
基準値 100 で相対 +20 が勝者のとき、下の絶対 Track が 500 で Hold 完了すると、表示は 120 → 520 へ移る。
「相対 = 基準値からの差分」の定義どおりの結果なので、これを仕様とする。同じフレームで複数の Hold が完了したときは、
既存どおり `clips_` の並び順に確定する(相対は、その時点の基準値に足す)。

**Hold の累積は仕様とする**。最後のキーが 0 でない相対 Track を Hold で終えると、基準値が「元 + 最終キー」に確定し、
次の `Play` はそこから始まる(+100 のクリップは再生のたびに 100 ずつ進む)。「その場から相対移動する」演出として正しい挙動なので止めない。
元の位置に戻したいなら Restore にするか、最後のキーを 0 にする。エディタは、相対 Track の最後のキーが 0 でなく Clip が Hold のとき、
Track 行に注意書き(`Hold: moves by the last key each play`)を出す。

**基準値の取り直し**は §6.4-6 のまま(レイヤーが 0 本になったら破棄し、次の起動でその時点の値を取る)。
ゲームコードが `Play` の前にノードを動かせば、それが新しい基準になる。これがレイアウト追従の正体である。
**ただし取り直すのは、そのプロパティのレイヤーが 0 本になった後だけ**。Bool / Trigger のクリップが同じプロパティに残っている間は、
外から値を変えても次の適用で上書きされ、基準値は古いままである(§6.4-2)。`StopAll()` は Manual しか止めないので、
Bool が残る構成では `StopAll()` → 移動 → `Play()` でも基準値は変わらない。`RemoveClip()` / `ReplaceAllClips()` は全 runtime を作り直すので、
次の `ApplyLayers()` で基準値は書き戻されて破棄される。

### 19.4 検証

`UIAnimationSerializer::ValidateDetailed()` に 1 規則足す:

- `Active`(0 / 1 の論理値)と `TextCharCount`(−1 = 全表示の特別値)の Track に `relative` が立っていたらエラー。
  メッセージは `clip '<name>': property '<prop>' cannot be relative`

### 19.5 エディタ

**`Rel` トグル**(Track 一覧の各行。プロパティの Combo と `-` ボタンの間)。

- 切り替えたとき、**キー値を換算して見た目を保つ**。基準 `r` = その時点の「静止値」(下記)。絶対 → 相対は各キーから `r` を引き、相対 → 絶対は `r` を足す
- `Active` / `TextCharCount` の行は Disabled、ツールチップに理由
- 切り替えは Undo を持たない既存の編集と同じ扱い(dirty を立てるだけ)

**静止値**(エディタでの基準値)。スクラブ中は obj にアニメの値が書かれているので、`ReadFrom(obj)` は基準値にならない。
静止値はスナップショットで決める。そのため**スナップショットの状態を次の 3 つの規則で保つ**(今のコードはどれも満たしていない):

1. **無効化するときは中身も消す**。`hasSnapshot_ = false` にする全経路(Reset ボタン、Clip 選択の変更、対象オブジェクトの変更、
   `Reset()`)で `snapshot_.clear()` も行う。**Clip 選択の変更は、一覧のクリックだけでなく `+ Clip` / `+ Preset` / `- Clip` で
   `selClipIdx_` が変わる経路も含む**(今は `+ Clip` / `+ Preset` が `selClipIdx_` だけを書き換え、スナップショットも録画も旧 Clip のまま持ち越す)。
   実装は選択変更を 1 つのヘルパ(`SelectClip(index)`: 復元 → 破棄 → 録画停止 → 選択の更新)に集め、全経路から呼ぶ。今は Reset ボタン・Clip 変更・対象変更が `hasSnapshot_` だけを落とし、
   古い値(別オブジェクトの値や Reset 前の値)がマップに残る(`UIAnimationEditor.cpp` の `OnTargetChanged` / Clip 一覧 / Reset ボタン)
2. **足りないプロパティは使う前に足す**。静止値は 1 つのヘルパ `RestValue(obj, prop)` からだけ読み、`hasSnapshot_` が真で
   `snapshot_` に `prop` が無ければ、その場で `ReadFrom(obj)` を足してから返す。`ApplyScrub` の直前にも Clip の全 Track について同じ補完を行う。
   換算(`Rel` の切り替え、空 Track へのキー追加、オートキー)も `RestValue` を通すので、スクラブを止めた状態で Track を足して
   すぐ `Rel` を切り替える・キーを足す場合も補完される。スナップショット後に Track を追加・プロパティ変更した場合、
   そのプロパティは `ApplyScrub` がまだ一度も書いていないので、この時点の値が静止値として正しい
   (今は `hasSnapshot_` が真だと取り直さないので、相対では差分が累積し、絶対でも Reset で戻らない)
3. **録画中はスナップショットを手放さない**。`hasSnapshot_` を落とす経路(1 の全部)では**録画も止める**(`isRecording_ = false`、`recBaseline_.clear()`)。
   録画を続けるには Rec を入れ直す(ON の時点でスナップショットを取り直す。§17.5)。今は Reset ボタンと Clip 変更が録画を止めず、
   「基準 100 で Rec → Reset → 130 に編集」で、キーは古い基準 100 から、次のスクラブは 130 から計算されて食い違う

この 3 規則のもとで、静止値は次のとおり一意に決まる:

- `hasSnapshot_` が真 → `snapshot_[prop]`(規則 2 の `RestValue` が無ければ補完する)
- `hasSnapshot_` が偽 → `ReadFrom(obj)`(スクラブも録画もしていない = obj は静止状態)

**換算が要る箇所**:

| 箇所 | 今 | 相対 Track では |
| --- | --- | --- |
| `ApplyScrub` | `Apply(Sample(t))` | `Apply(静止値 + Sample(t))`。静止値はスナップショット(スクラブ前に取る既存の作法) |
| `AddKeyAtTime`(空の Track) | `ReadFrom(obj)` | `ReadFrom(obj) − 静止値`(スクラブしていなければ 0) |
| `AddKeyAtTime`(キーあり) | `Sample(t)` | そのまま(差分同士の補間) |
| `SetKeyAtTime`(オートキー) | `ReadFrom(obj)` | `ReadFrom(obj) − 静止値`。録画中は規則 3 によりスナップショットが必ずあるので、静止値 = `snapshot_[prop]`(Rec ON 時点で無かったプロパティは録画基準値が足されている。§17.5) |
| オートキーで新しく作る Track | 絶対 | 絶対のまま(相対は手で切り替える) |
| Keyframe インスペクタ | `X` / `Y` / `Value` | ラベルを `X (offset)` / `Y (offset)` / `Value (offset)` にする。値は差分をそのまま編集 |
| Timeline のキーのホバーツールチップ(`DrawTrackRow`) | `X=…` / `Y=…` | 相対の軸だけ `X=+…`(符号付き)。X/Y で相対・絶対が混在してもよく、軸ごとに出し分ける |
| Timeline のキーのホバーツールチップ(単独行) | `t=… v=… ease=…` | 相対なら `v=+…` |
| Timeline の右クリックの見出し(単独行) | `v=…` | 相対なら `v=+…` |

**Hold の注意書き**(§19.3)は Track 一覧の該当行の下に `TextDisabled` で出す。

### 19.6 責務

| ファイル | 追加・変更 |
| --- | --- |
| `UI/Animation/UIAnimationTrack.h` | `bool relative = false` |
| `UI/Component/UIAnimationComponent.cpp` | `ApplyLayers()` の手順 4、`CommitHold()`(基準値の未取得を含む) |
| `UI/Animation/UIAnimationSerializer.cpp` | `SavePropTrack` / `LoadPropTrack` の `"relative"`、`ValidateDetailed` の規則 |
| `UI/Debug/UIAnimationEditor.h` / `.cpp` | `Rel` トグルと換算、静止値ヘルパ、`ApplyScrub` / `AddKeyAtTime` / `SetKeyAtTime` / インスペクタ / 右クリック表示、Hold の注意書き |

`UIAnimationComponent.h` の公開 API・§6 の評価順・プリセット・既存 JSON は変えない。

### 19.7 やらないこと

- **レイヤーの加算合成**(§19.1)
- **乗算モード**(§19.2)
- **既存プリセットの相対化**。`SlideIn` は相対の方が素直だが、P9 は「選べるようにする」までにする(§18.6 と同じ判断)
- **Hold の累積を止める仕組み**(§19.3。仕様として残し、注意書きだけ出す)

---

# 第 9 部: Click 演出後の遷移

## 20. Click 演出後の遷移(P10)

### 20.1 やること

今は、ボタンの callback が `Pop()` / `Replace()` を積むと、同じフレームで ExitStart が始まる(§9.3)。
Click 演出(押した瞬間の縮み・光りなど)は Exit と並んで再生されるが、Exit が早く終わると途中で画面ごと消える。
「押した手応えを見せてから画面を出る」を JSON だけで作れるように、**Click 演出が終わるまで Exit の開始を待つ**。

**待つかどうかは JSON で決まる**。先頭画面に再生中の Click クリップが無ければ、今までどおり同じフレームで Exit を始める。
Click クリップを持たない画面(AquaDash の全画面)は挙動が変わらない。

### 20.2 状態機械(§9.2 に 1 段足す)

```
Pending(Pop / Replace が積まれた)
  ↓
ClickWait   : 先頭画面に再生中の Click クリップがあれば入る。無ければ素通り
  ↓          入力をロック(Hover などの Bool は false に)。画面の OnUpdate は止めない(まだ OnExit 前)
  ↓          IsTriggerPlaying(root, Click) が false になるか、2.0 秒でタイムアウト(警告ログ)
ExitStart   : 以降は §9.2 のとおり
```

- 判定するのは **Pop と Replace だけ**(`Back` から変換された Pop を含む)。`Push` は先頭画面を消さないので待たない。
  ただし ClickWait 中に積まれた op は(`Push` も含めて)pending 列に残り、ClickWait → Exit が終わってから順に処理される(§9.2 と同じ規則)
- **どのクリックの演出かは区別しない**。その画面で再生中の Click クリップが 1 本でもあれば待つ。
  ゲームコードがクリックと無関係に `Replace` を積んでも、直前のクリック演出が残っていれば待つことになる。
  「callback から積まれた op だけ待つ」にすると、callback の外で遷移を積む構成(状態機械経由など)で待たなくなるので採らない
- **起点の op を失わない**。ClickWait に入るとき、起点の Pop / Replace **自身も含めて**、未処理の残り op を順序どおり `pendingOps_` の先頭へ戻す
  (`BeginExit` が待機に入るときは起点を `exitOp_` に持ち、残りだけ戻す。ClickWait はまだ Exit を始めていないので起点も戻す)。
  ClickWait 中は `FlushPendingOps` が何もせず戻る(`exitWaiting_` と同じ)
- ClickWait を抜けたら、**同じフレームの Flush でそのまま ExitStart へ進む**。抜けた直後に Click を再判定しない(タイムアウトで抜けた場合に再び待たないように)。
  `clickWaitDone_` を立て、**Flush の中で最初に処理される Pop / Replace(= 先頭へ戻した起点の op)だけがそれを消費する**(判定を飛ばして false に戻す)。
  以後の Pop / Replace(次の画面に対するもの)は改めて Click を判定する
- タイムアウトは Exit と同じ 2.0 秒(`kClickWaitTimeoutSec`)。ループする Click は §20.5 で弾くので、通常は届かない保険
- `ReloadTopDocument()`(§3.3)は従来どおりこの状態機械を通さない。ただし ClickWait 中に積まれた Reload は、他の op と同じく待ってから処理される

### 20.3 再生中の Click を調べる

`UIAnimationSystem::IsAnimationGroupPlaying` は Manual のグループしか見ない。Click は Trigger(`conditionParam` = `Click`)なので、別に足す:

| 追加 | 内容 |
| --- | --- |
| `UIAnimationComponent::IsTriggerPlaying(uint32_t trigger) const` | `condition == Trigger && conditionParam == trigger && runtime.active` のクリップが 1 本でもあれば true |
| `UIAnimationSystem::IsTriggerPlaying(const UIObject* root, uint32_t trigger)` | root 以下を再帰で調べる(`IsAnimationGroupPlaying` と同じ形) |

Trigger クリップは完了すると Hold / Restore のどちらでも `active = false` になる(§5.3)ので、「再生中」= `active` で足りる。
**非アクティブな階層は調べない**(`IsActiveInHierarchy()` が false のノードとその子は飛ばす)。`UIAnimationSystem::Update` はその階層を更新しないので、
callback や `OnUpdate` がボタンを非表示にすると Click の `active` が残ったまま時刻が進まず、待つと必ずタイムアウトまで止まってしまう。
更新の対象と待つ対象をそろえる(`UpdateObject` と同じ条件)。

### 20.4 入力のロック

ClickWait 中は `OnExit()` 前なので `exiting_` は立てない(立てると `OnUpdate` が止まり、Exit の意味も変わる)。
代わりに `UIScreen` に `inputLocked_` を足し、**`IsInputLocked()` = `exiting_ || inputLocked_`** とする。
`UIInputSystem` が `IsExiting()` で入力を止めている 2 か所(フォーカス入力と HitTest)を `IsInputLocked()` に置き換える。
ClickWait の開始で `inputLocked_ = true` と `UIInputSystem::ClearState()`(Bool 条件を false に)、ExitStart 以降は従来どおり `exiting_` が効く。

### 20.5 検証

`ValidateDetailed` に 1 規則足す: `condition == Trigger` かつ `conditionParam == Click` のクリップに `loopFrom >= 0` があればエラー
(`clip '<name>': Click clips must not loop`。Exit グループのループ禁止と同じ理由)。

### 20.6 責務

| ファイル | 追加・変更 |
| --- | --- |
| `UI/Component/UIAnimationComponent.h` / `.cpp` | `IsTriggerPlaying` |
| `UI/Animation/UIAnimationSystem.h` / `.cpp` | `IsTriggerPlaying(root, trigger)` |
| `UI/Screen/UIScreen.h` | `inputLocked_` と `IsInputLocked()` |
| `UI/Screen/UIScreenManager.h` / `.cpp` | ClickWait の状態と `Update` / `FlushPendingOps` / `BeginExit` の分岐、`kClickWaitTimeoutSec` |
| `UI/Input/UIInputSystem.cpp` | `IsExiting()` → `IsInputLocked()`(2 か所) |
| `UI/Animation/UIAnimationSerializer.cpp` | 検証規則 |

エディタ・JSON の形式・既存の Exit 待機の挙動(Click クリップが無い場合)は変えない。

### 20.7 やらないこと

- **クリック単位での待ち分け**(§20.2)
- **待たずに遷移する指定**(op ごとの `immediate` 等)。要求が出たら足す
- **Click 以外の Trigger を待つこと**。待つのは `Click` だけ
