# aqEngine エディタ E8: Undo/Redo とコンソール

> 対象コミット: d993cda / 最終更新: 2026-11-25(エディタ全体設計.md の旧 9.1 節と 11.3 節の該当フェーズを分割して移した。本文は分割前のまま。各節の照合コミットは節の「現状」に記載)
> 全体の構成・到達点・フェーズ一覧・未決事項は [エディタ全体設計](../エディタ全体設計.md)。節番号(4.4 など)は分割前の番号のまま残している。

## 9.1 E8 の詳細(2026-11-22 追記)

Windows 機で進める。E7 は完了済み。

### 現状(調査結果。a3ed56a で確認)

| 項目 | 実コード |
|---|---|
| コマンド | `Editor/Commands/` に 7 つ(`CreateNode` / `DeleteNode` / `MoveNode` / `RenameNode` / `SetField` / `AddComponent` / `RemoveComponent`)。`Undo` は空。`IEditorCommand::Undo` は `void`。`DeleteNodeCommand` は取り外したサブツリーと元の親・位置を、`MoveNodeCommand` は実行前の親・位置を、`RemoveComponentCommand` は取り外した JSON を `Execute` で控えている。`CreateNodeCommand` は振られた ID を控える |
| 適用 | `EditorContext::ApplyCommands` が積んだ順に `Execute` し、成功が 1 つでもあれば版番号を進めて dirty を立て、プレビューを置き換える。実行したコマンドは捨てる。失敗は `StartupLog` にコマンド名 |
| dirty | `bool dirty_`。コマンドの成功で立ち、New / Open / Save / Save As で下りる |
| SetField の変更前 | インスペクターの写しの `Begin` の値(参照ノードでは「既定値 ← 解決済みの値」)。控えが無いと Null |
| 編集モデルの部品 | `InsertNode` は採番済みの ID を変えない。`MoveNode` の添字は同じ列なら取り外した後の添字。`AddComponent` は末尾に足し、`RemoveComponent` は位置を返さない(7.9 節の E8 への申し送り)。`SetComponentField` は参照ノードを初回に付け替える(7.9 節 8.) |
| ショートカット | `LevelFileMenu` が `ImGui::Shortcut(ImGuiMod_Ctrl \| ImGuiKey_S, ImGuiInputFlags_RouteGlobal)`。ImGui 1.93 の `ImGuiInputFlags_RouteGlobal` は「フォーカスのあるウィンドウや操作中の項目が同じキーを登録していないとき」だけ効く |
| ログ | 2 経路。`EnginePrintf` → `aq::debug::Printf`(`Utility.h` のインライン。**Debug だけ**。Release では何も出ない)→ `aq::debug::OutputString`(Win32 は `OutputDebugStringA`)。`aq::StartupLog` → `StartupMark`(`aq.cpp`、全構成。`[startup] 経過ms (+差分) 本文` に整形して `startup_timing.log` と `OutputDebugStringA` へ。ミューテックスの中で書く)。どちらも外から読む口は無い。ほかに `OutputString` を直接呼ぶ所が 4 ファイル(Vulkan の検証レイヤとシェーダのエラー、`VulkanImGui`、`MemoryTracker`) |
| 起動の順 | `EditorMain.cpp` の `WinMain` が `EditorLayer` を作ってから `StartupMark("WinMain")` → `Engine::Create` → `Initialize` → `RunGame` → `Finalize` → `Engine::Release`。`EditorLayer` は `Engine` より長く生きる |
| 配置 | `EditorLayer::SetupInitialLayout` が、`Hierarchy` の配置の保存が無いときに 1 回だけ左(Hierarchy)・中央(Game View)・右(Inspector)に分ける |

### 決定

E8 を 2 段に分ける。

| 段 | 内容 |
|---|---|
| E8a | Undo / Redo。エンジン: 編集モデルの部品 2 つ(コンポーネントの位置、フィールドの現在値)。Editor: 各コマンドの `Undo`、`CommandHistory`、`EditorContext` の履歴と dirty、Edit メニューとショートカット。ReflectionTest の T13 |
| E8b | コンソール。エンジン: ログの通知の口。Editor: `ConsoleLog`(受け取って溜める)と `ConsolePanel`、Window メニュー、初期配置。ReflectionTest の T14 |

**1. エンジンの編集モデルに足すもの(E8a)**

