# UIアニメーション 階層統合設計

UI アニメーションのデータ構造を `Clip → ClipTrack → PropTrack → Keyframe` の 4 階層から
`Clip → Track → Keyframe` の 3 階層へ畳む。2026-09-16 設計、実装未着手。

「UI の設定がやりづらい / 特にアニメーションが設定しづらい」への対応のうち、
**データ構造そのものに手を入れる部分**だけをここに置く。
エディタ UX の改善(プリセット、オートキー、自動フック)は本書の構造変更の上に乗るため、
順序としてこちらが先になる(§5)。

---

## 0. 調査で分かったこと

### 0.1 ClipTrack は自前の長さを持っていない

| 型 | 持っているもの |
| --- | --- |
| `UIAnimationClip` | `name` / `duration` / `clipTracks[]` |
| `UIClipTrack` | `name` / `condition` / `conditionParam` / `loopFrom` / `loopSkipFirst` / `restoreOnComplete` / `tracks[]` |
| `UIAnimationTrack` | `property` / `keyframes[]` |
| `UIKeyframe` | `time` / `value` / `ease` |

`UIClipTrack` に `duration` はなく、[UIAnimationComponent.cpp](../aqEngine/UI/Component/UIAnimationComponent.cpp) の
`Update()` は全 ClipTrack を `currentClip_->duration` で評価している。
結果、**同一クリップ内のイントロとホバーループが強制的に同じ秒数になる**。

### 0.2 ClipTrack が存在する理由は「同時実行の受け皿」

`currentClip_` は単数で、`Play()` は `runtimes_` を毎回クリアする。
つまりクリップは同時に 1 本しか走らない。
「出現アニメを流しつつ、ホバー中は光らせる」を書ける場所が他になく、
その受け皿として Clip の内側にもう 1 層が必要だった。これが ClipTrack の正体。

同時実行の受け皿を Clip 側へ移せば、この層は役目を失う。

### 0.3 移行対象が存在しない

- `UIAnimationComponent::Play()` / `TriggerTrack()` / `SetCondition()` の呼び出しはリポジトリ内に 0 件
- `"animation"` セクションを持つ UI アセットも 0 件(`Game/Assets/UI/` 配下)

現在のゲーム UI の演出は [AquaDashScreens.cpp](../Game/Application/UI/AquaDashScreens.cpp) の
`OnUpdate()` で `elapsed_` を自前で回す手書きで、アニメ機構は使われていない。
**JSON 互換を壊すコストが 0 なのは今だけ**で、これが本書を先にやる根拠になる。

### 0.4 その他、統合ついでに消える暗黙ルール

- クリップの完了判定を `condition == Default` のトラックだけが担っている。
  Default トラックを作り忘れたクリップは永久に終わらない
- エディタが `selClipTrackIdx_` / `selPropTrackIdx_` の二重インデックスで選択を持っている

---

## 1. 方式決定

### 1.1 Clip と ClipTrack を統合する

`UIClipTrack` を廃止し、条件・ループ・復帰設定を `UIAnimationClip` が直接持つ。
`runtimes_` を **ClipTrack 単位から Clip 単位へ**変え、複数クリップの並走を許す。

```cpp
struct UIAnimationClip
{
    std::string      name;                       // 表示・保存用
    std::string      groupName;                  // 表示・保存用。空 = name と同じ
    uint32_t         group             = 0u;     // 実行時の識別子。0 = 未解決
    float            duration          = 0.f;    // クリップごとに持つ
    UIClipCondition  condition         = UIClipCondition::Manual;
    uint32_t         conditionParam    = 0u;
    std::string      conditionParamName;         // 表示・保存用
    float            loopFrom          = -1.f;
    bool             loopSkipFirst     = false;
    bool             restoreOnComplete = false;

    std::vector<UIAnimationTrack> tracks;
};
```

`UIAnimationTrack` と `UIKeyframe` は変更しない。

