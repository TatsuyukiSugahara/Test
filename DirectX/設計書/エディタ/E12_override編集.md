# aqEngine エディタ E12: Prefab 参照ノードの override の編集

> 対象コミット: ff139ec / 最終更新: 2026-11-27(新規。詳細設計)
> 全体の構成・到達点・フェーズ一覧は [エディタ全体設計](../エディタ全体設計.md)。override の形式と解決の正本は [Prefab設計](../Prefab設計.md) と [Level設計](../Level設計.md) 16 章(R2)。本書はエディタ(Editor)の操作だけを書く。

E11 まで実装済み(E9〜E11 の起動・確認は後回し。ユーザー決定)。Mac 機で設計した。もとの計画の E12(override の編集 UI・アセット GUID・Python)は大きさが違うので、**override の編集を E12、アセット GUID を E13、Python を E14 に分ける**(2026-11-27。まとめのフェーズ一覧を直す)。

## 現状(調査結果。ff139ec で確認)

| 項目 | 実コード |
|---|---|
| override の形式 | `{ name, prefab, overrides: { name, components, addedComponents, removedComponents, children, removedChildren } }`。解決は `PrefabSerializer::ResolveNode`: 参照先を再帰で展開し(循環の検出、深さ 32 まで)、外側の `name` を優先し、`ApplyPatch` で override を当てる。`components` は `JsonValue::Merge`(オブジェクトは再帰、配列とスカラーは値ごと置き換え)、`addedComponents` は型ごと置き換え、`removedComponents` は型を消す(同じ patch の `components` / `addedComponents` の同じ型も無効にする)。`children` は**名前で**対応を取り、同名があればその子への patch、無ければ新しい子として末尾に足す。`removedChildren` は名前で消す(同名の兄弟はまとめて消える) |
| 名前の対応の落とし穴 | 同名の兄弟があると、patch は先頭の 1 件しか見ずに同名のすべてに当たり、`removedChildren` は同名をすべて消す。`children` に基準と同じ名前を書くと「追加」ではなく「patch」になる。`removedChildren` にある名前の `children` の項目は無視される(消して差し替えはできない)。**既存の子への patch** の `prefab` は無視されるが、基準に無い名前の項目(override で足した子)は `ResolveNode` に渡るので、`prefab` を持てば参照として展開され、解決後の名前は項目の外側の `name` になる。Prefab の根は実体でも自分の `overrides` を当てる |
| 編集モデル | `PrefabEditNode` は参照ノードで `prefabRef`・override 用の `components`・`removedComponents`・`overridesRaw`(読み込んだ overrides の生 JSON)を持つ。`PrefabNodeFromJson` は `overrides.children` / `removedChildren` を編集ノードへ読まず `overridesRaw` に残すだけ。`PrefabNodeToJson`(`BuildReferenceOverrides`)は `overridesRaw` を写して始め、変わった所だけを書き換える(読めない型・子の override も往復で保つ。E4a) |
| `LevelEditModel` の参照ノード | `ResolveReferenceComponents`(解決済みのコンポーネント)、`SetComponentField` / `GetComponentField`(初回の操作で基準を解決済みの値へ付け替える。E7b)。`AddComponent` / `RemoveComponent` は参照ノードを拒む。`InsertNode` は参照ノードの下を拒む。override 適用前の基準(参照先だけを解決した値)を返す口は無い(私的な `MakeRawReferenceJson` / `ResolveComponentsOf` はある) |
| インスペクター | 参照ノードは解決済みの値の写しを描き、確定で `SetFieldCommand`。Add / Remove Component を出さない。`removedComponents` で消えた型は解決結果に無いので出ず、戻せない。override されているかの印も、元に戻すボタンも無い。写しは `EditorContext::GetVersion` が変わると作り直す |
| ヒエラルキー | 参照ノードの子(参照先の子孫)は編集ノードに無いので出ない。名前に ` [prefab]` の印が付くだけ |
| 値の元に戻し | 今は「基準と同じ値を入れる」しかできず、基準と同じ値の override が明示的に残る。override のキーを消す操作は無い(`PrefabEditNodeOps.cpp` の無名名前空間の `EraseKey` などはある) |
| ファイルの書き込み | `JsonSerializer::WriteFile(path, json, leadingComments)`。原子的(一時ファイル → rename)に書くのは `LevelEditModel::SaveFile` だけ。`JsonParser::ParseFile` / `ReadLeadingComments` は `ResolveExistingResourcePath`(`Resource/Resource.h`)で実際のファイルを探してから読むが、`WriteFile` は渡されたパスへそのまま書く |
| 参照ノードの値の編集と Undo | Editor の `SetFieldCommand` は参照ノードで `SetComponentField` の付け替え(`loadedJson` / `rebased`)に頼り、Undo は「変更前の値を入れる」だけで差分が消える(E8)。この状態はモデルの中にしか無く、override の JSON を読み直すと失われる。E11c のギズモも参照ノードの Transform に `SetFieldCommand` を使う |
| 履歴 | `CommandHistory::Clear()` は履歴を空にし、保存の印を「どの状態とも一致しない」にする(dirty になる) |
| 既存の v1 の UI | Game.exe の Level / Prefab エディタの `PrefabNodeDrawInspector`(Level設計 16 章 R2 v1)は付け替えをせず、参照ノードの Add Component で全フィールドを書く。Editor とは別の経路で、E7 から「Game.exe の挙動は変えない」としている |