```cpp
/** 足す位置を指定できるようにする(既定は末尾。範囲外も末尾) */
bool AddComponent(uint32_t id, const std::string& typeName, const util::JsonValue* json = nullptr, size_t index = APPEND);
/** 取り外した位置も返す */
bool RemoveComponent(uint32_t id, const std::string& typeName, util::JsonValue* outJson = nullptr, size_t* outIndex = nullptr);
/**
 * SetComponentField が書き換える前の値(そのキーの JSON)。
 * 実体ノード: 持っているコンポーネントを serializePtr した値の key。
 * 参照ノード: 付け替え済み・新しく足した(loadedJson が空)ものはそのまま、まだなら「既定値 ← R_cur[型]」を serializePtr した値の key。
 * SetComponentField が false を返す条件(型が無い、removedComponents にある、解決できない)では false
 */
bool GetComponentField(uint32_t id, const std::string& typeName, const std::string& key, util::JsonValue& outValue) const;
```

- 既存の呼び出し(Editor のコマンド、T11)は引数の既定値でそのまま動く。
- `GetComponentField` はモデルを変えない(付け替えもしない)。参照ノードで未付け替えのときは `ResolveReferenceComponents` を呼ぶのでファイルを読む(コマンドの実行時に 1 回だけ)。

**2. 各コマンドの Undo / Redo(E8a)**

`IEditorCommand::Undo` を `bool` にする(失敗なら false。モデルは変えない)。Redo は `Execute` をもう一度呼ぶ(取り消しの直後の状態は最初の実行の前と同じなので、同じ引数で同じ結果になる)。

| コマンド | Execute で控えるもの | Undo | Redo(再度の Execute)で気を付けること |
|---|---|---|---|
| `CreateNodeCommand` | 振られた ID と、**実際に置かれた**親・位置(`FindParent`) | `DetachNode(ID)` で取り外し、`node_` に持つ | `node_` があればそれを `InsertNode`(ID は保たれる) |
| `DeleteNodeCommand` | 取り外したサブツリー・元の親・位置(今のまま) | `InsertNode(元の親, 元の位置, サブツリー)`。ID が保たれるので、子孫を対象にした履歴もそのまま使える | 今のまま(`DetachNode`) |
| `MoveNodeCommand` | 実行前の親・位置(今のまま) | `MoveNode(ID, 元の親, 元の位置)` | 今のまま。同じ列の中の移動も、取り外した後の添字の規則で往復が一致する(T13 で確かめる) |
| `RenameNodeCommand` | **実行の時点のモデルの名前**(コンストラクタの `oldName` は使わない) | `SetNodeName(ID, 控えた名前)` | 今のまま |
| `SetFieldCommand` | **実行の時点の `GetComponentField` の値**。インスペクターの `Begin` の控えは使わない(同じフレームに同じキーへ 2 回積まれても、2 回目の変更前が正しくなるように)。コンストラクタから変更前の引数を外す | `SetComponentField(控えた値)` | 今のまま |
| `AddComponentCommand` | なし | `RemoveComponent(ID, 型)` | 今のまま(既定値で末尾。取り消しの直後なので最初と同じ) |
| `RemoveComponentCommand` | 取り外した JSON と**位置** | `AddComponent(ID, 型, JSON, 位置)` | 今のまま |

- **参照ノードの「override に無かったキー」の Undo**: 9 章の表は「変更前にそのキーが override にあったか」を持つとしていたが、**持たない**ことにする。7.9 節 8. の付け替えの後は、保存は「`loadedJson`(= `R_raw` 基準)と実体の差分」なので、変更前の値に戻せば差分が消え、キーは override に書かれない(override にあったキーは、変更前の値が `R_raw` の値と同じなので元のまま書かれる)。付け替えは取り消さない(`rebased` は立ったまま)が、付け替えの前後で保存の結果は変わらない(7.9 節 8.)。T13 で `ToJson` が元と一字一句一致することを確かめる。
- コマンドは選択に触れない。取り消しで消えたノードの選択は、今どおり `ApplyPending` が外す。

**3. 履歴 `CommandHistory`(Editor。E8a)**