### 1.2 起動条件は Clip が持ち、Bool / Trigger は常時評価

| condition | 起動 | 用途 |
| --- | --- | --- |
| `Manual` | `Play(group)` を呼んだとき | 画面の出現・退場など、ゲーム側が任意のタイミングで出すもの |
| `Bool` | `GetCondition(param)` が真の間 | ホバー、フォーカス、押下中の継続演出 |
| `Trigger` | `TriggerTrack(param)` で 1 回 | クリック、被弾などの単発演出 |

`Bool` / `Trigger` のクリップは**常駐して毎フレーム評価される**。
`Play()` を呼ばなくても動くので、ボタンの hover / press は JSON だけで組めるようになる
(この性質を使って UIScreen / UIButton へ自動フックするのが次の作業。§5)。

旧 `UITrackCondition::Default` は `Manual` に置き換わる。

### 1.3 グループ起動は 32bit ハッシュで持つ

複数の `Manual` クリップをまとめて起動する手段を、クリップ側のデータとして持たせる。
呼び出し側(C++)にグループを書かせない — 演出にクリップを 1 本足すたびに
C++ を書き換えに行く状態を避けるため。

**規則は 1 本だけにする。**

```
groupName を省略したクリップは group = aqHash32(name)
Play(x) = group == x の Manual クリップを全部起動する
```

グループを使わない単発クリップは今までどおり `Play(aqHash32("Open"))` で動く。
「クリップ名かグループ名か」の曖昧さが生まれない。

識別子は `std::string` ではなく `uint32_t` で持つ。
スケルタルアニメの [AnimationComponent](../aqEngine/Component/AnimationComponentSystem.h) が
`std::map<uint32_t, AnimationSlot>` / `Play(uint32_t nameHash)` / `0 = なし` を既に採っており、
UI 側だけ文字列比較を毎フレーム回す理由がない。ハッシュは `aqHash32()`
([Util/CRC32.h](../aqEngine/Util/CRC32.h)、constexpr CRC32)を使う。

**文字列は authoring 用、ハッシュは実行時用**と役割を分ける。

- JSON には `"group": "Open"` と**文字列で書く**(手編集できる形を維持する)
- ロード時に `aqHash32()` で `group` を埋める。`groupName` が空なら `name` をハッシュする
- 保存とエディタ表示は `groupName` / `conditionParamName` を使う
- `Update()` が触るのは `uint32_t` だけ。文字列比較は 1 回も走らない

`conditionParam` も同じ理由でハッシュにする。
`SetCondition()` / `TriggerTrack()` / 毎フレームの条件評価がすべて文字列比較になっているため、
`group` だけハッシュ化しても片手落ちになる。
**これは「group をハッシュに」の指示を条件パラメータへ広げた判断なので、不要なら 1.3 のこの段落だけ落とす。**

### 1.4 完了判定はグループ単位

「グループ内の `Manual` クリップが全部終わったら完了」とする。
`condition == Default` のトラックだけが完了を担う特例(§0.4)は廃止。

`Stop()` は引数ありでグループ停止、引数なしで全 `Manual` 停止。
`Bool` / `Trigger` のクリップは常駐なので `Stop()` の対象外。

### 1.5 失うものと代替

`Play()` 1 回で複数の条件付きトラックをまとめて起動する原子性を失う。

代替は §1.3 のグループで足りる。
そもそも `Bool` / `Trigger` は常駐になって `Play()` を必要としないため、
まとめて起動したいのは `Manual` クリップ同士だけになる。

---

## 2. JSON フォーマット

### 現在

```json
{
  "animation": {
    "clips": [{
      "name": "Open",
      "duration": 0.4,
      "clipTracks": [
        { "name": "Intro", "condition": "Default",
          "tracks": [ { "property": "ColorA", "keyframes": [] } ] },
        { "name": "Hover", "condition": "Bool", "conditionParam": "Hover", "loopFrom": 0.0,
          "tracks": [ { "property": "ColorA", "keyframes": [] } ] }
      ]
    }]
  }
}
```

