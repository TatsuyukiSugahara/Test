# C++ゲーム開発 設計ガイド(architecture)

`SKILL.md` から参照される設計リファレンス。グラフィックスAPI抽象化・プラットフォーム対応・
OOPとECSの使い分け・スレッド境界・ドキュメント運用の方針を定義する。
**設計に関わるコード(レンダリング/新モジュール/ECS/プラットフォーム対応)を書くときは本書に従うこと。**

出典はユーザーの実コードベース(`aqEngine/`)。抽象名・マクロ名は実物に合わせている。

---

## 1. グラフィックスAPI抽象化(ブリッジパターン)

DirectX 11 / DirectX 12 / Vulkan を**ブリッジパターン**で差し替え可能にする。
抽象(`GraphicsDevice` / `RenderContext`)が実装インターフェース(`IGraphicsDeviceImpl` /
`IRenderContextImpl`)を保持・委譲し、抽象と実装を独立に拡張できる構造。

```
上位レイヤー（Engine / aq::rendering / ゲーム）… API を知らない
      │
      ▼  抽象（Abstraction）
  GraphicsDevice ──保持──▶ IGraphicsDeviceImpl（Implementor）
  RenderContext  ──保持──▶ IRenderContextImpl
                              ├─ D3D11...Impl
                              ├─ D3D12...Impl   ← バックエンド実装
                              └─ Vulkan...Impl
```

> **Adapter との違い(用語注)**: Adapter は「既存の互換性のない IF を変換して
> 再利用する」パターン。本エンジンのように実装 IF をゼロから設計して差し替えるのは
> Bridge。構造が似ていても動機が違えばパターン名が変わる典型例。

### ルール

- **API 固有型(`ID3D11*` / `ID3D12*` / `Vk*`)を上位レイヤーに漏らさない。**
  これらが現れてよいのは `Graphics/D3D1x/`・`Graphics/Vulkan/` 内のみ。
  抽象 IF のメソッド引数・戻り値はすべて API 非依存型(`IBuffer` / `IShader` /
  `PixelFormat` / `NativeWindowHandle` 等)にする。
- **バックエンド選択はコンパイル時マクロ**で行う(`ENGINE_GRAPHICS_D3D11 / _D3D12 /
  _VULKAN` をちょうど 1 つ定義)。生成は `GraphicsDevice::Create<TImpl>()` テンプレートに注入する。
- **API を追加するときは「抽象 IF に no-op 既定を足す → 対応バックエンドだけ override」**の順。
  こうすると未対応 API(例: DX11 FL11_0 未満で compute/UAV/間接描画が無い)を壊さない。
  未対応かどうかは `IsComputeSupported()` 等のゲートで実行時にフォールバックする。
- 実装側(例: DX12)はこのインターフェースを実装し、内部で `GetNativeHandle()` 等を使って
  API 固有型へキャストする。呼び出し元は実装型を知らない。

### 同じ構造を他サブシステムにも展開する

グラフィックスと**同一の Bridge 構造**を、API/プラットフォーム差のある領域すべてに適用する。
新しい差し替え可能サブシステムを作るときはこの形に揃える。

| サブシステム | 抽象 | 実装インターフェース | バックエンド | 選択 |
|---|---|---|---|---|
| グラフィックス | `GraphicsDevice` / `RenderContext` | `IGraphicsDeviceImpl` / `IRenderContextImpl` | D3D11 / D3D12 / Vulkan | `ENGINE_GRAPHICS_*` |
| サウンド | `SoundEngine` | `ISoundBackend` / `ISoundVoice` | XAudio2 / (Oboe) | `SoundBackend.h` の `#define` |
| パッド入力 | `Pad` / `InputManager` | `IPadBackend` | XInput / WinRT | `PadBackend.h` の `#define` |
| プラットフォーム | `Engine` | `IPlatform` | Win32 / UWP / (GDK) | エントリで注入 |

---

## 2. プラットフォーム対応(Xbox / UWP)

- **プラットフォーム差は `IPlatform` に閉じる。** ウィンドウ生成・イベントループ・
  PLM ライフサイクル(`OnSuspend`/`OnResume`)・コンテンツ基点(`GetContentRoot`)を抽象化し、
  `Engine` は `IPlatform*` しか知らない。エントリ(`Main.cpp` / `UWPMain.cpp`)が
  Win32/UWP の実装を選んで注入する。
- **API 選択とプラットフォーム選択は直交させる。**「UWP × D3D12」「Win32 × D3D11」等の
  組み合わせが自由に成立するようにする(グラフィックス Bridge と `IPlatform` を混ぜない)。