```cpp
class CommandHistory
{
public:
	static constexpr size_t MAX_ENTRIES = 256;

	void        Push(std::unique_ptr<IEditorCommand> command);   // 実行に成功したもの。Redo の列を捨てる。上限を超えたら一番古いものを捨てる
	bool        CanUndo() const;
	bool        CanRedo() const;
	const char* GetUndoName() const;   // 無ければ nullptr
	const char* GetRedoName() const;
	bool        Undo(aq::level::LevelEditModel& model);   // 失敗したら履歴を空にして false
	bool        Redo(aq::level::LevelEditModel& model);   // 同上
	void        Clear();               // 保存の印は「どの状態とも一致しない」にする
	void        MarkSaved();           // 今の状態を保存済みとする
	void        MarkCleanEmpty();      // 空にして、空の状態を保存済みとする(New / Open の後)
	bool        IsAtSavedState() const;
	size_t      GetUndoCount() const;
	size_t      GetRedoCount() const;
};
```

- 各項目に通し番号を振る(1 から)。**今の状態の番号**は、Undo の列が空でなければその先頭の項目の番号、空なら**基底の番号**(上限で捨てた項目のうち最後のものの番号。初めは 0)。保存の印は「保存した時点の今の状態の番号」で、dirty は `今の状態の番号 != 保存の印`。保存した後に Undo して新しいコマンドを積むと、保存の時点の項目は Redo の列ごと捨てられ、以後は二度と一致しない(dirty のまま)。上限で一番古い項目を捨てると基底の番号がその項目の番号に進むので、0 の状態(保存済みの空)から 257 回積んで 256 回 Undo しても、今の状態は 1 で保存の印 0 と一致せず、dirty のまま(Codex レビュー 1 回目)。
- Undo / Redo の失敗(モデルとコマンドの食い違い。通常は起きない)は、ログに名前を出して履歴を空にし、保存の印も「一致しない」にする(dirty のまま)。中途半端に続けると以後の取り消しがずれるため。
- ImGui に依らない純粋なロジックにして、T13 から呼べるようにする。

**4. `EditorContext` の変更(E8a)**

- `ApplyCommands` は成功したコマンドを `history_.Push` する(失敗は今どおり捨ててログ)。成功したら `[editor] command: <名前> (undo=N redo=M)` をログに出す(自動操作の確認用)。
- `RequestUndo()` / `RequestRedo()` を足す。要求は次のフレームの `ApplyPending` で、**コマンド → Undo / Redo → ファイル操作**の順に処理する(同じフレームに複数来たら最後のもの)。Edit 以外のモードでは捨てる(`Submit` と同じ)。成功したら版番号を進め、プレビューを置き換え、`[editor] undo: <名前> (undo=N redo=M)` / `redo:` をログに出す。
- `bool dirty_` をやめ、`IsDirty()` は `!history_.IsAtSavedState()` を返す。New / Open は `MarkCleanEmpty()`、Save の成功と、基準ディレクトリが変わらない Save As の成功は `MarkSaved()`(7.9 節 6.「これらはモデル全体を入れ替える操作なので、E8 では履歴を空にする」のうち、Save は**モデルを入れ替えないので履歴を残す**に改める。Unity と同じく保存の後も取り消せる)。
- **基準ディレクトリが変わる Save As の成功は `MarkCleanEmpty()`(履歴を空にする)**。別のフォルダへの保存は「今のモデルに相対参照が無い」ことで許しているが(7.9 節 6.)、履歴の中には相対参照を持つノード(削除したサブツリー、変更前の Prefab の参照)が残りうる。保存の後にそれを取り消すと、相対参照が新しい基準で解決され、別のアセットを指すか解決に失敗するため(Codex レビュー 1 回目)。
- 外から読む口: `CanUndo()` / `CanRedo()` / `GetUndoName()` / `GetRedoName()`(メニュー用)。

**5. Edit メニューとショートカット(E8a)**

- メインメニューバーの `File` の右に `Edit` を足す(`Panels/EditMenu.{h,cpp}`)。項目は `Undo <名前>`(`Ctrl+Z`)と `Redo <名前>`(`Ctrl+Y`)。履歴が無い、Edit 以外のモード、ファイル操作の要求が残っているときは無効。
- ショートカット: `Ctrl+Z` で Undo、`Ctrl+Y` と `Ctrl+Shift+Z` で Redo。`ImGui::Shortcut(..., ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_Repeat)`。**操作中の項目があるとき(`ImGui::IsAnyItemActive()`)とポップアップが開いているときは受けない**。名前の `InputText` の入力中は ImGui 自身の文字の取り消しに任せ、インスペクターのドラッグ中(開いている操作がある)に取り消してモデルと写しが食い違うのを避けるため。
- 呼ぶのはエディタ用コンテキストの `OnBuildUI`(`Ctrl+S` と同じ場所)。ゲームのキー入力とは別のコンテキストなので、Edit 中のゲームへは届かない(E2b の振り分けのまま)。

