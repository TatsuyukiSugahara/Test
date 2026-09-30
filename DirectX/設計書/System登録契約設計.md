# System 登録契約設計

> 対象コミット: 34aa75c(P1)+ P2 のコミット / 最終更新: 2026-09-30 / **P1・P2 完了**

`SystemManager` の登録ルール(循環なし・依存先は登録済み・確定後は登録しない・確定前は更新しない)を、
**Release ビルドでも強制する**ための設計。現状はすべて `EngineAssertMsg` 頼みで、
Release ではアサートが `((void)0)` になり([Utility.h:23](../aqEngine/Utility.h#L23))、違反が黙って通る。

この文書は System 登録契約の一次資料とする。ECS 全体の構造は [04 ECS設計](04_ECS設計.md)、
wave 並列の実行モデルは [05 マルチスレッド設計](05_マルチスレッド設計.md) §3 を参照。

---

## 0. 位置づけ

System 並列化の改善は次の順で進める。本書は **③** だけを扱う。

| 順 | 内容 | 本書 |
| --- | --- | --- |
| ③ | 登録契約を Release でも強制する(本書 P1)+ 型照合を完全一致にそろえる(本書 P2) | ○ |
| ① | System の Component 読み書き宣言と、競合する System 間の依存経路の検証 | 別途設計 |
| — | wave ごとの待ち時間・ワーカー稼働状況の計測 | 別途 |
| ② | wave 単位の待ち合わせをやめ、依存が解けた System から実行する方式への移行 | 別途設計 |

③を先に行うのは、①の検証結果を「初期化失敗」として止める経路が③で初めてできるため。

---

## 1. 現状の問題

[System.h](../aqEngine/ECS/System.h) / [System.cpp](../aqEngine/ECS/System.cpp) を 6f2cc9c で確認した結果。

| 違反 | Debug | Release の現状 |
| --- | --- | --- |
| 循環依存 | assert | 循環に含まれる System とその後段が `updateOrder_` に入らず、**黙って実行されない**。確定状態にはなる([System.cpp:92-104](../aqEngine/ECS/System.cpp#L92-L104)) |
| 未登録の型への依存 | assert | 依存指定が捨てられる。失敗の記録も残らない([System.h:129-133](../aqEngine/ECS/System.h#L129-L133)) |
| 自己依存(同じ型) | assert | `if constexpr` で捨てられる([System.h:126-127](../aqEngine/ECS/System.h#L126-L127)) |
| 別の型が同じ登録 System を指す依存 | 循環として assert | 同上(循環として黙って実行されない)。`dynamic_cast` 照合で派生型を拾うと起きる |
| 確定後の `AddSystem`(新しい型) | assert | 登録されるが `updateOrder_` に入らず、**スケジュールを作り直さない限り実行されない** |
| 確定後の `AddDependency`(既存の System 間) | assert | System は古いスケジュールで実行され続け、**新しい依存が反映されない**。順序が守られず競合につながる |
| 確定後の `AddSystem`(登録済みの型) | assert | 既存 System が返る。同時に渡した依存は上と同じく反映されない |
| 確定前の `Update()` | assert | `updateOrder_` が空なので何も実行せず素通り |

なお [使いやすさ改善設計.md](使いやすさ改善設計.md) P2-C の「未登録の依存先も `AddDependency` が assert する。
順序ミスの検出は既に足りている」は **Debug ビルドに限った話**である。本書 P1 の完了時に同節へ注記する。

---

## 2. 方針

違反を 2 種類に分け、扱いを変える。

| 種類 | 対象 | 扱い |
| --- | --- | --- |
| **登録中の失敗** | 循環依存、未登録の型への依存、同じ System を指す依存 | `SystemManager` にエラーを**溜める**。`FinalizeRegistration()` でまとめて false を返し、`Engine::Initialize()` まで初期化失敗として伝える |
| **呼び出し順の違反** | 確定後の登録、確定前の `Update()` | Release でも**その場で拒否**し、ログを出す。状態は変えない |
| **コンパイル時に分かるもの** | 自己依存(`TSystem` と `TDependency` が同じ型) | `static_assert` で拒否する |

### 2.1 エラーを溜める理由

`OnRegister()` は void で、ゲーム側は 10 行以上の登録呼び出しを並べる
([Game/Application/Application.cpp:203-240](../Game/Application/Application.cpp#L203-L240))。
各 API から失敗を返す方式では、呼び出し側すべてに確認処理が要る。
エラーを溜めて確定時にまとめて返す方式なら、ゲーム側のコードは変わらない。

溜めたエラーは確定時に**すべて**ログへ出す。1 回の起動で複数の誤りをまとめて直せるようにするため。

### 2.2 失敗の伝わる経路

```
SystemManager::BuildSchedule()          bool  (エラーが 1 件でもあれば false)
  └ EntityContext::FinalizeRegistration()  bool
      └ Application::Register()             bool  (IApplication::Register を bool へ)
          └ Engine::Initialize()              既存の「StartupLog を出して return false」に合わせる
```

`Engine::Initialize()` は失敗時に `aq::StartupLog("  [engine] ... FAILED")` を出して false を返す方式が既にある
([Engine.cpp:86-116](../aqEngine/Engine.cpp#L86-L116))。登録失敗もこれにそろえる。
汎用の致命的エラーマクロは新設しない。

### 2.3 失敗後の動き(確認済み)

- Win32 / Mac / Android / UWP の起動処理は、初期化失敗で `RunGame()` を飛ばし `Finalize()` を呼ぶ
  ([Main.cpp:30-33](../Game/Application/Main.cpp#L30-L33)、[MacMain.mm:123-126](../Game/Application/MacMain.mm#L123-L126)、
  [AndroidMain.cpp:60-63](../Game/Application/AndroidMain.cpp#L60-L63)、[UWPMain.cpp:74-84](../Game/Application/UWPMain.cpp#L74-L84))。
  したがって登録に失敗した状態で更新ループへは入らない。
- 登録は `application_->Initialize()` の後に呼ばれる([Engine.cpp:107-117](../aqEngine/Engine.cpp#L107-L117))。
  失敗した時点でアプリの初期化は完了しているので、後始末は通常の `Application::Finalize()` 経路で済む。
- iOS は `Finalize()` を `applicationWillTerminate:` に任せており、初期化失敗時はフレームが回らないまま止まる
  ([iOSMain.mm:74-81](../Game/Application/iOSMain.mm#L74-L81))。**これは他の初期化失敗と同じ既存の動き**で、本書では扱わない。

### 2.4 ログの出し先

- 登録失敗の詳細と呼び出し順の違反は `aq::StartupLog()` へ出す。
  Win32 / Mac / Android では `StartupMark` 経由で `startup_timing.log` と `OutputDebugString` 等へ、
  UWP では `LocalState/startup.log` へ出る。Debug / Release のどちらでも有効([aq.h:152-164](../aqEngine/aq.h#L152-L164))。
- Debug では従来どおり、ログを出した後にアサートで止める(開発中はその場で気付けるように)。

---

## 3. 責務表

### P1: 登録契約の強制

| ファイル | 変更内容 |
| --- | --- |
| [ECS/System.h](../aqEngine/ECS/System.h) | 登録状態を `bool registrationFinalized_` から 3 状態(登録中 / 確定済み / 確定失敗)へ。登録エラー一覧 `std::vector<std::string>` を追加。`AddDependency` に自己依存の `static_assert` を追加し、既存の `if constexpr` による握りつぶしを削除。未登録の型・`sysIdx == depIdx` をエラー一覧へ記録。確定後(確定済み・確定失敗の両方)の `AddSystem` / `AddDependency` はログを出して何もしない。`AddSystem` はこのとき nullptr を返す(現状、戻り値を使う呼び出しはない) |
| [ECS/System.cpp](../aqEngine/ECS/System.cpp) | `BuildSchedule()` を bool 戻りに。トポロジカルソートで並べられなかった System 名をすべてエラーに記録する(循環に含まれるものと、その後段を区別しない)。エラーが 1 件でもあれば全件を `StartupLog` へ出し、確定失敗状態にして false。成功時だけ確定済みにする。`Update()` は確定済み以外なら**最初の 1 回だけ**ログを出して何もせず戻る(毎フレームのログ連打を避ける) |
| [ECS/EntityContext.h](../aqEngine/ECS/EntityContext.h) | `FinalizeRegistration()` を `[[nodiscard]] bool` に |
| [Core/IApplication.h](../aqEngine/Core/IApplication.h) | `Register()` を `bool` に |
| [Core/Application.h](../aqEngine/Core/Application.h) / [.cpp](../aqEngine/Core/Application.cpp) | `Register()` が `FinalizeRegistration()` の結果を返す。ゲーム側の `OnRegister()` は void のまま |
| [Engine.cpp](../aqEngine/Engine.cpp) | `Register()` が false なら `StartupLog("  [engine] application_->Register FAILED")` を出して false |

エラー文には System の型名(`typeid(T).name()`。既存の `displayName` と同じ)を入れる。例:

```
[ecs] AddDependency: dependency is not registered: class app::ecs::CoinSystem -> class app::ecs::FooSystem
[ecs] BuildSchedule: not scheduled (circular dependency or downstream of one): class app::ecs::A
```

### P2: 型照合を完全一致へ(P1 と別コミット)

| ファイル | 変更内容 |
| --- | --- |
| [ECS/System.h](../aqEngine/ECS/System.h) | `SystemEntry` に登録時の `std::type_index` を保持する。`FindIndex` / `HasSystem` / `GetSystem` / `AddSystem` の重複判定を、`dynamic_cast` から保持した `type_index` との完全一致へ統一する。`GetSystem` は一致した要素を `static_cast` で返す |

- 6f2cc9c 時点で `GetSystem` / `HasSystem` を呼んでいるのは `SystemManager` の中と `EntityContext` の中継だけで、
  **基底型で System を取得している箇所はない**(`DirectX/` 以下を検索して確認)。
- P2 の後は同じ型を 2 回登録できないので、P1 の `sysIdx == depIdx` 検査は通常は発生しなくなる。
  それでも防御として残す。

---

## 4. 設計上の判断

- **確定失敗後も登録を拒否する。** 確定失敗後に登録を受け付けると、再確定の手順が必要になる。
  現状、確定をやり直す使い方はないので、状態は一方向(登録中 → 確定済み / 確定失敗)に限る。
- **確定前の `Update()` はエラー扱いにしない。** 登録失敗時は更新ループに入らないため(2.3)、
  ここへ来るのはプログラムの誤りだけ。初期化結果には含めず、呼び出し順の違反として扱う。
- **循環の構成要素と後段を区別しない。** 区別には強連結成分の計算が要る。
  System 数が少ない現状では名前の一覧で原因を追えるので、まず区別しない形で入れる。
  足りなければ後で足す。
- **エラーは例外で投げない。** `SystemManager::Update()` の例外は System の実行エラー用で、
  初期化の失敗とは経路を分ける。

### 4.1 実装で決めた細部(P1)

- `TSystem` と `TDependency` がどちらも未登録なら、エラーを 2 件記録する(`system is not registered` / `dependency is not registered`)。
- 同じ System に解決された場合の文言は `both types resolve to the same system`。
- `Update()` の Debug アサートは、ログと同じく最初の 1 回だけ。
- 確定失敗状態では `DebugRenderMenuAll()` / `DebugRenderAll()` も何もしない(通常は起動が止まるので呼ばれない)。
- `BuildSchedule()` の 2 回目の呼び出しは拒否していない。呼び出しは `Application::Register()` の 1 か所だけなので、現状は起きない。

---

## 5. 評価チェックリスト

アプリの起動はユーザーの指示があるまで行わない。ビルドで確認できる項目を先に埋める。

### P1

- [x] Debug / Release(Win32、D3D12)がビルドでき、警告が増えていない(2026-09-30。Release もリンクまで通る。警告 52 件はすべて既存の Bullet ヘッダ等で、変更ファイル由来は 0 件)
- [x] 自己依存 `AddDependency<X, X>()` がコンパイルエラーになる(2026-09-30。`CoinSystem` で一時的に書き、`error C2338` を確認して戻した)
- [x] 既存の登録で起動し、System がすべて従来どおり動く(2026-09-30。Debug / Release とも起動〜終了コード 0。Release でタイトルからステージへ入り、走行・タイマー・ミニマップ・カメラ追従を目視)
- [x] Release で未登録の型への依存を一時的に入れると、ログに型名が出て起動が止まり、正常に終了する(`dependency is not registered: ...CharacterSteeringSystem -> ...UnregisteredTestSystem` → `[engine] application_->Register FAILED` → 自力で終了)
- [x] Release で循環依存を一時的に入れると、関係する System 名がログに出て起動が止まり、正常に終了する(PlayerInput → Speed → Coin → PlayerInput の循環で確認)
- [x] 複数の誤りを同時に入れると、1 回の起動で全件がログに出る(未登録 1 件 + 循環由来 12 件の計 13 行)
- [x] Debug では上記の失敗時にアサートで止まる(13 行をログに出した後、CRT のアサートダイアログで停止)
- [x] 一時的に入れた誤りを戻し、差分に残っていない

**評価で分かったこと**

- 循環の本体は 3 つなのに、後段を含めて 12 個の System 名が並び、どれが循環か読み取りにくい。
  §4 で「区別しない」とした判断の代償が思ったより大きい。循環を構成する System だけを先に示す改善を後続の候補にする。
- 登録失敗時も**プロセスの終了コードは 0**。起動処理(`Main.cpp` 等)が初期化の成否に関係なく 0 を返すためで、本書の範囲外。

### P2

- [x] Debug / Release(Win32、D3D12)がビルドでき、警告が増えていない(2026-09-30。52 件のまま)
- [x] 既存の登録で起動し、System がすべて従来どおり動く(2026-09-30。Debug は起動〜終了コード 0、Release はステージで走行まで目視)
- [x] `dynamic_cast` が `System.h` から消えている(`ECS/` 全体で 0 件)

### 文書の更新(P1 / P2 の完了時)

- [x] [04 ECS設計](04_ECS設計.md) §4 の `System.h` / `System.cpp` の説明(「循環依存を assert 検出」「`GetSystem<T>`(dynamic_cast)」)を本書へのリンクに置き換える
- [x] [05 マルチスレッド設計](05_マルチスレッド設計.md) §3 の「循環依存は assert」を同様に直す
- [x] [使いやすさ改善設計](使いやすさ改善設計.md) P2-C の「順序ミスの検出は既に足りている」に、Debug 限定だった旨と本書へのリンクを注記する
- [x] 本書の `対象コミット` を更新する
