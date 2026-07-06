---
name: cpp-gamedev
description: C++ゲーム開発のコーディングスタイル&設計ガイド(DirectX 11/12・Vulkan・Xbox/UWP対応)。命名規則、ヘッダ/ソースのレイアウト、空行と区切りのルール、日本語Doxygenコメント、ブリッジパターンによるグラフィックスAPI抽象化、オブジェクト指向/デザインパターンとECS(データ指向)の設計方針を定義する。C++コードの生成・レビュー・リファクタリング、DirectXやVulkanのレンダリングコード、ゲームエンジン系コード(ECS、シーン管理、ステートマシン、リソース管理)、Xbox/UWP向けコード、クラス設計・ヘッダファイル作成を頼まれたら、「スタイル」と明示されていなくても必ずこのスキルを参照すること。
---

# C++ゲーム開発 スタイル&設計ガイド

ユーザーの実コードベース(DirectX 11/12 エンジン、ECS、ステートマシン)から抽出したルール。
C++コードを生成・編集・レビューするときは、以下のすべてのルールに従うこと。

グラフィックスAPI抽象化(ブリッジパターン)、プラットフォーム対応(Xbox/UWP)、
OOPとECSの使い分けなど**設計に関わるコードを書くときは、必ず `references/architecture.md` も読むこと。**

---

## 1. 大原則

- コメントは**日本語**で書く。ドキュメントコメントは **Doxygen形式(`/** */`)**。
- インデントは**タブ文字**(スペース不可)。
- インクルードガードは `#pragma once`。
- 名前空間はネストして明示的に書き、各レベルでインデントを1段下げる。
  C++17 の `namespace app::ecs` 形式は使わない。
- グラフィックスAPI(DirectX 11/12/Vulkan)は差し替え可能にする。
  API固有の型を上位レイヤーに漏らさない(詳細は architecture.md)。

```cpp
namespace app
{
	namespace ecs
	{
		// ここに中身(タブ2段)
	}
}
```

---

## 2. 命名規則

| 対象 | ルール | 例 |
|---|---|---|
| クラス / 構造体 | PascalCase | `StateMachine`, `MoveState`, `RenderCommandList` |
| インターフェース | `I` プレフィクス + PascalCase | `IState`, `IComponent`, `IGraphicsDevice` |
| メンバ関数 | PascalCase | `Update()`, `RequestStateID()`, `GetTargetHandle()` |
| メンバ変数 | camelCase + 末尾アンダースコア `_` | `stateMachine_`, `currentState_`, `requestStateId_` |
| ローカル変数 / 引数 | camelCase(アンダースコアなし) | `stateId`, `move`, `targetHandle` |
| 定数(クラス内・ファイルローカル問わず) | ALL_CAPS + アンダースコア区切り | `OFFSCREEN_RT_WIDTH`, `INVALID_STATE_ID` |
| 名前空間 | すべて小文字、短く | `app`, `app::actor`, `aq::ecs` |
| テンプレートパラメータ | `T` または `T` + PascalCase | `T`, `TResourceBank` |
| 型エイリアス(`using`) | PascalCase | `StateHashMap`, `StatePair` |
| マクロ / エンジン提供 | PascalCase または小文字プレフィクス | `EngineAssert(...)`, `aqHash32(...)`, `ecsComponent(...)` |

### 略語の扱い

- 2〜3文字の略語は大文字のまま: `RT`, `UI`, `ECS`, `ID`(例: `RequestStateID`)
- メンバ変数内では camelCase に組み込む: `offscreenRTHandle_`, `requestStateId_`

### 定数サフィクスの使い分け

- 定数のサフィクスは意味で使い分ける: **個数は `_COUNT`**(`MAX_COMPONENT_COUNT`)、
  **バイト数は `_SIZE`**(`PAGE_SIZE_BYTES` 等)、上限一般は `MAX_` プレフィクス。

---

## 3. ヘッダファイル(.h)のレイアウト

### クラス内の並び順

1. **メンバ変数**(クラスの上側)
2. **メンバ関数**(その下)
3. **static 関連**(static メンバ変数・static 関数はクラスの一番下)

アクセス指定子(`private:` / `protected:` / `public:`)を**セクションの区切り**として使う。
同じアクセスレベルでも、意味のまとまりが変わるたびに指定子を書き直してよい
(例: コンストラクタ群とアクセサ群で `public:` を2回書く)。

### 空行ルール(.h)

- インクルード群と `namespace` の間: **空行2行**
- クラス内のセクション間(変数ブロック↔関数ブロック、`public:` 同士など): **空行2行**
- **クラスとクラスの間: 空行4行**(区切りコメントは付けない)
- 前方宣言とクラス定義の間: **空行2行**