**6. ログの通知の口(エンジン。E8b)**

```cpp
// Platform/Common/DebugOutput.h に足す(実装は aq.cpp。全プラットフォームで同じ)
namespace aq::debug
{
	/** ログ 1 回分の文字列を受け取る関数。どのスレッドからも呼ばれる。中でログを出してはいけない(StartupMark のロックの中で呼ぶため) */
	using LogListener = void (*)(const char* text);

	/** 受け取る関数を 1 つ登録する(nullptr で外す)。std::atomic で持つ */
	void SetLogListener(LogListener listener);

	/** 登録されていれば渡す */
	void NotifyLogListener(const char* text);
}
```

- 呼ぶ所は 2 つ: `aq::debug::Printf`(`OutputString` の後)と `StartupMark`(整形した 1 行を、ロックの中・プラットフォームごとの出力の前に)。これで `EnginePrintf`(Debug)と `StartupLog` / `StartupMark`(全構成)がコンソールに届く。
- `OutputString` を直接呼ぶ 4 ファイル(Vulkan の検証レイヤなど)は E8 では対象外(コンソールに出ない)。必要になったら `NotifyLogListener` を足す。UWP の `StartupLog` は `StartupMark` を通らないが、Editor は Win32 だけなので対象外。
- 15 章の「コンソールが読むログの出力先」はこれで決定とする(ファイルを読み返すのではなく、出力の時点で受け取る)。

**7. `ConsoleLog` と `ConsolePanel`(Editor。E8b)**

- `ConsoleLog`(`Editor/ConsoleLog.{h,cpp}`。ImGui に依らない): 受け取った文字列を `\n` で行に分け(末尾の空行と `\r` は落とす)、行ごとに通し番号と重要度を付けて、ミューテックスの下で最大 4000 行の環状の列に溜める。重要度は文字列からの推定で、小文字にして `error` / `failed` / `assert` / `[crash]` または `エラー` / `失敗` を含めば Error、`warn` / `警告` を含めば Warning、ほかは Info(エンジンのログに重要度が無いため。推定の誤りは見た目だけの問題)。`Attach()` で `SetLogListener` に自分の静的関数を登録し、`Detach()` で外す。読む側は `CopySince(通し番号, 出力先)` で新しい行だけを写す。`Clear()`。
- 寿命: `WinMain` で `EditorLayer` と同じスコープに置き、**`StartupMark("WinMain")` より前に `Attach`**(エンジンの初期化のログから溜まる)、**`Engine::Release` の後に `Detach`**(ワーカーのスレッドが止まった後)。`EditorLayer` には参照を渡す。
- `ConsolePanel`(`Panels/ConsolePanel.{h,cpp}`): ウィンドウ名 `Console`。毎フレーム `CopySince` で手元の写しに足し(手元も 4000 行で切る)、上に `Clear`・`Auto-scroll`(既定で入)・`Copy`(表示中の行をクリップボードへ)・Info / Warning / Error の表示の切り替え(件数付き)・文字の絞り込み(`ImGuiTextFilter`)。本体は `ImGuiListClipper` で表示する行だけ描く。Warning は黄、Error は赤。どのモードでも動く(Play 中のログも見る)。
- 閉じられるウィンドウにし、メインメニューバーに `Window` メニュー(`Console` の表示の切り替え)を足す。
- 初期配置: `SetupInitialLayout` の新規の分割で、中央を上下に分けて下(高さ 25%)に Console を入れる。**既に `editor_layout.ini` がある利用者**(Hierarchy の配置が保存済み)では分割し直さず、Console の配置の保存が無ければ画面下の中央に浮かせて出す(`SetNextWindowPos` / `SetNextWindowSize` の `ImGuiCond_FirstUseEver`)。利用者がドッキングすれば以後は ini に残る。
- Release では `EnginePrintf` が消えるので、`StartupLog` の行だけが出る(既知の制約。メニューの近くに注記はしない)。

**8. ReflectionTest**