## 決定

E12 を 3 段に分ける。**E12a: 根のコンポーネントの override**(印・元に戻す・コンポーネントの追加と削除と復元)→ **E12b: 子の override**(参照先の子の表示・子の値・子の追加と削除と復元)→ **E12c: Prefab への適用(Apply)**。

### 1. 仕組み: override の JSON を丸ごと差し替える(E12a。エンジン)

**Editor では、参照ノードに対する操作を値の編集も含めてすべて「override の JSON を変換して丸ごと差し替える」形に統一する**。参照ノードには `SetFieldCommand`(付け替えの経路)を使わない。付け替えの状態(`loadedJson` / `rebased`)はモデルの中にしか無く、JSON の差し替えと混ぜると Undo の後に元は無かった override が残る(Codex レビュー 1 回目)。JSON だけで表せば、Undo は常に操作の前の JSON をそのまま戻すだけになり、Prefab の中身が変わっても失敗しない。実体ノードは今までどおり `SetFieldCommand`。

- `LevelEditModel` に 2 つの口を足す(`AQ_DEBUG_IMGUI` の中):
  - `bool GetReferenceOverrides(uint32_t id, util::JsonValue& out) const`: 参照ノードの今の override(`PrefabNodeToJson(node)` の `overrides`。無ければ空のオブジェクト)。参照ノードでなければ false。
  - `bool SetReferenceOverrides(uint32_t id, const util::JsonValue& overrides)`: 参照ノードの override の状態を、`{ name, prefab, overrides }` を `PrefabNodeFromJson` で読み直した結果に置き換える(`overridesRaw`・override 用の `components`・`removedComponents` を入れ替え、付け替えの印は外れる。`editId` と `name` と `prefabRef` は変えない)。参照ノードでなければ false でモデルは変えない。
  - 読み直しは往復で値が変わらない(E4a の T6 で確かめている性質)ので、`Get` → 何もしない → `Set` でモデルの `ToJson` は変わらない(T17 で確かめる)。
