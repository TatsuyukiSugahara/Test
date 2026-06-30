# SetupXboxDevMode — Xbox(UWP / 道A)移植セットアップキット

aqEngine を **Xbox 実機(retail 機の Dev Mode)** で動かすための、登録・構成・デプロイ手順と
UWP プロジェクト雛形をまとめたフォルダです。

主経路は **道A:UWP(GDK 不要・NDA なし・個人で着手可)**。設計の全体像は
`DirectX/aqEngine/Platform/Xbox移植設計.md` を参照。本フォルダはその §1・§3.6・§5 の
「実機/登録なしで進められる範囲」と「実機が要る範囲」を手順に落とし込んだもの。

> ⚠️ 現状(2026-06 時点)で済んでいるのは **Phase 0(土台/抽象化)** のみ。
> `PlatformUWP` 本体・CoreWindow スワップチェーン・GameInput は未実装で、
> **この手順をすべて終えても、まだ実機起動には Phase 3+ の実装が必要**。
> 本キットは「Dev Mode 機を用意し、UWP 構成でビルド・デプロイできる状態」までを整える。

---

## 0. 何が要る / 要らない

| | 必要 | 備考 |
|---|---|---|
| Xbox 実機(Series X/S または One) | ○ | Dev Mode を有効化して使う |
| Partner Center 開発者登録 | ○ | 個人 ~ $19(一度きり) |
| GDK / GDKX | ✕ | 道A は不要(公開 API のみ) |
| NDA / ID@Xbox | ✕ | 道B(GDK)に移る時だけ |
| Visual Studio 2022 + UWP ワークロード | ○ | 「ユニバーサル Windows プラットフォーム開発」 |
| Windows SDK | ○ | 標準 SDK でよい |

---

## 1. Xbox Dev Mode を有効化する

1. Xbox 本体の Microsoft Store で **「Dev Mode Activation」** アプリを入手して起動。
2. [Partner Center](https://partner.microsoft.com/) で **開発者アカウント登録**(個人 ~$19、一度きり)。
3. Dev Mode Activation アプリに表示されるコードを Partner Center 側で登録 → **Dev Mode へ切替**(本体再起動)。
4. 本体が **Dev Home** で起動する。ここで以下を控える / 設定する:
   - **コンソールの IP アドレス**(Remote Access)
   - **デバイスポータルの有効化**(ブラウザから `https://<IP>:11443` でアクセス可)
   - **ペアリング用 PIN**(VS から接続する際に使用)

> 戻すときは Dev Home の「Retail へ切替」。データは消えるので注意。

---

## 2. ★「Game」分類でデプロイする(最重要)

Dev Mode の UWP は割り当てが厳格で、**「App(アプリ)」分類だと約 1GB に制限**される。
**「Game」分類**にして初めて以下の予算が使える(設計 §1):

| リソース | App 分類 | **Game 分類** |
|---|---|---|
| メモリ(RAM) | ~1GB | **最大 5GB**(Series X/S 共通) |
| CPU | 2〜4 コアのシェア | **4 コア占有 + 2 コア共有** |
| バックグラウンド時 | — | 128MB(超過で強制終了) |
| 1 ファイルのサイズ上限 | — | 2GB |

### 設定手順
1. アプリを一度デプロイする(下記 §4)。
2. **Dev Home → マイゲーム＆アプリ** で対象を選び、**アプリの種類を「ゲーム(Game)」に設定**。
   - Dev Home の UI はシステム更新で変わることがある。見当たらない場合は
     デバイスポータル(`https://<IP>:11443`)の該当アプリ設定からも変更可。
3. 再起動 / 再デプロイして、5GB プロファイルで動いていることを確認。

> エンジン側はこの予算を `aq::platform::GetResourceBudget()`(`AQ_PLATFORM_UWP` 時)で
> 5GB / 8MB スタック / 6 ワーカ / 2GB ファイルとして既に反映済み
> (`DirectX/aqEngine/Platform/PlatformBudget.h`)。**必ず「Game」でデプロイすること**が前提。

---

## 3. UWP 構成を用意する(vcxproj 雛形)

UWP は **別アプリモデル**(AppContainer / `CoreApplication`)なので、既存のデスクトップ
`DirectX/Game/DirectX.vcxproj` をそのままは使えない。**UWP 用 vcxproj を分離**する方針。

本フォルダの雛形:

| ファイル | 用途 |
|---|---|
| `UWP.props` | UWP 構成の MSBuild プロパティ集(`AppContainerApplication` / 各種ターゲットバージョン / `AQ_PLATFORM_UWP` 等)。新規 UWP vcxproj から `<Import>` する |
| `Package.appxmanifest.template` | appx/msix パッケージマニフェスト雛形。`<Application>` の Category 等を含む |

### 適用の要点(設計 §3.6)
- defines: **`AQ_PLATFORM_UWP` / `WINAPI_FAMILY=WINAPI_FAMILY_APP`** + 既存 `ENGINE_GRAPHICS_D3D12`。
  Vulkan / D3D11 は UWP では**除外**。
- `<AppContainerApplication>true</AppContainerApplication>`、`<ApplicationType>Windows Store</ApplicationType>`。
- C++/WinRT(または C++/CX)を有効化。C++ 標準は既存に合わせ `stdcpp20`。
- プラットフォームは **x64**(Xbox は x64)。
- アセットは **パッケージに同梱**(install フォルダ相対で読む。`IPlatform::GetContentRoot()` 経由)。
  単一ファイル 2GB 上限に注意し、大パックは分割する。
- DirectInput を使う `HID/Input.cpp`(キーボード/マウス)は UWP で**コンパイル不可**。
  Pad は `IPadBackend` で分離済みだが、KB/Mouse を含む完全な入力対応は **Phase 4(GameInput)**。

> `AQ_PLATFORM_UWP` を定義するだけではビルドは通らない(下記 §5 の未実装分があるため)。
> 本 props/manifest は「UWP プロジェクトの器」を用意するためのもの。

---

## 4. Visual Studio から実機へデプロイ

1. UWP vcxproj を開き、構成 **Debug | x64**、ターゲットを **リモート コンピューター** に設定。
2. プロジェクト プロパティ → デバッグ → **コンピューター名 = コンソールの IP**、
   認証モードを **ユニバーサル(暗号化なしも可)**。
3. 初回デプロイ時に **PIN ペアリング**を求められる → Dev Home の PIN を入力。
4. デプロイ後、§2 の手順で **「Game」分類**に設定。

---

## 5. ここまでで動く? → まだ。残りの実装(Phase 3+)

本キットを終えると「Dev Mode 機 + UWP 構成でビルド/デプロイの土台」が整う。
**実機起動には以下が未実装**(設計 §3・§5):

- [ ] `PlatformUWP`(`CoreApplication`/`CoreWindow` + PLM Suspend/Resume) … Phase 3
- [ ] D3D12 スワップチェーンの `CreateSwapChainForCoreWindow` 分岐 … Phase 3
- [ ] GameInput(キーボード/マウス/パッド)… Phase 4
- [ ] アセットのパッケージ同梱 + `GetContentRoot()` の UWP 実装 … Phase 2-3
- [ ] suspend 時に 128MB 以下へメモリ解放 … Phase 4

Phase 3 以降は **Dev Mode 実機が前提**。性能の壁に当たったら ID@Xbox を申請して
道B(GDK / `PresentX`)へ。`IPlatform` 抽象のおかげで `PlatformUWP` → `PlatformGDK` の
差し替えで移行できる。