| 試験 | 内容 | 段 | 構成 |
|---|---|---|---|
| T13 Undo / Redo | `Editor/Commands/*.cpp` と `Editor/CommandHistory.cpp` を ReflectionTest に直接コンパイルして使う(下の注)。T10 / T11 の試験用 Level で: (a) 7 つのコマンドそれぞれで「`Execute` → `ToJson` が変わる → `Undo` → 元と一致 → `Redo`(`Execute`) → 実行後と一致」。`RemoveComponent` は先頭・中ほどの位置のコンポーネントで、**ノードの `components` の型の列を直接比べて**並びまで一致する(`ToJson` の `components` は `unordered_map` のオブジェクトなので並びが出ない。Codex レビュー 1 回目)。`MoveNode` は別の親・同じ列の前へ・同じ列の後ろへの 3 通り。(b) 子のフィールドを変える → 親を削除 → 削除を Undo → 子の変更を Undo で、子の値と `ToJson` が元と一致する(子孫の ID が保たれる)。(c) 参照ノードで override に無いキーを変えて Undo すると、`ToJson` が元と一字一句一致する(11.4 節の比較。キーが override に残らない)。override にあるキーでも同じ。(d) 同じキーへの `SetField` を 2 つ続けて実行し、逆順に Undo すると元に戻る(変更前を実行時に取ることの確認)。(e) `CommandHistory`: Push で Redo が消える、上限で古いものが消える、保存の印(保存 → Undo で dirty → Redo で dirty でない → Undo して新しいコマンドで以後ずっと dirty、保存済みの空から 257 回積んで 256 回 Undo しても dirty)、失敗するテスト用コマンドで Undo が false を返して履歴が空になる | E8a | Debug のみ(`LevelEditModel` が `AQ_DEBUG_IMGUI` だけにあるため) |
| T14 ログの通知 | (a) `SetLogListener` で登録した関数に `StartupLog` の 1 行が届く(`[startup]` の整形込み)、`EnginePrintf` は Debug で届き Release では届かない、`nullptr` で外すと届かない。(b) `ConsoleLog`: 改行での分割・末尾の空行と `\r` の除去・重要度の推定(各 2 例以上)・上限 4000 行で古いものから消える・`CopySince` が新しい行だけを返す・`Clear`。(c) 4 スレッドから同時に 1000 行ずつ送り、行数と各スレッドの順序が保たれる | E8b | 全構成 |

- 注: Editor のソースを ReflectionTest に入れるため、`Commands/*.cpp`・`CommandHistory.cpp`・`ConsoleLog.cpp` は `stdafx.h`(Game の `Application/stdafx.h`。中身は `aq.h` と `GameInput.h`)ではなく `aq.h` を直接読む(Editor の .cpp はもともとプリコンパイル済みヘッダを使っていない)。CMake ではコマンドと履歴を `$<$<CONFIG:Debug>:...>` で Debug のときだけ足す。

**9. 11.3 の E8 のチェックリストの確かめ方**

| 項目 | 確かめ方 |
|---|---|
| 5 種の操作をそれぞれ Undo / Redo できる | 正しさは T13(a)。Editor では配線を Claude の自動操作で確かめる: Playground を開き、**比較の基準として Save As で同じフォルダの `E8Check_Base.level.json` に書く**(Save As は dirty でなくても書く)。作成・削除・付け替え・値の確定・コンポーネントの追加と削除・名前の変更をして、それぞれ `Ctrl+Z` → `Ctrl+Y`。各操作の後に `startup_timing.log` の `command:` / `undo:` / `redo:` の行、構造の操作はプレビューの木(`preview tree`)、値とコンポーネントはインスペクターの撮影で戻ることを見る(木のログは名前と親子だけなので、値とコンポーネントの確認には使えない。Codex レビュー 1 回目)。最後に全部 Undo して `E8Check_Undo.level.json` に Save As し、2 つのファイルを Python の `json.loads`(先頭のコメントを除く)で比べて一致すること(同じ書き出しの経路なので既定値の書き足しも揃い、キーの順序は辞書の比較で無視される)。比べた後に 2 つのファイルを消す |
| ドラッグで値を変えた操作が Undo 1 回で戻る | 自動操作で値をドラッグして確定 → `command: Set Field` が 1 行だけ → `Ctrl+Z` 1 回で撮影の値とログの `undo=0` |
| 子の値を編集 → 親を削除 → 削除を Undo → 子の編集を Undo | T13(b)。加えて自動操作で同じ順に行い、インスペクターの値を撮影で確認 |
| 参照ノードの override に無かったキーの Undo | T13(c)。加えて Editor で上と同じ基準と比べる: 参照ノードの値を変えて Undo し、Save As した `E8Check_Undo.level.json` が基準と一致する(Save は保存済みの状態では書かないので使わない。Codex レビュー 1 回目) |
| Play 中の編集が履歴に残らない | 自動操作: Edit で 1 つ操作(`undo=1`)→ Play → インスペクターで値を変える → Stop → Edit メニューの Undo の名前が Play の前の操作のまま、`Ctrl+Z` のログが `undo=0` になる。Play 中は Edit メニューが無効で `Ctrl+Z` がログを出さない |
| コンソールにログが出る | T14。自動操作で Editor の起動直後に Console へエンジンの初期化の行(`[startup]`)が出ていることを撮影、Open / Save の `[editor]` の行が増える、存在しないパスを Open して赤の行が出る。Clear・絞り込み・Auto-scroll はユーザー確認 |