- 基準の口: `bool ResolveReferenceBase(uint32_t id, util::JsonValue& outNode, std::string* error = nullptr) const`: **この参照ノードの override を当てる前**の解決結果(`{ name, prefab }` だけを `PrefabSerializer::FromJson` で解決した根のノード。components と children を持つ canonical な JSON)。参照先がさらに参照を持つ(入れ子)ときは、その中の override は当たった後のもの(= その Prefab が差し出す姿)。ファイルを読むので毎フレームは呼ばない。
- Editor に、汎用のコマンド `SetOverridesCommand(nodeId, transform, name)`(`Editor/Commands/OverrideCommands.h/.cpp`、新規)を足す。**`transform` は override の JSON を書き換える関数**(値を写して持つ。`bool(JsonValue&)`)で、`Execute` のたびに今の JSON を `GetReferenceOverrides` で取り(= 取り消し用の `before`)、その写しに `transform` を当てたものを `SetReferenceOverrides` する。変換を実行の時点の JSON に当てるので、同じフレームに先に積まれたコマンドの結果を古い値で上書きしない(Codex レビュー 1 回目)。`transform` が false(対象が無いなど)ならモデルを変えずに false。`Undo` は `SetReferenceOverrides(before)`。名前は操作ごと(`Set Field` / `Revert Override` / `Add Component` / `Remove Component` / `Restore Component` / `Add Child` など)。
- **参照ノードの値の編集**: インスペクターの確定と、E11c のギズモの参照ノードの確定を、`SetOverridesCommand`(変換 `OverrideEdit::SetField(type, key, value)`。2 つ以上のキーは 1 つの変換にまとめる)に変える。書く場所の規則: その型が `addedComponents` にあれば `addedComponents.<型>.<キー>`(型ごとの置き換えなので `components` に書いても効かない)、無ければ `components.<型>.<キー>`。子の項目でも同じ規則(6.)。ギズモのドラッグ開始の値と Transform の有無は、`GetComponentField` ではなく解決済みの値(`ResolveReferenceComponents`)から取る。`LevelEditModel::SetComponentField` / `GetComponentField` の参照ノードの経路はエンジンに残す(ReflectionTest が使う。Editor からは呼ばなくなる)。
- override の JSON の変換は、ImGui にもモデルにも依らない純粋な関数の集まり `editor::OverrideEdit`(`Editor/OverrideEdit.h/.cpp`、新規)に置き、T17 で単体で試す。

### 2. override の印(E12a)

- インスペクターの写しを作るとき(版番号が変わったとき)に、`GetReferenceOverrides` で override の JSON を取り、フィールドごとに「override されているか」を決める: `components.<型>.<キー>` があるか、`addedComponents.<型>` があれば(型ごと置き換え)その型のすべてのキー。
- 判定は「保存される override に書かれているか」で行い、値が基準と同じかどうかでは決めない(基準と同じ値の override も「override されている」と見える。消すには元に戻す)。
- 表示: override されているフィールドの左に細い色の帯(青)を描き、コンポーネントの見出しに `(overridden)` を付ける。override だけにある型(基準に無く、足した型)の見出しには `(added)` を付ける。帯はフィールドの矩形(`ImGuiEditEventVisitor::itemRects` と同じ記録の仕組みを使う)に合わせる。
- フィールドのキーは `Reflect` の永続キー(トップレベル)単位。入れ子のオブジェクトの中のキー単位の印は付けない(今の Reflect の永続キーはトップレベルだけ。T17 で今の登録済みの型に入れ子のキーが無いことを確かめる)。

### 3. 元に戻す(E12a)

| 操作(右クリックのメニュー) | 変換 |
|---|---|
| フィールドの `Revert` | `components.<型>.<キー>` を消す。その型のキーが空になったら `components.<型>` も消す。`addedComponents.<型>` のときは型ごとの置き換えなので、フィールド単位では出さない(コンポーネント単位だけ) |
| コンポーネントの `Revert` | `components.<型>` と `addedComponents.<型>` を消す。基準に無い型(足した型)なら、それで型が消える(= 追加の取り消し) |
| 参照ノードの `Revert All`(見出しの右クリック) | override の `components` / `addedComponents` / `removedComponents` を消す。`name`・`children`・`removedChildren` は残す(子は E12b で個別に戻す) |

- どれも `SetOverridesCommand` で、Undo で元の override に戻る。消した後の解決結果は基準の値になる。

### 4. コンポーネントの追加・削除・復元(E12a)

