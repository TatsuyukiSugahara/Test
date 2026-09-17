# UIアニメーション統合設計

> 対象コミット: b88b5fe(P0)/ 最終更新: 2026-09-17

UI アニメーションを「JSON だけで動き、UI Editor 1 つで設定できる」状態にする。
2026-09-16 に階層統合(`Clip → ClipTrack → PropTrack → Keyframe` の 4 階層を
`Clip → Track → Keyframe` の 3 階層へ)だけを対象に初版を書き、
2026-09-17 の 5 回のレビューで **プロパティ競合規則・状態遷移・エディタ統合・画面遷移** まで
範囲を広げた。P0〜P4 は 2026-09-17 に実装し Mac(Metal / Debug)で評価済み。§12 の後続改善は P4(プリセット)から順に本書へ取り込む。

本書の構成は実施順に並べてある。

| 部 | フェーズ | 内容 |
| --- | --- | --- |
| 第 1 部 | P0 | 編集・保存基盤(共通選択 / 保存 / Reload / texturePath) |
| 第 2 部 | P1 / P2 / P2B | データ構造・排他モデル / レイヤー評価と基本自動フック / Exit 待機遷移 |
| 第 3 部 | P3 | 統合エディタ(Animation タブ / タイムライン / プレビュー) |
| 第 4 部 | P4 | プリセット(§14) |
| 後続 | — | ベクタトラック / オートキー / Ease / 相対値 |

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
「Click 演出が終わってから Exit を始める」は今回作らない。必要なら Click クリップの長さ分だけ遷移を遅らせる仕組みを後続で足す。

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
- [ ] Windows / D3D11 でビルドが通り、警告が増えていない(Windows 機の宿題。Mac の Metal は Debug / Release とも警告増なし)

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
- [ ] Windows / D3D11 でビルドが通り、警告が増えていない(Windows 機の宿題。Mac の Metal は Debug / Release とも警告増なし。`UIAnimationClip` の struct/class 不一致警告が 1 件減った)

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
- [ ] Windows / D3D11 でビルドが通り、警告が増えていない(Windows 機の宿題)

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
- [ ] Windows / D3D11 でビルドが通り、警告が増えていない(Windows 機の宿題)

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
- [ ] Windows / D3D11 でビルドが通り、警告が増えていない(Windows 機の宿題)

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
- [ ] Windows / D3D11 でビルドが通り、警告が増えていない(Windows 機の宿題)

P4 の実装で気づいたこと

- Duration を伸ばしてもキーの時刻は動かない(0.3 秒のキーのまま末尾でホールド)。キーの時間を Duration に比例して伸ばす操作は §12 の後続
- Play when が Enter / Exit のときも Group 欄が出る(§10.3 は Manual のときだけ)。エディタ寸法の修正と一緒に直す(§13)

---

## 12. 後続改善(本書の範囲外。この順で続ける)

本書が入ってから着手する。逆順にすると全部書き直しになる。

1. ~~**プリセット**~~ → **P4 として本書に取り込んだ(§14)**
2. **ベクタトラック** — `PositionX` / `PositionY` を 1 行の `Position` として扱い、タイムラインの行数を減らす
3. **オートキー** — 録画中に値をいじった瞬間、現在のスクラブ時刻へキーを自動追加する
4. **Ease 拡充** — `Back` / `Elastic` / `Bounce` と曲線プレビュー。現状 5 種のみ、`Bezier` の実体は smoothstep。
   イージングは左キーが右への区間を支配することを UI に出す
5. **相対値モード** — キー値に「絶対 / 初期値からの差分」を選べるようにし、レイアウト変更でアニメがズレないようにする
6. **Click 演出完了後の遷移** — §9.3 で見送った「Click クリップが終わってから Exit を始める」
7. **`Pause()`** — 途中値を保持したまま止める要求が出たら

---

## 13. 決めていないこと

- Exit 待機のタイムアウトは 2.0 秒で確定(P2B、`kExitTimeoutSec`)。変える要求が出たら setter を足す
- `Save As` は P0 で作った(ポップアップでパス入力)。使われなければ後で外す
- P1 のエディタ追従で Cond / Finish は enum 名(Manual / Bool / Trigger、Hold / Restore)のまま出している。§10.3 の「Play when」「Keep / Return」への言い換えは P3
- `priority`(§6.2)を足す条件。serial で足りなくなった実例が出るまで足さない
- **エディタの寸法(2026-09-17 ユーザー指摘、後で直す)**: UI Editor の既定サイズ(700x560)では Animation タブと Timeline で縦横が
  足りず、TextStyle ポップアップも幅が足りない。既定サイズの拡大と、Timeline の高さを内容に合わせる(P4 以降の小改修)

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
