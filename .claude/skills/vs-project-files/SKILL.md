---
name: vs-project-files
description: このリポジトリ(DirectX/aqEngine エンジン + Game)に新しいソースファイル(.cpp/.h)を追加・作成・リネーム・削除するときに、Visual Studio のプロジェクト(.vcxproj / GameSources.props)とフィルター(.vcxproj.filters)へも必ず登録するためのワークフロー。フィルターは基本フォルダ構成に合わせるが、どのフィルターに入れるかは追加前に必ずユーザーへ確認する。新規クラス/コンポーネント/システムのファイルを作る、ファイルを移動する、といった作業で「プロジェクトに追加して」と明示されていなくても参照すること。
---

# Visual Studio プロジェクト & フィルター登録

このリポジトリでは、ソースファイルを新規追加してもディスク上に置くだけでは
**ビルド対象にならず、VS のソリューションエクスプローラーにも出ない**。
`.cpp` / `.h` を追加・リネーム・移動・削除したら、必ず以下も同期する。

---

## 1. どのファイルを編集するか(配置先で分岐)

追加するファイルのパスで編集対象が変わる。

| ファイルの場所 | ソース登録先 | フィルター登録先 |
|---|---|---|
| `aqEngine/...`(エンジン) | `aqEngine/Engine.vcxproj` | `aqEngine/Engine.vcxproj.filters` |
| `Game/...`(ゲーム) | `Game/GameSources.props`(**1箇所で Desktop/UWP 両方に反映**) | `Game/DirectX.vcxproj.filters` |

- Game のソースは `GameSources.props` に集約されている。`DirectX.vcxproj` と
  `GameUWP.vcxproj` の両方が Import するので、**props を 1 箇所直せば両対応**になる
  (個別の vcxproj には書かない)。
- `GameUWP.vcxproj.filters` は存在しない。Game のフィルターは
  `DirectX.vcxproj.filters` のみ維持する(UWP はフィルター管理対象外)。

---

## 2. 追加の手順

### 2-1. まず「どのフィルターに入れるか」を確認する ★必須

**基本ルールはフォルダ構成に一致**させる。フィルター値 = プロジェクトルートから
見たフォルダパス(ファイル名は含めない)。

- `Component\FooSystem.cpp` → `<Filter>Component</Filter>`
- `Graphics\D3D12\Bar.cpp`   → `<Filter>Graphics\D3D12</Filter>`
- ルート直下(`aq.cpp` など)→ `<Filter>` を付けない

ただし**実際に書き込む前に、フォルダ一致の案を提示してユーザーに確認を取る**。
(意図的にフォルダと別のフィルターへ入れたい場合があるため。)
`AskUserQuestion` で「フォルダ通り `<提案パス>` でよいか / 別フィルターか」を尋ね、
回答を得てから編集する。既存の `.vcxproj.filters` の `<Filter Include=...>` 一覧を
選択肢として提示できるとよい。

### 2-2. ソースを登録する

- `.cpp` は `<ItemGroup>` 内の `ClCompile` 群へ、`.h` は `ClInclude` 群へ追加。
- パスは登録先ファイルのディレクトリ基準の相対パス。区切りは `\`(バックスラッシュ)。

```xml
<ClCompile Include="Component\FooSystem.cpp" />
<ClInclude Include="Component\FooSystem.h" />
```

### 2-3. フィルターを登録する

`.vcxproj.filters` の対応する `ItemGroup`(ClCompile 用 / ClInclude 用)へ、
`<Filter>` 付きで同じ Include パスを追加する。

```xml
<ClCompile Include="Component\FooSystem.cpp">
  <Filter>Component</Filter>
</ClCompile>
```

```xml
<ClInclude Include="Component\FooSystem.h">
  <Filter>Component</Filter>
</ClInclude>
```

### 2-4. フィルターフォルダが未定義なら新規作成する

入れたいフィルターパスが `.vcxproj.filters` にまだ `<Filter Include=...>` として
無い場合、宣言を追加する。**ネストする各階層それぞれ**を宣言すること
(`Graphics\D3D12` を足すなら `Graphics` と `Graphics\D3D12` の両方)。

```xml
<Filter Include="Graphics\D3D12">
  <UniqueIdentifier>{生成した新規GUID}</UniqueIdentifier>
</Filter>
```

- `UniqueIdentifier` は**新しく生成した GUID**を `{...}` 形式で入れる
  (既存 GUID と重複させない)。
- `<Filter Include=...>` 群はファイル先頭の `ItemGroup` にまとまっている。

---

## 3. リネーム / 移動 / 削除

- **リネーム・移動**: ソース登録先(vcxproj / props)とフィルター登録先の
  両方で、旧 Include を新パスへ更新する。移動でフィルターが変わる場合は
  §2-1 の確認を再度行う。
- **削除**: 両ファイルから該当 `ClCompile` / `ClInclude` エントリを削除する。
  そのフィルターが空になっても `<Filter Include>` 宣言は残してよい(害はない)。

---

## 4. 注意点・落とし穴

- **ソース登録とフィルター登録は別ファイル**。片方だけだとビルドされない、
  または VS 上でフォルダ分けされず散らかる。必ず両方そろえる。
- **Include パスは両ファイルで完全一致**させる(大文字小文字・`\` 含む)。
  一致しないと VS がフィルター対応を認識できない。
- Game の `.filters` には props と食い違う古いエントリ(既に無い `Scene\*` など)が
  残っていることがある。編集ついでに気づいたら整合を取ってよいが、
  勝手な大掃除より、まず今回追加分を正しく入れることを優先する。
- 迷ったら**現物の `.vcxproj.filters` の既存エントリの書式にそろえる**のが正。