- **Add Component**(参照ノードにも出す): 候補は「解決結果に無い、`addable` で `serializePtr` / `deserializePtr` を持つ型」(実体ノードの `CanAdd` と同じ条件)。追加は `components.<型>` に、その型の既定値を `serializePtr` した全フィールドを書く(基準に無い型なので Merge でも置き換えでも同じ。今の `BuildReferenceOverrides` が新しく足した型を書くのと同じ形)。`removedComponents` にある型は候補に出さない(先に復元する。同じ patch で消して作り直すことはできないため)。
- **Remove Component**: 基準にある型なら `removedComponents` に足し、`components.<型>` / `addedComponents.<型>` を消す。基準に無い(足した)型なら `components.<型>` / `addedComponents.<型>` を消すだけ。`addable == false`(Transform)は出さない(実体ノードと同じ)。
- **Restore**: インスペクターの末尾に「Removed components」の折りたたみを出し、`removedComponents` の型ごとに `Restore` ボタン。押すと `removedComponents` から外す(空になったら キーごと消す)。
- 実体ノードの Add / Remove Component(E7 の `AddComponentCommand` / `RemoveComponentCommand`)は変えない。

### 5. 子の表示と選択(E12b)

- 参照ノードの子(参照先の子孫を override を当てて解決したもの)を、ヒエラルキーで参照ノードの下に**仮想の子**として出す。字は灰色、ドラッグできない、Add Child は出さない(6. のメニューで足す)。
- 仮想の子は「参照ノードの `editId` + 名前の道筋(根からの子の名前の列)」で指す(`editor::OverrideChildPath`)。参照ノードの解決結果(`ResolveReferenceComponents` と同じく `PrefabSerializer::FromJson(PrefabNodeToJson(node), baseDir)` の canonical な木)から、版番号が変わったときだけ木を作り直して持つ(`HierarchyPanel` と `InspectorPanel` が共有する `EditorContext` の持ち物。ファイルを読むので毎フレームは作らない)。
- **同名の兄弟がある子とその子孫は読み取り専用**にする(名前の対応が一意でなく、patch が同名のすべてに当たるため)。ヒエラルキーで印(`[dup]`)を付け、インスペクターで理由を出す。
- 選択: `EditorContext` の選択に「仮想の子の道筋」を足す(ノード ID と道筋の組。道筋が空ならそのノード)。版番号が変わって道筋が解決できなくなったら選択を外す(今のノードの扱いと同じ)。シーンビューのクリックは今までどおり参照ノード全体を選ぶ(E11b)。

### 6. 子の値・追加・削除・復元(E12b)

すべて `OverrideEdit` の変換と `SetOverridesCommand` で行う。

- **子の値**: インスペクターは仮想の子の解決済みのコンポーネントを写して描く(参照ノードの根と同じ写し方)。確定で、道筋をたどって `overrides.children` の項目を名前で探し(無ければ `{ name }` で作る)、その項目に 1. の書く場所の規則(`addedComponents` にある型ならそこ、無ければ `components`)で値を書く。道筋の途中が「override で足した子」(基準に無い子。項目そのものが子の実体)なら、その項目の `components.<型>.<キー>` を直接書く。印・`Revert`(フィールド・コンポーネント)は根と同じ規則を、その項目に当てる。項目が空(`name` だけ)になったら項目ごと消す。
- **子の Add Component / Remove Component / Restore**: 根と同じ規則を、道筋の項目の `components` / `addedComponents` / `removedComponents` に当てる。
- **子を足す**(参照ノードか仮想の子の右クリック `Add Child`): 名前は既定 `Node`、兄弟(解決結果の子と `removedChildren` の名前)と重ならないよう `Node_1` から順に空いている名前にする(基準と同じ名前だと「追加」ではなく「patch」になるため)。親の項目の `children` に `{ name, components: { Transform: 既定値 } }` を足す。
- **子を消す**(仮想の子の右クリック `Delete`): 基準にある子なら、親の項目の `removedChildren` に名前を足し、`children` のその名前の項目を消す。override で足した子なら、`children` の項目を消すだけ。
- **子を戻す**: 親(参照ノードか仮想の子)のインスペクターの末尾に「Removed children」の折りたたみを出し、名前ごとに `Restore`(`removedChildren` から外す)。
- **override で足した子が参照(項目に `prefab` がある)か、自分の `overrides` を持つ(空でない)ときは、その子とその子孫を読み取り専用にする**(**その足した子そのもの**の Delete だけは出す。子孫には Delete も出さない。参照の項目の下の `children` / `removedChildren` は `ResolveNode` が読まないので、子孫を消しても反映されないため。Codex レビュー 3 回目)。参照なら項目の直下の `components` / `children` は参照先の編集にならず、実体でも自分の `overrides` が最後に当たるので、直下の `components` を書いても上書きされるため(Codex レビュー 2 回目)。必要になったら別に決める。
- **項目と解決後の名前の対応**: 既存の子への patch の項目は、項目の `name` = 解決後の名前。override で足した子の項目の解決後の名前は、項目の `overrides.name` があればそれ、無ければ項目の `name`(`ResolveNode` が外側の `name` を付けた後に `overrides.name` で上書きするため)。`OverrideEdit` は道筋の名前で項目を探すとき、この規則で各項目の解決後の名前を求めて比べる(Delete もこれで対象を決める)。
- 子の名前の変更・並べ替え・別の親への移動はしない(名前の対応が変わり、patch が別の子へ当たるため)。