### E8a の結果(2026-11-23)

確認の結果は 11.3 節。実装で決めたこと(設計に書いていなかったもの):

- `RenameNodeCommand` のコンストラクタの変更前の名前の引数は残し、使わない(`Execute` でモデルから取り直す)。設計で引数を外すとしたのは `SetFieldCommand` だけのため。
- `GetComponentField` はキーが無いときも false。`R_raw` の解決は見ないので、`R_raw` だけが解決に失敗する場合は Get が通って Set が false になる(どちらでもコマンドは失敗)。
- `CreateNodeCommand::Undo` は、控えた「実際に置かれた親・位置」と今の場所が食い違えば取り外さずに false。
- Undo / Redo の失敗は `[editor] undo failed: <名前> (... history cleared)` を出し、版番号もプレビューも進めない。`CommandHistory::Clear` は基底の番号を変えず保存の印だけを「一致しない」にする。Redo は最初に積んだときの通し番号を保つ。
- Edit メニュー自体はどのモードでも開け、項目だけを無効にする。項目名は `Undo <名前>###Undo` で ID を固定。ImGui 1.93 の `Shortcut` は修飾キーの完全一致で判定するので、`Ctrl+Shift+Z` で `Ctrl+Z` は発火しない。3 つの `Shortcut` は経路の登録のために毎フレーム呼ぶ。
- 自動操作で分かったこと: メニューを `Esc` で閉じた直後のフレームの `Ctrl+Z` は、ポップアップが開いている扱いで受けない(設計どおり。続けて押せば効く)。Play 中の Scene Hierarchy の最初のクリックはウィンドウのフォーカスだけで選択にならないことがあった(E7c の既存の挙動。2 回目で選べる)。

### E8b の結果(2026-11-23)

実装で決めたこと(設計に書いていなかったもの):

- `ConsoleLog` の登録先のインスタンスは、関数内の静的なミューテックスで守った生ポインタで持ち、ログを渡す間もそのロックを持つ(`Detach` から戻った後に外したインスタンスへ渡らないように。atomic だけでは避けられない)。ロックの順は `StartupMark` のロック → 登録のロック → 行の列のロックの一方向。デストラクタでも `Detach` する。
- `` は行末に限らずすべて落とす。改行の無い `EnginePrintf` を続けて呼ぶと、呼び出しごとに別の行になる(連結しない)。
- 件数は文字の絞り込みに依らず、手元の写しの全行の重要度別の数。Clear は `ConsoleLog` と手元の写しの両方を消す(通し番号は戻さない)。閉じている間も写しは取り続ける。
- 浮かせて出すときの大きさは作業領域の幅 60%・高さ 25%、下端の中央。`EditorLayer` はコンストラクタで `ConsoleLog&` を受け取る。
- T14(c) の 4 × 1000 行は上限の 4000 行とちょうど同じ(`static_assert` で超えないことを確かめる)。試験の間にほかのログが出ると古い行が押し出されて失敗しうるが、試験の間にログを出す処理は無い。T14(a) の `StartupLog` で、ReflectionTest のカレントディレクトリに `startup_timing.log` が作られる。

### このフェーズで作らないもの