### 統合後

```json
{
  "animation": {
    "clips": [
      { "name": "OpenSlide", "group": "Open", "duration": 0.4, "condition": "Manual",
        "tracks": [ { "property": "PositionY", "keyframes": [] } ] },
      { "name": "OpenFade",  "group": "Open", "duration": 0.25, "condition": "Manual",
        "tracks": [ { "property": "ColorA", "keyframes": [] } ] },
      { "name": "Hover", "duration": 0.8, "condition": "Bool", "conditionParam": "Hover",
        "loopFrom": 0.0,
        "tracks": [ { "property": "ColorA", "keyframes": [] } ] }
    ]
  }
}
```

`Play(aqHash32("Open"))` で 2 本が**それぞれの長さ**で走る。
`Hover` は `Play` を呼ばずに条件だけで動く。
`"group"` を省略したクリップは自分の名前がグループになる。

`clipTracks` キーは読み捨てる(移行対象が 0 件なので互換コードは書かない)。

---

## 3. 責務表

### 変更ファイル

| ファイル | 変更 |
| --- | --- |
| [UI/Animation/UIClipTrack.h](../aqEngine/UI/Animation/UIClipTrack.h) | **削除**。`UITrackCondition` は `UIClipCondition`(`Manual` / `Bool` / `Trigger`)として `UIAnimationClip.h` へ移す |
| [UI/Animation/UIAnimationClip.h](../aqEngine/UI/Animation/UIAnimationClip.h) | 条件・ループ・復帰設定と `group` / `conditionParam`(ハッシュ)を持つ。`clipTracks` を `tracks` に置換 |
| [UI/Component/UIAnimationComponent.h](../aqEngine/UI/Component/UIAnimationComponent.h) / [.cpp](../aqEngine/UI/Component/UIAnimationComponent.cpp) | `clips` を `std::vector<UIAnimationClip>` へ(名前順の不定を解消)。`TrackRuntime` を Clip 単位に。`Play(uint32_t)` / `Stop(uint32_t)` / `SetCondition(uint32_t, bool)` / `TriggerTrack(uint32_t)`。`currentClip_` を廃止し、条件付きクリップは常駐 |
| [UI/Animation/UIAnimationSerializer.h](../aqEngine/UI/Animation/UIAnimationSerializer.h) / [.cpp](../aqEngine/UI/Animation/UIAnimationSerializer.cpp) | `SaveClipTrack` / `LoadClipTrack` を削除。`group` / `conditionParam` を文字列で入出力し、ロード時に `aqHash32()`。`ConditionToStr` の `Default` を `Manual` へ |
| [UI/Debug/UIAnimationEditor.h](../aqEngine/UI/Debug/UIAnimationEditor.h) / [.cpp](../aqEngine/UI/Debug/UIAnimationEditor.cpp) | `selClipTrackIdx_` を廃止し選択を 1 本化。左パネルは `groupName` 見出しの下にクリップを並べる(見た目の 2 段グループは維持、データは平ら)。クリップ行に条件 / ループ / duration を出す |
| [UI/Resource/UIDocumentLoader.cpp](../aqEngine/UI/Resource/UIDocumentLoader.cpp) | 変更なし(`UIAnimationSerializer::LoadAll` に委譲しているため) |

新規ファイルなし。`.vcxproj` からは `UIClipTrack.h` の登録を外す。

---

## 4. フェーズ計画

### P0: 保存先のバグを止める(本書の前に入れる)