### 7. Prefab への適用(E12c)

- 参照ノードの見出しの右クリック `Apply to Prefab`。**根のコンポーネントの override だけ**を参照先のファイルへ書き、子の override は書かない(子の項目の書き込み先が参照先の入れ子の構造で変わるため。必要になったら別に決める)。
- 適用できるのは、**直接の参照先の Prefab の根が実体ノードで、根に自分の `overrides` が無い(または空)**ときだけ。根が参照のとき、根に `overrides` があるとき(実体の根でも最後に自分の override が当たるので、`components` に書いても解決結果が変わってしまう。Codex レビュー 1 回目)は、メニューを無効にして理由をツールチップに出す。
- **書き込むパス**: Level の基準ディレクトリと `prefab` を結合した論理パスを `ResolveExistingResourcePath` で実際のファイルのパスに直し、**そのパスだけを**読み込み・確認のモーダルの表示・一時ファイル・rename に使う(読む関数は候補を探して読むが、書く関数は渡されたパスへそのまま書くので、論理パスのまま書くと別の場所に書いてしまう。Codex レビュー 1 回目)。見つからなければ適用しない。
- 手順: 確認のモーダル(「`<実際のパス>` を書き換えます。同じ Prefab を参照するすべてのノードが変わります。ファイルの書き換えは Undo できず、**Undo の履歴は消えます**。Prefab エディタでこのファイルを開いているなら、そこで保存すると上書きされます。」)→ `EditorContext` へ適用の要求(ファイル操作と同じく次のフレームの先頭の `ApplyPending` で行う)→ 生の JSON と先頭のコメントを読む(`JsonParser::ParseFile` / `ReadLeadingComments`。編集モデルを通さないので、読めない型や子は壊れない)→ 根の `components` に `components` を `Merge`、`addedComponents` を型ごとに置き換え、`removedComponents` の型を消す → **一時ファイルに書いて rename**(`LevelEditModel::SaveFile` と同じ。共通の関数 `JsonSerializer::WriteFileAtomic(path, json, comments)` を足し、`SaveFile` もそれを使う)→ 成功したら、この参照ノードの override から根の `components` / `addedComponents` / `removedComponents` を `SetReferenceOverrides` で直接消し(コマンドにしない)、**`CommandHistory::Clear()` で履歴を消し**(dirty になる)、版番号を進めてプレビューを作り直す。
- **Apply は Undo できない操作として扱い、履歴を消す**: ファイルを戻さないまま前の操作を取り消すと、変わった Prefab を基準に古い override が戻り、Remove の取り消しで型が戻らないなど、操作の前の状態に戻らない(Codex レビュー 1 回目)。New / Open と同じく取り消しの対象外にする。ログに `[editor] prefab applied: <path>`。
- 他の影響: 同じ Prefab を参照する他のノードは、次のプレビューの作り直しで変わる(コマンドがプレビューを作り直す)。他のノードが同じキーの override を持つなら、そちらが勝つ。Prefab エディタがそのファイルを開いていると、そこで保存したときに上書きされる(Prefab エディタは変更の有無もファイルの監視も持たない。README の既知の課題 2026-11-25 と同じ)。確認のモーダルにその旨も書く。
- 書き込みに失敗したらモデルを変えず、ステータスに赤で出す。

