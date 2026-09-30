# System アクセス宣言設計

> 対象コミット: ef0b420 / 最終更新: 2026-09-30 / **保留**(2026-09-30。想定より複雑なため見送り、問題が出たときに再検討する)

**保留中の扱い**: ②(wave の待ち合わせをやめる方式)は、本書の検証が無いと依存の経路が無い System 同士が重なり得る(1.2)。
②に着手する場合は、先に本書を再開すること。1.3 の競合候補は README の「既知の課題」にも記録してある。

各 System が**何を読み、何を書くか**を宣言させ、起動時に
「競合する System 同士に依存の経路があるか」を検証する設計。
経路が無い競合は登録エラーとし、[System登録契約設計](System登録契約設計.md) の仕組みで初期化失敗として止める。

System 並列化の改善の順(③ → ① → 計測 → ②)のうち、本書は **①** を扱う。③は完了済み。

---

## 0. 目的と範囲

今は競合を避けるために、開発者が依存を手で張り、理由をコメントで残している
([Core/Application.cpp:653-655](../aqEngine/Core/Application.cpp#L653-L655))。
付け忘れても、ビルドでも起動でも何も言われない。

本書で作るもの:

- System ごとの**アクセス宣言**(Component 型と、Component 以外の共有資源)
- 起動時の**競合検証**: 競合する 2 つの System の間に、直接または間接の依存経路があることを要求する
- 競合してもよいと分かっている組の**明示的な除外**(理由付き)
- Debug 限定の**宣言漏れ検出**(宣言していない Component 型に触ったら止める)

本書で作らないもの:

- 依存の自動生成。読み書きの宣言からは「どちらを先に走らせるべきか」というゲーム上の意味が決まらないため。
  検証は「競合を見つけて、依存の指定を求める」ところまでにする。
- ②(wave 単位の待ち合わせをやめる)の実行方式。ただし本書の検証は②の移行に必要な条件を先に満たす(1.2)。

---

## 1. 現状

### 1.1 アクセスの実態(ef0b420 で調査)

エンジン 8 個・ゲーム 8 個の System の `Update()` を読んだ結果。

- `Foreach<Cs...>` のラムダ引数はすべて `T*`(非 const)で、**API のシグネチャから読み書きを推論できない**
  ([ECS/ECS.h:14-18](../aqEngine/ECS/ECS.h#L14-L18))。
  const を表せるのは `GetSingletonComponent<const T>` だけ([ECS/EntityContext.h:202-212](../aqEngine/ECS/EntityContext.h#L202-L212))。
- Component 以外にも、複数の System が触る共有資源がある: カメラ(`CameraManager`)、入力(`GameInput`)とパッドの出力(振動・トリガー抵抗)、
  `SoundEngine` / `AudioDirector`、`PrefabRegistry`(mutex なし)、`LevelManager`(mutex なし)。
- `ResourceManager` は内部で mutex を取るので、System 間の競合の対象にしない。
- `HierarcicalTransformSystem` は、走査している Entity とは別の Entity(親)の `HierarchicalTransformComponent` も書く。
  そのため宣言は「クエリした行」ではなく**型単位**で扱う。
- `RenderSystem` は `Foreach<TransformComponent, HierarchicalTransformComponent, X>` の `TransformComponent` を受け取るが使っていない
  (対象を絞るためだけ)。読みとして宣言すると、`TransformComponent` を書く System と見かけ上の競合になる。
- `RenderSystem::BuildRenderFrame()`、`DebugRender*`、GameFlow の状態クラスなどは、ECS の Update の**外**でメインスレッドから呼ばれる。
  System の並列実行とは重ならないので、本書の対象外。

### 1.2 wave 方式が隠している問題

wave が違う System 同士は、依存の経路が無くても今は順番に走る。
その順番は、たまたま決まった level の結果にすぎない。
別の場所で依存を 1 本足すと level がずれ、並走するようになることがある。
②に移ると、依存の経路が無い System はいつでも並走し得る。

したがって検証は「同じ wave かどうか」ではなく**依存経路の有無**で判定する。

### 1.3 見つかった競合の候補

依存経路が無いのに、同じ型・資源を片方が書き、もう片方が読むか書く組。level は ef0b420 の登録から計算した値。

| # | 組(level) | 対象 | 状況 |
| --- | --- | --- | --- |
| 1 | `ActorStateMachineSystem`(1)↔ `SpeedCharacterSystem`(1) | `TransformComponent` 書き / 書き | **同じ wave で並列に走っている**。AquaDash には `StateMachineComponent` を持つ Entity が無いので実害は出ていない |
| 2 | `ActorStateMachineSystem`(1)↔ `CoinSystem`(2) | `TransformComponent` 書き / 読み書き | wave が違うので今は順番に走る |
| 3 | `ActorStateMachineSystem`(1)↔ `AutoCameraSystem`(2) | `TransformComponent` 書き / 読み | 同上 |
| 4 | `CoinSystem`(2)↔ `AutoCameraSystem`(2) | `TransformComponent` 書き(取得演出の Entity)/ 読み(プレイヤー) | **同じ wave で並列に走っている**。触る Entity は別なので実際のデータ競合は無い |
| 5 | `AutoCameraSystem`(2)↔ `CameraSteeringSystem`(4) | カメラ 書き / 書き | wave の順で後者が上書きしている |
| 6 | `CharacterSteeringSystem`(0)↔ `AutoCameraSystem`(2) | カメラ 読み / 書き | wave が違うので今は順番に走る |
| 7 | `CharacterSteeringSystem`(0)↔ `SpeedCharacterSystem` / `CoinSystem` | 入力 読み / パッド出力 書き | 入力状態とパッドの出力バッファは別データ。資源を分けて宣言すれば競合しない |

エンジン System 同士には、依存経路の無い競合は見つかっていない
(`RenderSystem` の `TransformComponent` を「絞り込みのみ」として扱う前提)。
`CharacterSteering` / `ActorStateMachine` / `CameraSteering` / `CameraEffect` の 4 つは、
AquaDash の現行フローでは対象の Component を持つ Entity が作られず、実質的に空回りしていると推測している。空回りしていても宣言は必要。

---

## 2. 方針

### 2.1 宣言は System のクラスに書く

`SystemBase` に仮想関数を 1 つ足し、各 System が自分のアクセスを宣言する。

```cpp
class SystemBase
{
public:
	/** この System が Update() で触るものを宣言する。オーバーライドしない System は「未宣言」 */
	virtual void DeclareAccess(SystemAccess& access) const;
	...
};

void SpeedCharacterSystem::DeclareAccess(aq::ecs::SystemAccess& access) const
{
	access.Read<SessionComponent>();
	access.Read<PlayerInputComponent>();
	access.Write<SpeedCharacterComponent>();
	access.Write<aq::ecs::TransformComponent>();
	access.Write<PlayerScoreComponent>();
	access.Write<aq::ecs::AnimationComponent>();
	access.Write<aq::ecs::resource::SoundEngine>();
	access.Write<aq::ecs::resource::PadFeedback>();
}
```

[使いやすさ改善設計](使いやすさ改善設計.md) P2-C では、**依存**を `SystemBase` のメタ情報に持たせる案を採らなかった。
エンジンの System はゲームの System を知らないので、依存を型の側に書けないためである。
**アクセス**は System が自分の中身だけで決められるので、この制約に当たらない。

### 2.2 宣言の種類

| 種類 | 意味 | 競合するか |
| --- | --- | --- |
| `Read<T>` | 読むだけ | `Write` とだけ競合する |
| `Write<T>` | 書く(読みも含む) | `Read` / `Write` の両方と競合する |
| `Filter<T>` | クエリの絞り込みにだけ使い、中身には触らない | 競合しない |

`T` は Component 型か、2.3 の資源タグ。

### 2.3 Component 以外の共有資源

資源ごとにタグ型を用意し、Component と同じ宣言で扱う。キーは Component と同じく型名のハッシュ
(`TYPE_INFO` の `GetTypeHash()`。[ECS/TypeInfo.h](../aqEngine/ECS/TypeInfo.h))。

| タグ | 対象 | 主な利用者 |
| --- | --- | --- |
| `resource::Camera` | `CameraManager` のカメラ全体(カメラ別には分けない) | AutoCamera・CameraSteering(書き)、CameraEffect(読み書き)、CharacterSteering(読み) |
| `resource::InputState` | `GameInput` の入力状態(メインスレッドが ECS 更新の前に作る) | 入力を読む System。読みだけなので、System 同士では競合しない |
| `resource::PadFeedback` | パッドの振動・トリガー抵抗の出力 | SpeedCharacter・Coin(書き) |
| `resource::SoundEngine` | `SoundEngine` と `AudioDirector` | SpeedCharacter・Coin・SoundSystem(書き) |
| `resource::PrefabRegistry` | `PrefabRegistry` のキャッシュ | SpawnSystem(書き) |
| `resource::LevelManager` | `LevelManager` | LevelStreamSystem(書き) |

- 中で排他を取っているサービス(`ResourceManager` など)は宣言しない。どのサービスが該当するかは本書に一覧で持つ。
- `SessionComponent` は Component として `Read` で宣言する。書くのはメインスレッドの GameFlow だけで、ECS の更新とは重ならない。

### 2.4 検証

`BuildSchedule()` でトポロジカルソートに成功した後に行う。循環がある場合は到達可能性が決まらないので、検証を飛ばして循環だけを報告する。

1. 全 System の宣言を集める。宣言していない System は登録エラー(2.6 の移行期間を除く)。
2. 依存グラフの到達可能性を求める(A から B へ、依存を辿って行けるか)。
3. 同じキーを持つ 2 つの System が「書き / 書き」または「書き / 読み」で、**どちら向きにも経路が無く**、2.5 の除外にも当たらなければ登録エラー。

エラー文の例:

```
[ecs] access conflict without dependency: app::ecs::CoinSystem (Write TransformComponent) <-> app::ecs::AutoCameraSystem (Read TransformComponent)
```

System が少ないうちは、到達可能性は各 System からの深さ優先探索で十分と見込む。起動時に 1 回だけ走る処理なので導入しやすいが、
所要時間は実装後に起動ログで確かめる。

### 2.5 除外

同じ型を触っていても、実際には別の Entity しか触らないと分かっている組には、理由付きで除外を指定できる。

```cpp
aq::ecs::EntityContext::Get().AllowConcurrentAccess<app::ecs::CoinSystem, app::ecs::AutoCameraSystem, aq::ecs::TransformComponent>(
	"Coin は取得演出の Entity、AutoCamera はプレイヤーの Transform だけを触る");
```

- 除外は組と型を指定し、System 同士の組全体をまとめて許可はしない。
- 理由の文字列は必須(空なら登録エラー)。起動ログには除外の件数だけを出す。
- 除外の中身(Entity の集合が別であること)は起動時には検証できない。除外は依存を張れない理由があるときだけに使う。

### 2.6 宣言漏れの扱い

- **System 単位の漏れ**(`DeclareAccess` をオーバーライドしていない): 登録エラー。未宣言を「何も触らない」とは扱わない。
- **型単位の漏れ**(宣言した一覧に、実際に触る型が入っていない): 起動時には分からない。
  Debug 限定で、System の実行中に `Foreach` / `GetView` / `GetComponent` / `GetSingletonComponent` が宣言外の Component 型に触ったらアサートで止める(P3)。
  - 実行中の System は、`SystemManager::Update()` がワーカーに渡すラムダの中でスレッドローカル変数に入れる。
  - 読み書きの別は判定できない(`Foreach` が `T*` を渡すため)。見るのは「宣言した型の中にあるか」だけ。
  - 資源タグ(カメラ・サウンド等)は、各サービスの入口に手を入れないと検出できないので、P3 の対象外とする。

---

## 3. フェーズ

エンジン側だけを先に入れると、ゲームの System が未宣言のまま起動できなくなる。そこで、未宣言の扱いを段階的に切り替える。

### P1: エンジン側の仕組みとエンジン System の宣言(`<Engine>`)

| ファイル | 変更内容 |
| --- | --- |
| `ECS/SystemAccess.h`(新規) | `SystemAccess`(`Read` / `Write` / `Filter`)と、資源タグ `resource::*` |
| [ECS/System.h](../aqEngine/ECS/System.h) / [.cpp](../aqEngine/ECS/System.cpp) | `SystemBase::DeclareAccess` を追加。`SystemEntry` に宣言を保持。`AllowConcurrentAccess` を追加。`BuildSchedule()` に 2.4 の検証を追加し、エラーは既存の登録エラー一覧へ入れる |
| [ECS/EntityContext.h](../aqEngine/ECS/EntityContext.h) | `AllowConcurrentAccess` の中継 |
| エンジンの System 8 個 | `DeclareAccess` を実装する(`RenderSystem` の `TransformComponent` は `Filter`) |

- この段階では、**未宣言の System は検証から外し、名前を起動ログに出すだけ**にする(ゲーム側がまだ宣言していないため)。
- 新規ファイルの追加は `vs-project-files` スキルに従い、フィルターはユーザーに確認する。

### P2: ゲーム System の宣言と、既存の競合の解消(`<Game>`)

ゲームの System 8 個に `DeclareAccess` を実装し、1.3 の競合を次のように解消する。
そのうえで、未宣言の System を登録エラーに切り替える(エンジンの 1 行の変更。本文に明記してこのコミットに含める)。

| # | 解消方法 | 実行順への影響 |
| --- | --- | --- |
| 1 | 依存を追加(`ActorStateMachineSystem` を `SpeedCharacterSystem` の後) | ActorStateMachine が level 1 → 2 になる(下の注を参照) |
| 2 | 依存を追加(`CoinSystem` を `ActorStateMachineSystem` の後) | #1 と合わせると Coin が level 2 → 3 |
| 3 | 依存を追加(`AutoCameraSystem` を `ActorStateMachineSystem` の後) | #1 と合わせると AutoCamera が level 2 → 3 |
| 4 | 除外(`TransformComponent`。触る Entity が別) | 変わらない |
| 5 | 依存を追加(`CameraSteeringSystem` を `AutoCameraSystem` の後) | 今の順番と同じ |
| 6 | 依存を追加(`AutoCameraSystem` を `CharacterSteeringSystem` の後) | 今の順番と同じ |
| 7 | 資源を `InputState`(読み)と `PadFeedback`(書き)に分けて宣言 | 競合にならない |

後ろの level にある System へ、前の level の System から辺を足すだけなら、どの level も変わらない(#5・#6、および #1 を除外で解消した場合の #2・#3)。
暗黙だった順番が明示されるだけになる。

ただし #1 を依存で解消すると、ActorStateMachine が level 2 に上がる。
その後に #2・#3 を足すと Coin・AutoCamera が level 3 になり、`HierarcicalTransformSystem` 以降もすべて 1 つずつ後ろへずれる
(wave が 1 つ増え、待ち合わせが 1 回増える)。#1 の向きを逆にしても(SpeedCharacter を後にする)、同じく 1 段ずれる。

**要判断**: #1 と #4 は、今は同じ wave で並列に走っている組。

- #4 は除外を推奨する(取得演出の Entity とプレイヤーの Entity は別で、コードからも確認できる)。
- #1 は次のどちらか。
  - **依存(推奨)**: 安全側。代わりに wave が 1 つ増える。ActorStateMachine は AquaDash では空回りなので、増える時間は待ち合わせ 1 回分と見込む(計測の段階で確かめる)。
  - **除外**: level は変わらない。ただし「StateMachine を持つ Entity と SpeedCharacter を持つ Entity は別」という前提は、どこでも保証されていない。

### P3: Debug 限定の宣言漏れ検出(`<Engine>`)

2.6 の型単位の検出を入れる。Release の実行時コストは 0 にする(`_DEBUG` 限定)。

---

## 4. 設計上の判断

- **型単位で判定し、Entity 単位では判定しない。** 同じ型を別の Entity の集合で触る組(1.3 の #4)は、起動時の情報では安全と言い切れない。
  判定を粗くして誤検出を除外で吸収するほうが、見逃すより安全。
- **カメラは 1 つの資源として扱う。** カメラ別に分けると、`cameraType` を実行時に切り替える System(CameraSteering)を宣言で表せない。
- **`Write` は読みを含む。** 読み書きを別々に宣言させると記述が増えるだけで、競合の判定結果は変わらない。
- **`Foreach<const T>` による読み取り専用の強制は、本書では扱わない。** 調査では通る見込みだが、
  `TypeInfo::Create<const T>` が `MoveImpl<const T>` を作るので、ムーブ専用メンバを持つ型でコンパイルが通らない可能性がある。
  入れるなら別の設計にする。
- **既存の依存は消さない。** 検証を通すためだけに依存を減らすことはしない。依存には、読み書きでは表せない順番の意味も入っている
  (「入力の転写 → 走行 → ワールド変換」など)。

---

## 5. 評価チェックリスト

アプリの起動はユーザーの指示があるまで行わない。

### P1

- [ ] Debug / Release(Win32、D3D12)がビルドでき、警告が増えていない
- [ ] 既存の登録で起動でき、ゲームの System 8 個が「未宣言」として起動ログに出る
- [ ] エンジン System 同士の検証でエラーが出ない
- [ ] `SpawnSystem` → `HierarcicalTransformSystem` の依存を一時的に外すと、`HierarchicalTransformComponent` の競合として 2 つの System 名がログに出て、起動が止まる(戻す)
- [ ] 理由が空の除外が登録エラーになる(一時的に書いて戻す)
- [ ] 検証の所要時間を起動ログで確認する

### P2

- [ ] Debug / Release(Win32、D3D12)がビルドでき、警告が増えていない
- [ ] 16 個すべての System が宣言済みで、未宣言のログが出ない
- [ ] 1.3 の 7 件がすべて解消され、検証エラーが出ない
- [ ] 解消後の level が、3 章の見込みどおりに変わっている(起動ログかデバッグ UI の System Graph で確認)
- [ ] ステージを 1 周して、走行・コイン取得・カメラ・サウンドが従来どおり
- [ ] `DeclareAccess` を持たない System を一時的に登録すると、登録エラーで起動が止まる(戻す)

### P3

- [ ] Debug で、宣言していない Component 型を `Foreach` する行を一時的に入れると、アサートで止まる(戻す)
- [ ] Release のビルドに検出のコードが含まれない
- [ ] ステージを 1 周して、誤検出のアサートが出ない

### 文書の更新

- [ ] [04 ECS設計](04_ECS設計.md) §4 と §9 のチェックポイント「並列 System の競合」を本書へのリンクに置き換える
- [ ] [System登録契約設計](System登録契約設計.md) §0 の①の行を本書へのリンクにする
- [ ] 本書の `対象コミット` を更新する