### テンプレート(実ファイル準拠)

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
			app::actor::StateMachine stateMachine_;


		public:
			StateMachineComponent() {}
			~StateMachineComponent() {}


		public:
			app::actor::StateMachine* GetStateMachine() { return &stateMachine_; }
		};




		/**
		 * ステートマシン機能用のシステム
		 */
		class ActorStateMachineSystem : public aq::ecs::SystemBase
		{
		public:
			ActorStateMachineSystem();
			~ActorStateMachineSystem();

			void Update() override;
		};
	}
}
```

### メンバ変数のグループ化

関連するメンバ変数は、グループごとに1行Doxygenコメントの見出しを付ける。

```cpp
		private:
			/** 状態関連 */
			StateHashMap stateHashMap_;
			IState* currentState_;
			uint32_t requestStateId_;

			/** 状態操作対象 */
			aq::ecs::EntityHandle targetHandle_;

			/** 移動関連 */
			aq::math::Vector3 direction_;
			float speed_;
```

### 大きな機能セクションの見出し

アクセサ群などが大きくなる場合、`public:` の直前にブロックDoxygenコメントで見出しを付ける。

```cpp
			/**
			 * 移動関連
			 */
		public:
			/** 移動方向 */
			inline void SetDirection(const aq::math::Vector3& dir)
			{
				direction_.Set(dir);
			}
```

### ヘッダ内実装のルール

- 単純なアクセサ(Get/Set)はヘッダ内に定義してよい。その際は `inline` を明示する。
- 数行を超えるロジックは .cpp に書く。テンプレート関数は例外的にヘッダに書く。
- `using` による型エイリアスはクラス先頭の `private:` セクションにまとめる。

---

## 4. ソースファイル(.cpp)のレイアウト

### 並び順と空行ルール(.cpp)

- インクルード順: `"stdafx.h"`(先頭固定) → 対応ヘッダ → プロジェクト内 → エンジン → 標準/外部。
- インクルード群と `namespace` の間: **空行2行**
- **関数と関数の間: 空行2行**
- **クラス(の実装群)が続く場合の区切り: 空行2行 → `/************************************/` → 空行4行**
  区切りの直後は、次のクラスのブロックDoxygenコメント(またはコンストラクタ)から書き出す。
- ファイルローカル定数は冒頭の**無名名前空間**に `static constexpr` で置く。

### インクルードは「集約ヘッダ優先」

新しい `#include` を足す前に、それが集約ヘッダ経由で既に供給されていないか
確認する。無駄な重複インクルードを増やさない。

- **.cpp は先頭で `#include "stdafx.h"` → 続いて対応する自分の `.h`** を
  include する。この順序により、**その `.h` の中でも stdafx の中身
  (`aq.h` 経由)が使える**。よって .cpp / .h のどちらでも、stdafx が
  供給するヘッダは再インクルードしない(.h を自己完結させる必要はない)。
- stdafx(→ `aq.h`)が**推移的に供給する**もの。これらは書かない。
  - `<windows.h>`(NOMINMAX 済み)、D3D11/12・DXGI・d3dcompiler
    (バックエンドマクロで選択)、`<dinput.h>`、DirectXTex
  - STL: `<vector> <array> <list> <string> <algorithm> <functional>`
    `<utility> <memory> <unordered_map> <thread> <mutex>`
    `<condition_variable> <future> <atomic> <chrono>`、
    `<cstdint> <cstring> <assert.h>` ほか
  - `<DirectXMath.h>`、`Math/Vector.h`・`Matrix.h`・`Utility.h`
  - エンジン中核: `Engine.h`、`ECS/ECS.h`、`Component/*`、
    `Graphics/*`、`Rendering/*`、`Resource/*`、`UI/*`、`Utility.h`
- **判断基準**: 集約ヘッダに含まれるものは書かない。含まれない個別ヘッダ
  (自分が使う特定のコンポーネント/システム等)だけを追加する。
- 共通で使うものを増やしたい時は、各所での個別 include ではなく
  集約ヘッダ(`aq.h`)側への追加を検討する。
- 上記一覧はあくまで目安。**現在の中身は実ファイル
  `aqEngine/aq.h` と `Game/Application/stdafx.h` を正**とし、
  増減した場合はこのリストではなく実ファイルを参照する。

