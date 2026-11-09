---
name: cpp-gamedev
description: C++ゲーム開発のコーディングスタイル&設計ガイド(aqEngine。DirectX 11/12・Vulkan・Metal、Windows/UWP/Mac/iOS/Android 対応)。命名規則、ヘッダ/ソースのレイアウト、空行と区切りのルール、日本語Doxygenコメント、ブリッジパターンによるグラフィックスAPI・プラットフォーム抽象化、オブジェクト指向/デザインパターンとECS(データ指向)の設計方針を定義する。C++コードの生成・レビュー・リファクタリング、レンダリングコード、ゲームエンジン系コード(ECS、シーン管理、ステートマシン、リソース管理)、移植・プラットフォーム対応コード、クラス設計・ヘッダファイル作成を頼まれたら、「スタイル」と明示されていなくても必ずこのスキルを参照すること。
---

# C++ゲーム開発 スタイル&設計ガイド

ユーザーの実コードベース(aqEngine / Game)準拠。C++ を書く・直す・レビューするときは以下のすべてに従う。
**設計に関わる作業(レンダリング / 新モジュール / ECS / プラットフォーム対応、ファイル分割・
継承か委譲か・パターン適用の判断、スレッド境界)では、必ず `references/architecture.md` も読むこと。**

設計書の書き方・運用ルールは CLAUDE.md を参照(このスキルの対象外)。

---

## 1. 大原則

- コメントは**日本語**、ドキュメントコメントは **Doxygen `/** */`**。
- インデントは**タブ**。インクルードガードは `#pragma once`。
- 名前空間はネストして書き、各レベルでインデント 1 段(`namespace app::ecs` 形式は不可)。
- API 固有型(`ID3D11*` / `ID3D12*` / `Vk*` / Metal の `id<MTL...>`)を上位レイヤーに漏らさない
  (詳細は architecture.md §1)。
- 既存ファイルの改行コード・BOM は保持する(CLAUDE.md)。

## 2. 命名

| 対象 | ルール | 例 |
|---|---|---|
| クラス / 構造体 | PascalCase | `StateMachine`, `RenderCommandList` |
| インターフェース | `I` + PascalCase | `IState`, `IGraphicsDeviceImpl` |
| メンバ関数 | PascalCase | `Update()`, `RequestStateID()` |
| メンバ変数 | camelCase + 末尾 `_` | `stateMachine_`, `requestStateId_` |
| ローカル変数 / 引数 | camelCase | `stateId`, `targetHandle` |
| 定数(場所を問わず) | ALL_CAPS | `INVALID_STATE_ID`, `OFFSCREEN_RT_WIDTH` |
| 名前空間 | 小文字・短く | `app`, `aq::ecs` |
| テンプレートパラメータ | `T` / `T`+PascalCase | `T`, `TResourceBank` |
| 型エイリアス(using) | PascalCase | `StateHashMap` |
| マクロ / エンジン提供 | PascalCase / 小文字プレフィクス | `EngineAssert(...)`, `aqHash32(...)` |

- 略語: 2〜3 文字は大文字のまま(`RT`, `ID`, `ECS`)。メンバ変数内では camelCase に組み込む(`requestStateId_`)。
- 定数のサフィクス: 個数は `_COUNT`(`MAX_COMPONENT_COUNT`)、バイト数は `_SIZE`、上限一般は `MAX_` プレフィクス。

## 3. ファイルレイアウトと空行

**.h**: クラス内は「メンバ変数 → メンバ関数 → static」の順。
アクセス指定子はセクション区切りとして、同じレベルでも再掲してよい。
関連メンバ変数はグループごとに `/** 見出し */` を付ける。
単純アクセサはヘッダ内 `inline` 可、数行超のロジックは .cpp へ(テンプレートは例外)。
`using` エイリアスはクラス先頭の `private:` にまとめる。
アクセサ群など大きな機能セクションには `public:` の直前にブロック Doxygen で見出しを付けてよい。

**.cpp**: インクルード順は `"stdafx.h"`(先頭固定)→ 対応ヘッダ → プロジェクト内 → エンジン → 標準/外部。
ファイルローカル定数は冒頭の無名名前空間に `static constexpr`。

**空行ルール**:

| 場所 | 空行 |
|---|---|
| インクルード群 ↔ namespace | 2 行 |
| 前方宣言 ↔ クラス定義 | 2 行 |
| クラス内セクション間(.h) | 2 行 |
| クラスとクラスの間(.h) | 4 行(区切りコメントなし) |
| 関数と関数の間(.cpp) | 2 行 |
| クラス実装の区切り(.cpp) | 2 行 → `/************************************/` → 4 行 |

### インクルードは「集約ヘッダ優先」

新しい `#include` を足す前に、集約ヘッダ経由で既に供給されていないか確認する。
**.cpp は `stdafx.h` → 対応する自分の `.h` の順**なので、`.h` の中でも stdafx の中身が使える。
stdafx が供給するもの(Windows / D3D / DXGI / STL の主要ヘッダ / DirectXMath /
`Math/*` / `Engine.h` / `ECS/*` / `Component/*` / `Graphics/*` / `Rendering/*` / `Resource/*` / `UI/*` ほか)は
**再インクルードしない**。追加してよいのは、集約ヘッダに含まれない個別ヘッダ(自分が使う特定の
コンポーネント / システム等)だけ。共通で使うものを増やしたいときは個別 include ではなく
集約ヘッダ側への追加を検討する。
**現在の中身は実ファイル `aqEngine/aq.h` と `Game/Application/stdafx.h` を正**とする
(この文書には一覧を持たない)。

