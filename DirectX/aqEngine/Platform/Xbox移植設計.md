# Xbox 移植 設計メモ

エンジンを Xbox 実機(retail 機の Dev Mode)で動作させるための移植設計。
**主経路は 道A:UWP(GDK不要・NDAなし・個人で今すぐ着手可)**。
将来フル性能が必要になったら 道B:GDK(コンソール) へ移行できる構成にする。

---

## 0. どの道で行くか

| | 道A:UWP(本命) | 道B:GDK(コンソール) |
|---|---|---|
| 対象 | Dev Mode 上の UWP アプリ/ゲーム | `Gaming.Xbox.Scarlett/XboxOne.x64` |
| 入手 | 標準 Windows SDK + VS。Dev Mode 登録(Partner Center 一度きり ~$19)のみ | GDKX(承認制・NDA、ID@Xbox 等) |
| API | 公開API(DirectX12 / GameInput / XAudio2) | コンソール最適化API(`d3d12_x` / `PresentX`) |
| AI併用 | 全部公開なので問題なし | 機密ヘッダは外部AIに出さない |
| 性能 | Dev Mode の制限あり(後述 §1) | フル性能 |

→ **個人開発はまず道A**。性能の壁に当たったら ID@Xbox 申請して道Bへ。`IPlatform` 抽象のおかげで `PlatformUWP` → `PlatformGDK` の差し替えで移行できる。

---

## 1. UWP(Dev Mode)のリソース制限 ★設計に直結

Dev Mode の UWP は割り当てが厳格。**「Game」として分類しないと 1GB 前後に制限される**点が最重要。

### メモリ(RAM)
- システム総量: Series X = 16GB / Series S = 10GB。
- **標準アプリとして実行: 最大 1GB**。
- **ゲームとして実行: 最大 5GB**(下記の「Game」設定が必要)。
- バックグラウンド時: 最大 128MB(超過するとアプリ強制終了)。
- ファイルアクセス: 1ファイルにつき最大 2GB。

### CPU
- 標準アプリ: 他アプリ/ゲームの稼働数に応じて **2〜4コアのシェア**。
- ゲーム: **4コア占有 ＋ 2コア共有**。

### 「Game」として 5GB を使う設定
1. Xbox Dev Mode のダッシュボード(設定画面)を開く。
2. アプリ/ゲームのタイプを **「Game」** に設定。
3. Visual Studio からのデプロイでも、プロジェクト種別がゲーム構成になっていることを確認。
   - 未設定だと「アプリ」扱いで 1GB 前後に留まる。

参考: Microsoft Learn「Xbox システムリソース割り当てガイド」。

### エンジンへの設計反映
- **必ず「Game」分類でデプロイ**する(5GB / 4+2 コア前提)。デプロイ手順書 `Tools/SetupXboxDevMode` に明記。
- **メモリ予算を 5GB 上限で設計**:`MemoryManager`(`aq::memory::MemoryConfig`)のアロケータ総量を Xbox ビルドで 5GB を超えないよう構成プロファイルを切る。Series S も同じ 5GB 上限なので機種差は出にくいが、安全側に見積もる。
- **バックグラウンド 128MB**:PLM で suspend に入ったら GPU/大きなバッファを解放して 128MB 以下に収める実装が将来必要(道A/道B 共通の課題)。最低限、suspend 中にメモリを掴み続けないこと。
- **大アセットは 2GB/ファイル制限**に注意。単一の巨大パックを作らず分割する。
- **CPU 4+2 コア**:`ThreadPool` のワーカ数を Xbox ビルドで 4〜6 に固定(`std::thread::hardware_concurrency()` 任せにしない)。

---

## 2. 現状評価:どこまで流用できるか

Bridge パターンのグラフィクス抽象・`ISoundBackend`・`NativeWindowHandle(void*)` が既にあり素地は良い。