- **`#if defined(AQ_PLATFORM_UWP)` 分岐は最小限にし、中立フォールバックを用意する。**
  例: UWP で DirectInput が使えないキーボード/マウスは中立状態(入力なし)で
  ビルド・動作させ、落ちないようにする。
- **リソース予算はコンパイル時プロファイル**(`PlatformBudget.h`)で持つ。
  Xbox Dev Mode(UWP「Game」分類)の制約(メモリ 5GB・4+2 コア・2GB/ファイル)を
  設計目標として反映する。Feature Level 等の具体値は該当プラットフォーム設計書に一元化し、
  他所では条件表記(例:「FL11_0 未満」)で参照する。

---

## 3. OOP / デザインパターンと ECS(データ指向)の使い分け

| 用途 | 方針 | 例 |
|---|---|---|
| 状態遷移・振る舞い | OOP(ステートマシン / ポリモーフィズム) | `IState`(Idle/Move)、`aqHash32` の状態 ID で遷移 |
| API/プラットフォーム抽象 | **Bridge**(§1 の中核) | GraphicsDevice / SoundEngine / IPlatform |
| 外部ライブラリ IF の変換 | Adapter | ImGui バックエンド接続(`imgui_impl_dx11` 等)のラップ |
| 大量エンティティのデータ処理 | **ECS(アーキタイプ・データ指向)** | Transform/描画/アニメの Component + System |
| 一意リソースの寿命管理 | シングルトン + 二段階初期化 | `Xxx::Get()`、`Create()`/`Initialize()` |

- OOP と ECS は排他ではなく橋渡しする(例: OOP のステートマシンを
  `StateMachineComponent` として ECS に載せる)。
- ECS の**構造変更(生成/破棄/Add/Remove)は System 内で即時実行せず**、遅延コマンド
  (`Request*` 系)に積み、`FlushCommands()` で一括適用する(並列 System 実行中の
  データ配置破壊を防ぐ)。詳細は §5。

---

## 4. パターン一覧(このガイドで使う主なもの)

| パターン | 適用先 |
|---|---|
| Bridge | グラフィックスAPI抽象化(本ガイドの中核。サウンド/パッド/プラットフォームも同構造) |
| Adapter | 外部ライブラリ IF の変換(ImGui バックエンド接続など) |
| Singleton + 二段階初期化 | エンジンサブシステム(`Get()` / `Create()`+`Initialize()`) |
| State | アクターの振る舞い(`IState` + ステートマシン) |
| Template Method | `aq::Application` の `On*` フック(共通処理は基底、差分はゲーム) |
| Data-Oriented(ECS) | 大量エンティティ(Archetype + Chunk の SoA) |

---

## 5. スレッド境界規約

エンジンは「ゲーム(メイン)/ レンダー / ワーカープール」の 3 種のスレッドで動く。
コードを書くときは以下の契約を守ること。

- **描画 API(コンテキスト操作)はレンダースレッドのみ。**
  ゲーム/ワーカースレッドは D3D コンテキストを一切触らない。描画は
  `RenderCommandList` に積んで `RenderThread::Submit()` で渡す。
- **GPU リソースの生成・アップロードはメインスレッドの
  `ResourceManager::Update()` に集約する。** ワーカーはデコード(CPU)まで。
- **ECS の構造変更(生成/破棄/Add/Remove)は System 内で即時実行しない。**
  遅延コマンド(`Request*` 系)に積み、`FlushCommands()` で一括適用させる。
- **並列処理の新規コードは `ThreadPool` を使う。** `JobSystem` は凍結中(新規使用禁止)。
- レンダースレッドに渡すデータ(`RenderFrame`)の GPU リソースは `shared_ptr` で
  保持し、1 フレーム遅延消費に耐える寿命にする。
- ワーカースレッドで COM/MF を使う場合は自前で初期化する(メインの初期化に依存しない)。

---

## 6. 設計書の運用規約

- 設計書は「ファイル別責務表 + 末尾チェックポイント(`- [ ]` 形式)」を基本形とする。
- タイトル直下に `> 対象コミット: <hash> / 最終更新: <日付>` を必ず置く。
  設計に影響するコミットで更新すること。
- **同じ内容を 2 つの設計書に書かない。** 一方を一次資料と明記し、他方はリンクで参照する。
- 内容は必ず実コード(ヘッダ/主要 .cpp)を読んで書く。推測で埋めない。
  未確認箇所は TODO として残す。
- 発見したバグ・命名の澱みは README の「既知の課題」節に追記する(その場で直さない)。
- Feature Level 等のプラットフォーム固有の具体値は該当プラットフォーム設計書に
  一元化し、他書は条件表記(例: FL11_0 未満)で参照する。