[UIAnimationEditor.cpp:1092](../aqEngine/UI/Debug/UIAnimationEditor.cpp#L1092) の `Save` が、
選択オブジェクトのクリップを JSON の**ルートノード**の `"animation"` に書いている。
`UIDocumentLoader` は `"animation"` をノードごとに読むため、
子オブジェクトを選んで保存すると別ノードにアニメが付く。

- [ ] 選択オブジェクトのルートからの名前パスを辿り、該当ノードへ書く
- [ ] 保存パスの既定値をドキュメントのロード元にする(512 バイトの手入力をやめる)
- [ ] 子オブジェクトを選んで保存 → 再ロードで同じ子に付くことを確認

### P1: データ構造とシリアライザ

- [ ] `UIClipTrack.h` を削除し、`UIClipCondition` を `UIAnimationClip.h` へ移す
- [ ] `UIAnimationClip` に `groupName` / `group` / `condition` / `conditionParam` / `conditionParamName` / ループ設定を持たせる
- [ ] `UIAnimationSerializer` を新フォーマットに合わせ、`group` / `conditionParam` をロード時にハッシュ化する
- [ ] `groupName` が空のとき `group == aqHash32(name)` になることを確認
- [ ] Windows / D3D11 でビルドが通り、警告が増えていない

### P2: ランタイム

- [ ] `runtimes_` を Clip 単位にし、複数クリップの並走を許す
- [ ] `Bool` / `Trigger` クリップを常駐評価にする(`Play()` 不要)
- [ ] `Play(uint32_t)` がグループ内の `Manual` クリップを全部起動する
- [ ] 完了判定をグループ単位にし、`Default` トラック特例を消す
- [ ] `Stop(uint32_t)` / `Stop()` を実装する
- [ ] `Update()` から文字列比較が消えていること(`std::string` の比較が 0 箇所)
- [ ] 手書き JSON 1 枚で「出現 + ホバー光り」が動くことを実機で確認

### P3: エディタ

- [ ] 選択インデックスを 1 本化し、タイムラインを「左 = クリップ一覧、右 = プロパティ行」にする
- [ ] クリップ行に条件 / グループ / ループ / duration を出す
- [ ] 左パネルで `groupName` 見出しごとにクリップをまとめる
- [ ] ClipTrack 名の入力バッファ共有(`ctNameBuf_` / `condParamBuf_` を全トラックで使い回し、毎フレーム上書き)が
      構造変更で消えることを確認する
- [ ] プレビューを `ApplyScrub` 直書きではなく `Play()` / `TriggerTrack()` 経由にし、
      ループ・条件・`restoreOnComplete` を実挙動で確認できるようにする

---

## 5. 本書の範囲外(この順で続ける)

本書(P0 → P1 → P3)が入ってから着手する。逆順にすると全部書き直しになる。

1. **自動フック** — `UIScreen` の Enter / Exit で `"Enter"` / `"Exit"` グループを再生し、
   `UIButtonComponent` の `isHovered` / `isPressed` / `isFocused` を `SetCondition()` へ橋渡しする。
   クリックで `TriggerTrack()`。これで C++ を書かずに UI が動く状態になる
2. **プリセット** — `FadeIn` / `FadeOut` / `PopIn` / `SlideIn` / `Blink` / `Shake` をワンクリック挿入。
   必要なトラックとキーを裏で生成する
3. **ベクタトラック** — `PositionX` / `PositionY` を 1 行の `Position` として扱い、
   タイムラインの行数を減らす
4. **オートキー** — 録画中に値をいじった瞬間、現在のスクラブ時刻へキーを自動追加する
5. **Ease 拡充** — `Back` / `Elastic` / `Bounce` と曲線プレビュー。
   現状 5 種のみ、`Bezier` の実体は smoothstep。イージングは左キーが右への区間を支配することを UI に出す
6. **相対値モード** — キー値に「絶対 / 初期値からの差分」を選べるようにし、
   レイアウト変更でアニメがズレないようにする

---

## 6. 決めていないこと

- `conditionParam` のハッシュ化(§1.3 末尾)を P1 で同時にやるか、後続にするか
- グループの完了を通知する口(コールバック / ポーリング)が要るか。
  現状は誰も完了を見ていないため、必要になってから足す