| サブシステム | 抽象化の状態 | UWP 流用度 | 主な改修 |
|---|---|---|---|
| Graphics | ◎ Bridge (`IGraphicsDeviceImpl`)、`NativeWindowHandle` 抽象済 | 中 | DXGI スワップチェーンを `CreateSwapChainForCoreWindow` に分岐 |
| Sound | ◎ `ISoundBackend`、`SoundBackend.h` で分岐 | 高 | XAudio2 は UWP でも標準。ほぼそのまま |
| Input | ○ `InputManager`/`Pad`(XInput) | 中 | UWP は GameInput(または `Windows.Gaming.Input`)に差し替え |
| Resource | △ `FindProjectRoot()` がソースツリー前提 | 低 | パッケージ install 相対へ(sandbox) |
| Entry/Loop | △ `WinMain`+`HWND`+`PeekMessage` が `Engine` に直結 | 低 | **CoreApplication/CoreWindow へ。最大の壁** |

唯一の「抽象漏れ」は `Engine` クラスが `#include <windows.h>` し `HINSTANCE`/`HWND`/`MsgProc` を直に持つ点(`Engine.h`, `Engine.cpp`)。今回の設計の中心。

**UWP は別アプリモデル**である点に注意:
- `WinMain`/`HWND` でなく `CoreApplication`/`CoreWindow`(`IFrameworkViewSource`)。
- スワップチェーンは `CreateSwapChainForCoreWindow`(標準 DXGI)。
- パッケージ化された sandbox アプリ。リソースは install フォルダ相対(`Windows.ApplicationModel.Package.InstalledLocation`)。
- ライフサイクル: PLM(Suspending/Resuming)を `CoreApplication` のイベントで受ける。

---

## 3. 設計

### 3.1 プラットフォーム層の切り出し(最優先・WinMain 改修の本体)

`Engine` から Win32 依存を抜き、`IPlatform` 抽象に逃がす。

```
aqEngine/Platform/
  IPlatform.h            // ウィンドウ/ループ/ライフサイクルの抽象
  PlatformWin32.{h,cpp}  // 既存の WinMain/HWND/MsgProc を移設
  PlatformUWP.{h,cpp}    // CoreApplication/CoreWindow + PLM (道A 本命)
  PlatformGDK.{h,cpp}    // GDK初期化 + PresentX (道B, 将来)
  PlatformEntry.cpp      // 各モデルのブートストラップ
```

`IPlatform` の責務(案):

```cpp
class IPlatform {
public:
    virtual ~IPlatform() = default;
    virtual bool CreateMainWindow(int w, int h, NativeWindowHandle& out) = 0;
    virtual bool PumpEvents() = 0;        // false で終了要求
    virtual void OnSuspend() {}           // メモリを 128MB 以下へ / GPU idle
    virtual void OnResume()  {}
    virtual const char* GetContentRoot() = 0;  // アセットの基点
};
```

`Engine` の変更:
- `HINSTANCE hInstance_` / `HWND hWnd_` / `MsgProc` を削除し `IPlatform*` 経由に。
- `InitializeParameter` から `hInstance`/`nCmdShow` を抜く。
- `RunGame()` の `PeekMessage` ループを `while (platform_->PumpEvents()) { Update(); }` に。

エントリポイント:
- **Win32**: 現状の `WinMain`(`PlatformWin32`)。
- **UWP**: `int main(Platform::Array<Platform::String^>^)` から `CoreApplication::Run(viewSource)` を呼び、`IFrameworkView::Run` の中で `aq::AppMain(IPlatform&)` 相当を回す。`NativeWindowHandle.handle` に `CoreWindow^`(の `IUnknown*`)を格納。
- `AppMain` の中身は現 `Game/Application/Main.cpp` のロジック(Engine 生成 → Initialize → RunGame → Finalize)を移植。ゲーム側コードは無変更。

### 3.2 Graphics:UWP スワップチェーン

Bridge があるので新バックエンドは増やさず、既存 `D3D12GraphicsDeviceImpl` 内をコンパイル時分岐:
- `CreateSwapChain`(`CreateSwapChainForHwnd`)→ UWP では `IDXGIFactory2::CreateSwapChainForCoreWindow`。
- `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL`/`FLIP_DISCARD`、`CoreWindow^` を渡す。
- それ以外(デバイス生成・RTV 管理・コマンドリスト)は標準 D3D12 のまま流用。
- ※`PresentX`/`d3d12_x` は **道B(GDK)専用**。道A では使わない。