## 責務

| ファイル | 新規 / 変更 | 責務 | 段 |
|---|---|---|---|
| `aqEngine/Level/LevelEditModel.h/.cpp` | 変更 | `GetReferenceOverrides` / `SetReferenceOverrides` / `ResolveReferenceBase` | E12a |
| `Editor/OverrideEdit.h/.cpp` | 新規 | override の JSON の変換(印の判定、元に戻す、追加・削除・復元、子の道筋、子の追加・削除・復元、空き名) | E12a / E12b |
| `Editor/Commands/OverrideCommands.h/.cpp` | 新規 | `SetOverridesCommand` | E12a |
| `Editor/SceneGizmo.cpp` | 変更 | 参照ノードの確定を `SetOverridesCommand` に、開始の値を解決済みの値から | E12a |
| `Editor/Panels/InspectorPanel.h/.cpp` | 変更 | 印、右クリックの Revert、参照ノードの Add / Remove Component、Removed components(E12a)。仮想の子の写しと編集、Removed children(E12b)。Apply to Prefab(E12c) | E12a〜c |
| `Editor/EditorContext.h/.cpp` | 変更 | 仮想の子の木の持ち物と、選択の道筋 | E12b |
| `Editor/EditorContext.h/.cpp` | 変更 | 適用の要求(ファイル操作と同じく `ApplyPending` で行い、履歴を消す) | E12c |
| `Editor/Panels/HierarchyPanel.h/.cpp` | 変更 | 仮想の子の表示、Add Child / Delete | E12b |
| `aqEngine/Util/SimpleJson.h/.cpp` | 変更 | `JsonSerializer::WriteFileAtomic` | E12c |
| `Editor/PrefabApply.h/.cpp` | 新規 | 参照先のファイルへの適用 | E12c |
| `Tools/ReflectionTest` | 変更 | T17 | E12a〜c |
| VS のプロジェクトとフィルター | 変更 | 新規ファイルの登録(実装の前にフィルターをユーザーへ確認する) | E12a〜c |

`PrefabEditNodeOps.cpp`(`PrefabNodeFromJson` / `PrefabNodeToJson` / `BuildReferenceOverrides`)と Game.exe の Level / Prefab エディタ(v1 の UI)は変えない。

## このフェーズで作らないもの

- 子の override の Prefab への適用、入れ子の参照の根への適用。
- 子の名前の変更・並べ替え・移動、同名の兄弟がある子の編集。
- シーンビューで仮想の子を選ぶこと・ギズモで仮想の子を動かすこと(E11 の対応表は参照ノード全体に結んでいる)。
- 参照ノードの Prefab パスの差し替え。
- Game.exe の v1 の UI の作り直し(Level設計 16 章 R2 の v1 はそのまま)。

## テスト方針

**T17(ReflectionTest。Editor の `OverrideEdit.cpp`・`OverrideCommands.cpp`・`PrefabApply.cpp` を直接コンパイルする。Debug)**