### テンプレート(.h)

```cpp
#pragma once
#include "Actor/StateMachine.h"


namespace app
{
	namespace ecs
	{
		/**
		 * ステートマシン機能
		 */
		class StateMachineComponent : public aq::ecs::IComponent
		{
			ecsComponent(app::ecs::StateMachineComponent);

		private:
			/** 状態関連 */
			app::actor::StateMachine stateMachine_;


		public:
			StateMachineComponent() {}


		public:
			inline app::actor::StateMachine* GetStateMachine() { return &stateMachine_; }
		};




		/**
		 * ステートマシン機能用のシステム
		 */
		class ActorStateMachineSystem : public aq::ecs::SystemBase
		{
		public:
			void Update() override;
		};
	}
}
```

> コンポーネントに空のデストラクタ(`~X() {}` / `= default` も同様)を**宣言しない**。
> 暗黙 move が消え、Chunk 拡張で move がコピーになる(上の例のように書かない)。

### テンプレート(.cpp)

```cpp
#include "stdafx.h"
#include "StateMachine.h"


namespace app
{
	namespace actor
	{
		namespace
		{
			static constexpr uint32_t INVALID_STATE_ID = 0xffffffff;
		}


		/**
		 * 待機
		 */
		IdleState::IdleState(StateMachine* stateMachine)
			: IState(stateMachine)
		{
		}


		void IdleState::Update()
		{
			// 移動入力があれば遷移
			if (!stateMachine_->GetDirection().IsZero()) {
				stateMachine_->RequestStateID(aqHash32("Move"));
			}
		}


		/************************************/




		/**
		 * 移動
		 */
		MoveState::MoveState(StateMachine* stateMachine)
			: IState(stateMachine)
		{
		}
	}
}
```

### メンバ初期化リスト

初期化子が 2 つ以上なら 1 行に 1 つ・**先頭カンマ**。

```cpp
		StateMachine::StateMachine()
			: currentState_(nullptr)
			, requestStateId_(INVALID_STATE_ID)
			, speed_(0.0f)
		{
		}
```

## 4. ブレース(ハイブリッド)

- **改行してから `{`**: `namespace`、クラス、関数、ラムダ本体
- **同じ行に `{`**: `if` / `else` / `for` / `while` / `switch`。`else` は `} else {`。
- ラムダは引数リストの後で改行し、1 段深くインデントして `{`。

```cpp
			aq::ecs::Foreach<StateMachineComponent>([](const aq::ecs::Entity& entity, StateMachineComponent* component)
				{
					component->GetStateMachine()->Update();
				});
```

## 5. コメント(日本語 Doxygen)

- クラス・構造体: ブロック形式 `/** ... */`(体言止め基本)
- 関数・変数グループ: 1 行形式 `/** 状態リクエスト */`
- 実装内: `//` ─「何を」より「**なぜ**」を書く
- `@param` / `@return` は複雑な関数のみ。自明なアクセサには付けない。

## 6. 型と言語機能

- 値渡し引数にも `const`(`const uint32_t stateId`)。参照渡しは `const T&` 基本。
- 仮想関数のオーバーライドは `override` 必須。抽象基底には仮想デストラクタ(漏れると未定義動作)。
- C スタイルキャスト禁止 → `static_cast<T>()`。幅を明示する整数は固定幅型(`uint32_t`)。
- コンパイル時定数は `static constexpr` + ALL_CAPS。`#define` 定数は禁止。
- `auto` は右辺から型が明らかな場合のみ。
- 所有権: 上位レイヤーは `std::unique_ptr` / `make_unique` 基本。エンジン内部でコンテナが寿命を
  明示管理する場合のみ生ポインタ可(確保と解放を同一クラス内で完結)。
  `ResourceBase::data_`(`void*`)を delete する側は、実型へ `static_cast` してから delete する
  (しないとデストラクタが走らず GPU リソースが漏れる)。
- 前提条件チェックは関数先頭で early return。
- エンティティ等の外部ハンドルは使用前に必ず `IsValid` 検証。

## 7. フォーマット詳細

- 連続代入では `=` を揃える。浮動小数リテラルは必ず `f` サフィクス(`0.0f`)。
- 集成体初期化は `{ }` の内側にスペース: `{ 1.0f, 1.0f, 1.0f, 1.0f }`
- 長い引数リストは開き括弧の後で改行して位置を揃える。
- 複数サブシステムを扱う初期化などは「`// 見出し` + 無名ローカルスコープ `{ }`」で論理グループ化。

## 8. エンジンイディオム(aq)

| イディオム | 用途 |
|---|---|
| `#include "stdafx.h"` | プリコンパイル済みヘッダ。.cpp 先頭固定 |
| `EngineAssert(expr)` / `EngineAssertMsg(...)` | 実行時アサート。不正状態の早期検出 |
| `aqHash32("Move")` | 文字列 32bit ハッシュ。状態 ID・リソース ID |
| `ecsComponent(型名)` | コンポーネント登録マクロ。クラス先頭 |
| `aq::ecs::Foreach<T>(ラムダ)` | 対象コンポーネント全走査 |
| `aq::ecs::EntityContext::Get()` | ECS コンテキストへのアクセス |
| `ClassName::Get()` | シングルトンアクセス |
| `Create()` / `Initialize()` | 二段階初期化。コンストラクタで重い処理をしない |

汎用コードを書く場合は同等の仕組みに読み替える。