### テンプレート(実ファイル準拠)

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


		IdleState::~IdleState()
		{
		}


		void IdleState::Update()
		{
			// 移動
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

2つ以上の初期化子がある場合、1行に1つずつ、**先頭カンマ**スタイルで書く。

```cpp
		StateMachine::StateMachine()
			: currentState_(nullptr)
			, requestStateId_(INVALID_STATE_ID)
			, targetHandle_()
			, direction_(0.0f)
			, speed_(0.0f)
		{
		}
```

---

## 5. ブレーススタイル(ハイブリッド)

- **改行してから `{`**: `namespace`、クラス、関数、ラムダ本体
- **同じ行に `{`**: `if` / `else` / `for` / `while` / `switch` などの制御文
- `else` は `} else {` の形でつなげる。

```cpp
		void MoveState::Update()
		{
			aq::math::Vector3 move = stateMachine_->GetDirection();
			if (!move.IsZero()) {
				move.Scale(stateMachine_->GetSpeed());
			} else {
				stateMachine_->RequestStateID(aqHash32("Idle"));
			}
		}
```

ラムダは引数リストの後で改行し、1段深くインデントして `{` を置く。

```cpp
			aq::ecs::Foreach<StateMachineComponent>([](const aq::ecs::Entity& entity, StateMachineComponent* component)
				{
					component->GetStateMachine()->Update();
				});
```

---

## 6. コメント規約(日本語 Doxygen)

| 用途 | 形式 | 例 |
|---|---|---|
| クラス・構造体 | ブロック形式 | 下記参照 |
| メンバ関数・アクセサ | 1行形式 | `/** 状態リクエスト */` |
| メンバ変数グループ | 1行形式 | `/** 移動関連 */` |
| 実装内の補足 | `//` | `// すでに追加済み` |
| .cpp のクラス区切り | `/************************************/` | — |

```cpp
		/**
		 * 状態の遷移を制御するクラス
		 */
		class StateMachine
```

- 説明は簡潔な日本語の体言止め・名詞句を基本とする(「〜する」も可)。
- 引数・戻り値の説明が必要な複雑な関数では `@param` / `@return` を使う。
  自明なアクセサには付けない。
- 実装内コメント(`//`)は「何をしているか」より「なぜそうするか」を優先して書く。

```cpp
		/**
		 * 指定IDの状態へ遷移をリクエストする
		 * @param request 遷移先の状態ID(aqHash32で生成)
		 */
		inline void RequestStateID(const uint32_t request) { requestStateId_ = request; }
```

---

## 7. 型と言語機能

- **引数の const**: 値渡しでも `const uint32_t stateId`、`const float speed` のように const を付ける。
  参照渡しは `const T&` を基本とする。
- **override**: 仮想関数のオーバーライドには必ず `override` を付ける。
- **キャスト**: Cスタイルキャスト禁止。`static_cast<T>()` を使う。
- **整数型**: 幅を明示する場合は `uint32_t` 等の固定幅型。
- **constexpr**: コンパイル時定数は `static constexpr`。`#define` 定数は使わない。
  命名は場所を問わず ALL_CAPS(`JUMP_POWER`, `OFFSCREEN_RT_WIDTH`)。
- **auto**: 型が右辺から明らかな場合のみ(`auto it = map.find(...)` など)。
- **所有権**: 上位レイヤーでは `std::unique_ptr` / `std::make_unique` を基本とする。
  エンジン内部でコンテナが要素の寿命を明示的に管理する場合(状態マシンの
  `new T(this)` / デストラクタでの `delete` など)は生ポインタも許容する。
  その場合、確保と解放のペアを同一クラス内で完結させること。
- **early return**: 前提条件チェックは関数先頭で `if (!x) { return; }` で抜ける。
- **ハンドルの有効性チェック**: エンティティ等の外部参照は使用前に必ず検証する。
  `if (aq::ecs::EntityContext::Get().IsValid(targetHandle)) { ... }`

---

## 8. フォーマット詳細

- 連続代入では `=` を揃える。

  ```cpp
  shadowSettings.resolution  = 2048;
  shadowSettings.orthoWidth  = 50.0f;
  shadowSettings.nearPlane   = 0.1f;
  ```

- 浮動小数リテラルには必ず `f` サフィクス(`0.0f`)。
- 配列・集成体初期化は `{ }` の内側にスペース: `{ 1.0f, 1.0f, 1.0f, 1.0f }`
- 長い引数リストは開き括弧の後で改行し、位置を揃えて折り返す。
- 初期化などで複数のサブシステムを扱う関数では、
  「`// 見出しコメント` + 無名ローカルスコープ `{ }`」で論理グループ化する。

---

## 9. エンジン固有のイディオム

ユーザーの自作エンジン(aq)/学校の K2 エンジン前提のコードで使う。
汎用コードを書く場合は同等の仕組みに読み替えること。

| イディオム | 用途 |
|---|---|
| `#include "stdafx.h"` | プリコンパイル済みヘッダ。.cpp の先頭固定 |
| `EngineAssert(expr)` / `EngineAssertMsg(...)` | 実行時アサート。不正状態の早期検出に使う |
| `aqHash32("Move")` | 文字列の32bitハッシュ。状態IDやリソースIDに使う |
| `ecsComponent(型名)` | コンポーネント登録マクロ。クラス先頭に置く |
| `aq::ecs::Foreach<T>(ラムダ)` | 対象コンポーネントを持つ全エンティティの走査 |
| `aq::ecs::EntityContext::Get()` | ECSコンテキストへのシングルトンアクセス |
| `ClassName::Get()` | シングルトンアクセスの一般形 |
| `Create()` / `Initialize()` | 二段階初期化。コンストラクタで重い処理をしない |

---

## 10. 設計ガイドへの入口

以下のいずれかに該当する作業では、**必ず `references/architecture.md` を読むこと**。

- レンダリングコード、グラフィックスAPI(DX11/DX12/Vulkan)に触れるコード
- 新しいクラス群・モジュールの設計、デザインパターンの適用
- ECS のコンポーネント/システムの追加
- Xbox・UWP などプラットフォーム対応