### 3.3 Input:UWP の GameInput

`Pad`(XInput) の実装裏を差し替え。UWP では `IGameInput`(GDK系)または `Windows.Gaming.Input`(`Gamepad`クラス)を使う。`IInputBackend` を切り `XInputBackend`(Win32)/`GameInputBackend`(UWP)を `#ifdef` 選択。`PadButton`/`PadAxis` の公開 API は維持。

### 3.4 Sound:XAudio2 をそのまま

`SoundBackend.h` の分岐条件に UWP を含める。XAudio2 は UWP でも標準で使えるため `XAudio2SoundBackend` はほぼ無改修。リンク/ヘッダパスの調整のみ。

### 3.5 Resource:パッケージ install 相対へ

`FindProjectRoot()`(`Resource/Resource.cpp`)はソースツリーの `Game/Assets` を遡る設計で sandbox では破綻。
- `IPlatform::GetContentRoot()` を基点にする。Win32 は現状維持、UWP は `Package::Current->InstalledLocation->Path`。
- アセットを appx/msix パッケージに同梱(`Content` としてビルドに含める)。
- 単一ファイル 2GB 上限に注意し、大パックは分割。
- UWP ビルドでは `std::filesystem::current_path` 探索を切り、`GetContentRoot()` 直下固定に。
- セーブは `Windows.Storage.ApplicationData`(`LocalFolder`)。

### 3.6 ビルド構成

- `Engine.vcxproj`/`DirectX.vcxproj` に UWP 構成を追加(プロジェクト種別=ゲーム、§1)。または UWP 用 vcxproj を分離。
- defines: `AQ_PLATFORM_UWP` / `WINAPI_FAMILY=WINAPI_FAMILY_APP` / 既存 `ENGINE_GRAPHICS_D3D12`。Vulkan/D3D11 は UWP では除外。
- C++/WinRT(または C++/CX)を有効化。
- ThirdParty(Bullet/imgui)はビルド可。imgui はデバッグ専用に。
- **デプロイは必ず「Game」分類**(§1)。

---

## 4. ロードマップ(段階的)

| Phase | 内容 | 必要なもの |
|---|---|---|
| **0** | プラットフォーム層の切り出し。`IPlatform` 導入、`Engine` から Win32 を剥がす。現 Win32 経路で従来どおり動くこと確認 | なし |
| 1 | リソース予算プロファイル整備(メモリ 5GB 上限 / ThreadPool 4〜6 コア / 2GB ファイル分割) | なし |
| 2 | Resource を `GetContentRoot()` 経由に。UWP パッケージのアセット同梱整備 | なし |
| 3 | `PlatformUWP` 実装。CoreWindow + `CreateSwapChainForCoreWindow` で Dev Mode 起動 | Dev Mode 登録機 |
| 4 | GameInput / XAudio2(UWP) / PLM(Suspend で 128MB 以下) | Dev Mode 機 |
| 5 | (将来)道B:GDK へ。`PlatformGDK`+`PresentX` でフル性能 | GDKX(承認制) |

---

## 5. 今やること(実機/登録なしで進められる範囲)

- **A. プラットフォーム層切り出し**(3.1)。現 Win32 ビルドで回帰確認しながら。リスク最小・効果最大。
- **B. リソース予算の Xbox プロファイル**(§1):`MemoryConfig` の 5GB 上限版、`ThreadPool` のコア数固定、アセット 2GB 分割方針。
- **C. 抽象漏れ封じ**:`SoundBackend.h` に UWP 分岐、`Resource.cpp` を `GetContentRoot()` 経由に一段かませる(Win32 は現挙動)。`Pad` を `IInputBackend` で分離可能に。
- **D. `Tools/SetupXboxDevMode` 整備**:Dev Mode 登録手順、**「Game」分類でのデプロイ手順**、UWP 構成の vcxproj 雛形。

### 今やらない
- `PlatformUWP` の実機実装・GameInput 実機・PLM の 128MB 落とし込みは Phase 3 以降(Dev Mode 機が要る)。
- 道B(GDK / `PresentX`)は性能の壁に当たってから。