- (a)(E12a)`OverrideEdit` の変換: 印の判定(`components` のキー、`addedComponents` の型)、フィールドの Revert(最後のキーで型ごと消える)、コンポーネントの Revert、Revert All(子の項目は残る)、Add / Remove / Restore Component(基準にある型・無い型・`removedComponents` の空になったらキーごと消える)。`GetReferenceOverrides` → `SetReferenceOverrides` で `ToJson` が変わらない(T6 の試験データのすべての参照ノード。名前を変えた参照ノード(`overrides.name` あり・無し)と、登録されていない型を override に持つ参照ノード(試験データに足す)を含む)。**混ぜた操作の Undo**: `RefPlain` で `Spawner.interval` を変える(`SetOverridesCommand`)→ Revert → Revert を Undo → 変更を Undo で、`ToJson` が最初と一字一句同じ。同じフレームに 2 つの `SetOverridesCommand`(別の型のフィールドの変更と Remove Component)を積んで、両方が残る。`addedComponents` の型のフィールドの変更が `addedComponents` に書かれ、解決結果に出る。`SetOverridesCommand` の実行・取り消し・やり直し。各操作の後の解決結果(`ResolveReferenceComponents`)が期待どおり(Revert で基準の値に戻る、Remove で型が消える、Restore で戻る)。`ResolveReferenceBase` が override を当てない値を返す。登録済みの型の Reflect の永続キーに入れ子のキーが無い。
- (b)(E12b)子の道筋の解決(基準の子・override で足した子・孫・`overrides.name` を持つ足した参照の子の Delete)、足した子が `prefab` か自分の `overrides` を持つときの読み取り専用の判定、子の値の書き込みと Revert(項目が空なら消える)、子の Add / Delete / Restore(基準にある子は `removedChildren`、足した子は項目を消す)、空き名(基準・`removedChildren` と重ならない)、同名の兄弟の検出。各操作の後に `PrefabSerializer` で解決した木が期待どおり。`T6_Overrides` の `RefFull`(`ChildA` の patch・`ChildNew` の追加・`ChildB` の削除)で往復。
- (c)(E12c)`WriteFileAtomic` に替えた `LevelEditModel::SaveFile` を使う既存の試験(T6・T7・T11・T12 の保存の往復)が同じ件数で PASS。一時フォルダの Prefab へ適用: 根の `components` に Merge・`addedComponents` の置き換え・`removedComponents` の除去、先頭のコメントと読めない型と子が残る、根が参照の Prefab・根に `overrides` がある Prefab は拒む、適用の後に履歴が空で dirty、適用後の override が消えて解決結果が適用前と同じ、`WriteFileAtomic` の書き込み。

**段ごとの確認**

| 段 | 自動で確認する(Claude) | ユーザーに確認する |
|---|---|---|
| E12a | 通常のツリーとエディタのツリーのビルド(Mac)、警告、T1〜T17(a)。T6(往復)・T13(Undo)の件数が変わらず PASS。`PrefabEditNodeOps.cpp` に差分が無い(`git diff` で確かめる。Game.exe の v1 の UI と T6 の往復はこのファイルの経路) | 印の見え方、Revert / Add / Remove / Restore の操作感 |
| E12b | ビルド・T17(b) | 仮想の子の表示と選択、子の追加・削除・復元の操作感 |
| E12c | ビルド・T17(c) | 確認のモーダルの文言、適用の結果 |

起動・撮影・合成入力は、E9〜E11 と同じく後回しにする(ユーザーの指示があるまで起動しない)。

## 評価チェックリスト

### E12a 根のコンポーネントの override

- [ ] override されているフィールドとコンポーネントに印が付く
- [ ] フィールド・コンポーネント・参照ノード全体の Revert で override が消え、Undo で戻る
- [ ] 参照ノードでコンポーネントを足す・消す・戻すことができ、Undo で戻る
- [ ] `GetReferenceOverrides` → `SetReferenceOverrides` の往復でモデルが変わらない(名前の変更・登録されていない型を含む)
- [ ] 参照ノードの値の編集(インスペクター・ギズモ)が `SetOverridesCommand` になり、編集と Revert を混ぜた Undo で元の JSON に戻る
- [ ] T17(a) が PASS

### E12b 子の override

- [ ] 参照ノードの下に仮想の子が出て、選ぶとインスペクターに解決済みの値が出る
- [ ] 子の値を変える・戻す、子のコンポーネントを足す・消す・戻す、子を足す・消す・戻すことができ、Undo で戻る
- [ ] 同名の兄弟がある子と、override で足した参照の子は読み取り専用になる
- [ ] T17(b) が PASS

### E12c Prefab への適用

- [ ] 根のコンポーネントの override を参照先へ書き、override が消え、解決結果が変わらない
- [ ] 根が参照の Prefab・根に `overrides` がある Prefab では適用できず、理由が出る
- [ ] 適用は実際のファイルのパスに書き、履歴が消えて dirty になる
- [ ] 書き込みは一時ファイル → rename で、先頭のコメントと読めない型と子が残る
- [ ] T17(c) が PASS