- 複合コマンド(9 章の表の「複合」)。E8 の操作はどれも 1 つのコマンドで済む(ドラッグは確定の 1 回だけ)。複数選択・複製(E11 以降)で必要になったら足す。
- 履歴の一覧ウィンドウ(任意の時点へ戻る)。
- 取り消しで消えたノードを選び直すこと・取り消しで戻ったノードを選ぶこと。
- コンソールからソースの位置へのジャンプ(13 章)、ログの重要度をエンジンに持たせること、コンソールの入力欄(コマンドの実行)。
- `OutputString` を直接呼ぶ所の通知(6.)。

### テスト方針

| 段 | 自動で確認する(Claude) | ユーザーに確認する |
|---|---|---|
| E8a | ビルド(Game の D3D11 / D3D12 / Vulkan × Debug / Release、Editor の 2 構成。CMake の ReflectionTest は windows-vs2026 の Debug / Release)、警告が基準から増えない。T1〜T13 が PASS(T12 までの件数は変わらない)。自己テスト(`-editor-selftest`)が Debug / Release で PASS。上の 9. の自動操作 | Undo / Redo の操作感(ショートカット、メニューの名前、入力中の `Ctrl+Z` が文字の取り消しになること) |
| E8b | ビルド・警告。T14 が全構成で PASS。自己テスト PASS。上の 9. のコンソールの自動操作 | コンソールの見た目と操作感(Clear・絞り込み・Auto-scroll・Copy、Play 中のログ) |

### 足すファイル(`vs-project-files` に従って登録する)

| ファイル | フィルター(案) | 段 |
|---|---|---|
| `Editor/CommandHistory.{h,cpp}` | (なし。`EditorContext` と同じ直下) | E8a |
| `Editor/Panels/EditMenu.{h,cpp}` | `Panels` | E8a |
| `Editor/ConsoleLog.{h,cpp}` | (なし。直下) | E8b |
| `Editor/Panels/ConsolePanel.{h,cpp}` | `Panels` | E8b |

---

## 評価チェックリスト(旧 11.3 節)

### E8 Undo/Redo・コンソール

確かめ方は 9.1 節 9.。

- [x] フィールドの変更、コンポーネントの追加と削除、ノードの作成と削除、親の付け替えを、それぞれ Undo / Redo できる (E8a。T13(a)。自動操作で Playground に作成・付け替え・値のドラッグ・名前・コンポーネントの追加と削除をしてそれぞれ `Ctrl+Z` → `Ctrl+Y`、ログ・木・インスペクターの撮影で往復を確認。全部 Undo して Save As したファイルが基準の Save As と `json.loads` で一致。作成の Undo で `*` が消える)
- [x] ドラッグで値を変えた操作が Undo 1 回で戻る (E8a。position x を 0.000 → 0.800 にドラッグして `command: Set Field` が 1 行、`Ctrl+Z` 1 回で 0.000)
- [x] 「子の値を編集 → 親を削除 → 削除を Undo → 子の編集を Undo」で、子の値が編集前に戻る (E8a。T13(b) と自動操作: FX_Test の子の x = 0.8 → FX_Test を削除 → Undo で子が x = 0.8 のまま戻る → Undo で 0.000)
- [x] 参照ノードで、override になかったキーを編集して Undo すると、そのキーが override から消える (E8a。T13(c) と自動操作: T6_Overrides の写しで RefPlain の Spawner.interval を変えて Save As すると `overrides.components.Spawner.interval` が出て、Undo 後の Save As は基準と一致)
- [x] Play 中の編集が履歴に残らない (E8a。Play 中は Edit メニューが無効で `Ctrl+Z` がログを出さない。Play 中に Session の x を 0.6 にして Stop → Edit メニューは Play 前の `Undo Remove Component` のまま、`Ctrl+Z` で `undo: Remove Component`)
- [x] Undo / Redo の操作感(ショートカット、メニューの名前、入力中の `Ctrl+Z` が文字の取り消しになること)(E8a のユーザー確認。入力中の `Ctrl+Z` で履歴が動かないことは自動操作で確認済み) (2026-11-23 ユーザー確認)
- [x] コンソールにログが出る (E8b。T14 が全構成で PASS。自動操作で、既存の配置では Console が画面下の中央に浮いて出る、起動の `[startup]` と `EnginePrintf`(`[LoadProf]` など)の行が出る、存在しないパスの Open で `Error (1)` になり Info を外すと赤の `level open failed` の行だけが出ることを撮影で確認。Clear・絞り込み・Auto-scroll・Copy・Window メニュー・Play 中の表示はユーザー確認(2026-11-23))
