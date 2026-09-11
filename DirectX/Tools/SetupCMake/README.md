# CMake ビルド セットアップ手順

Mac 移植(→ [設計書/Mac移植設計.md](../../設計書/Mac移植設計.md) §9)のために追加した
CMake ビルドの導入手順。**既存の `DirectX.sln`(MSBuild)と併存する**もので、
Windows 開発の主経路は当面 `.vcxproj` のまま。

- ルート: `CMakeLists.txt`
- サブ: `aqEngine/CMakeLists.txt` / `Game/CMakeLists.txt` / `ThirdParty/CMakeLists.txt`
- ヘルパ: `cmake/AqCommon.cmake`
- プリセット: `CMakePresets.json`

---

## 1. 必要なもの

| ツール | バージョン | 入手 |
|---|---|---|
| CMake | 3.21 以上 | Visual Studio 同梱版でよい(下記) |
| Ninja | 任意(clang-cl 構成で使う) | Visual Studio 同梱版でよい |
| clang-cl | Visual Studio の「C++ Clang Tools for Windows」 | VS インストーラの個別コンポーネント |

### Visual Studio 同梱版を使う(推奨・追加インストール不要)

「x64 Native Tools Command Prompt for VS」を起動すると、下記が PATH に入る。

```
"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-cl.exe"
```

VS2022 を使う場合はパスの `18` を `2022` に読み替える。

### 単体でインストールする場合

```powershell
winget install Kitware.CMake
winget install Ninja-build.Ninja
```

---

## 2. VS プロジェクトを生成する

```powershell
cmake --preset windows-vs2026
cmake --build --preset windows-vs2026-debug
```

- 生成先は `build/windows-vs2026/`。**`DirectX.sln` は一切変更されない**
  (CMake が作るのは `build/windows-vs2026/AquaDash.sln`)。
- VS2026 が無い環境は `windows-vs2022` プリセットを使う。
- 実行ファイルは `build/<preset>/bin/<Config>/Game.exe`。
  デバッグ時の作業ディレクトリは `Game/` に設定してあるので、
  `Resource.cpp` の `FindProjectRoot()` が `Game/Assets` を見つけられる。

## 3. clang-cl 構成を生成する(可搬性検証用)

MSVC 拡張(`*_s` 系 / `#pragma comment` など)への依存を、Mac を触る前に
Windows 上で潰すための構成。

**必ず「x64 Native Tools Command Prompt for VS」から実行すること。**
Ninja は VS の環境変数(`INCLUDE` / `LIB` / `LIBPATH`)を継承して動くため、
素の PowerShell から実行すると Windows SDK やリンカが見つからない。

```powershell
cmake --preset windows-clang-cl
cmake --build --preset windows-clang-cl-debug
```

- `Ninja Multi-Config` なので Debug / Release の両方が 1 つの生成ツリーに入る。
- P0 の到達点は「**コンパイル・リンクが通る**」ところまで(実行は任意)。

## 4. グラフィックス API を切り替える

MSBuild 側のプロパティ `AqGraphicsApi`(`Game/GraphicsApi.props`)と
同じ意味・同じ既定値(`D3D12`)のキャッシュ変数 `AQ_GRAPHICS_API` がある。

```powershell
cmake --preset windows-vs2026 -D AQ_GRAPHICS_API=D3D11
cmake --preset windows-vs2026 -D AQ_GRAPHICS_API=Vulkan
```

- `ENGINE_GRAPHICS_<API>` の定義と API 別ライブラリのリンクがこれ 1 つで決まる。
  `aq.h` を書き換える必要はない。
- `Vulkan` を指定したときだけ環境変数 `VULKAN_SDK` を要求する。
  **SDK 未導入でも `D3D11` / `D3D12` の configure は失敗しない。**
- `Graphics/Vulkan/*.cpp` は API に依らず常にビルド対象に入る。非 Vulkan 構成では
  ファイル冒頭の `#ifdef ENGINE_GRAPHICS_VULKAN` により空 TU になるだけで、
  Vulkan SDK のヘッダは要求されない(既存 `Engine.vcxproj` と同じ方針)。

## 5. Mac(P2 到達済み)

macOS 26.6.2 / Apple Silicon / Xcode Command Line Tools + AppleClang 21.0.0 で
**ビルドが通り、AquaDash のタイトル画面まで起動する**ところまで確認済み。

### 5.1 ツールの導入(初回のみ)

Homebrew があれば `brew install cmake ninja` でよい。無い環境向けに、
**sudo 不要でユーザーローカルに置く**手順を示す(検証環境はこちら)。

Xcode ジェネレータ(`macos-xcode`)を使わないなら Command Line Tools だけでよい。
フル Xcode.app を入れた場合は、**ライセンス同意と初回セットアップを先に済ませること**。
これを飛ばすと `cmake --preset macos-xcode` が
`No CMAKE_C_COMPILER could be found` で止まる(`xcrun clang` も同じ理由で弾かれる)。

```bash
sudo xcodebuild -license accept
sudo xcodebuild -runFirstLaunch
```

```bash
xcode-select --install          # Command Line Tools だけで済ませる場合。Xcode.app があれば不要

mkdir -p ~/.local/bin ~/.local/opt && cd ~/Downloads
# CMake(macOS universal)。バージョンは cmake.org / GitHub Releases の最新に読み替える
curl -fsSLO https://github.com/Kitware/CMake/releases/download/v4.4.3/cmake-4.4.3-macos-universal.tar.gz
tar xzf cmake-4.4.3-macos-universal.tar.gz
mv cmake-4.4.3-macos-universal ~/.local/opt/cmake
ln -sf ~/.local/opt/cmake/CMake.app/Contents/bin/cmake ~/.local/bin/cmake

# Ninja(universal バイナリ 1 個)
curl -fsSLO https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-mac.zip
unzip -o ninja-mac.zip -d ~/.local/bin && chmod +x ~/.local/bin/ninja
```

Vulkan SDK for macOS(MoltenVK / dxc / validation layer が入る。約 360MB)は
LunarG から取得して**非対話インストール**できる。

```bash
cd ~/Downloads
curl -fL -o vulkan_sdk.zip https://sdk.lunarg.com/sdk/download/latest/mac/vulkan_sdk.zip
unzip -q vulkan_sdk.zip -d vulkan_sdk_extract && cd vulkan_sdk_extract
xattr -dr com.apple.quarantine vulkansdk-macOS-*.app
./vulkansdk-macOS-*.app/Contents/MacOS/vulkansdk-macOS-* \
    --root ~/VulkanSDK/1.4.357.1 --accept-licenses --default-answer-yes \
    --confirm-command install
```

毎回の環境設定はまとめて source すると楽。

```bash
cat > ~/.local/aq-mac-env.sh <<'EOF'
export PATH="$HOME/.local/bin:$PATH"
source "$HOME/VulkanSDK/1.4.357.1/setup-env.sh"
EOF
```

### 5.2 ビルドと実行

```bash
source ~/.local/aq-mac-env.sh
cd <repo>/DirectX
cmake --preset macos-ninja
cmake --build --preset macos-ninja-debug

# 実行は必ず CWD を Game/ にする(理由は下記)
cd Game && ../build/macos-ninja/bin/Debug/Game.app/Contents/MacOS/Game
```

### 5.2.0 ネイティブ Metal 構成でビルド・実行する

道B(ネイティブ Metal。設計書 `MetalBackend設計.md`)は**別プリセット**で、
Vulkan 構成と併存する。Vulkan 側の手順は何も変わらない。

```bash
source ~/.local/aq-mac-env.sh
cd <repo>/DirectX
cmake --preset macos-ninja-metal
cmake --build --preset macos-ninja-metal-debug

cd Game && ../build/macos-ninja-metal/bin/Debug/Game.app/Contents/MacOS/Game
```

**Metal 構成でも `VULKAN_SDK` が要る。** 描画には使わないが、シェーダのビルド経路
(`.fx` → `dxc` → `.spv` → `spirv-cross` → `.metal`)の 2 つのツールがどちらも
Vulkan SDK 同梱のため。未設定だと configure が `FATAL_ERROR` で止まる。

**Metal API Validation(Vulkan の validation layer に相当)の有効化:**

```bash
METAL_DEVICE_WRAPPER_TYPE=1 ../build/macos-ninja-metal/bin/Debug/Game.app/Contents/MacOS/Game
```

起動直後に `Metal API Validation Enabled` が出れば有効。Xcode で実行する場合は
スキームの Diagnostics で GUI から切り替えられるが、**Ninja ビルドの実行では
この環境変数が唯一の手段**。Metal の検証は既定で無効なので、**付け忘れると
「エラーが出ていない」のか「検証していない」のか区別がつかない**点に注意。

### 5.2.1 Xcode でビルド・実行する

**Xcode プロジェクトは CMake が生成する**。`.xcodeproj` はリポジトリに入っていないので、
まず生成してから開く(ソースを追加したときも同じ手順でよい)。

```bash
source ~/.local/aq-mac-env.sh
cd <repo>/DirectX
cmake --preset macos-xcode          # build/macos-xcode/AquaDash.xcodeproj を生成
open build/macos-xcode/AquaDash.xcodeproj
```

Xcode が開いたら:

1. ツールバー左のスキーム選択(実行ボタンの右)で **`Game`** を選ぶ。
   CMake は ALL_BUILD / ZERO_CHECK / aqEngine など**全ターゲット分のスキームを作る**ので、
   既定では別のものが選ばれていることがある。
2. その右のターゲット選択は **`My Mac`**。
3. **⌘B でビルド**、**⌘R でビルドして実行**。
   構成(Debug / Release)は Product > Scheme > Edit Scheme… > Run > Build Configuration。

コマンドラインから同じものをビルドしたいときは:

```bash
cmake --build --preset macos-xcode-debug     # または macos-xcode-release
```

生成物は `build/macos-xcode/bin/<Config>/Game.app`。

**Xcode はターミナルの環境を引き継がない**。これが 3 箇所に効くので、いずれも
CMake が configure 時に解決して焼き込んである(手で設定する必要は無い)。
動かないときはここを疑う:

| 設定 | 値 | 無いとどうなるか |
|---|---|---|
| Working Directory | `<repo>/DirectX/Game` | アセットが見つからない(§5.2 と同じ理由) |
| 環境変数 `VK_ICD_FILENAMES` ほか | Vulkan SDK の `setup-env.sh` と同じ値 | 実行時に **`vkCreateInstance` が `VK_ERROR_INCOMPATIBLE_DRIVER`(-9)で落ちる**。MoltenVK の ICD を見つけられない |
| `dxc` のパス | configure 時に解決した絶対パス | ビルド時に **`aqCompileSpv` が `PhaseScriptExecution failed with a nonzero exit code` で落ちる**(`Tools/ShaderCompile/compile_spv.cmake`) |

確認は Product > Scheme > Edit Scheme… > Run > Options(作業ディレクトリ)と
Arguments(環境変数)。


- **CWD は `Game/` にすること。** `.app` から起動すると `GetContentRoot()` が
  `Contents/Resources` を返し、ソースツリーの上方探索が行われない。ビルドしただけの
  `.app` には Assets が入っていないので、相対パス(`Assets/...` が CWD で解決する)に
  頼っている。設計書 §8-16。Assets を同梱して CWD に依存しない `.app` を作る手順は
  §5.2.2。
- 起動診断は CWD に `startup_timing.log` が出る。
- validation layer のメッセージは stderr に出る。P2 時点で**エラー 0 / 警告 10**
  (警告はストレージイメージのフォーマット不一致。Mac 固有ではない。設計書 §8-15)。

### 5.2.2 `.app` を配布形態にする(P5)

ビルドしただけの `Game.app` は `Contents/MacOS/Game` しか入っておらず、
CWD をソースツリーの `Game/` にしないと起動できない(§5.2 の最後の注意)。
**`aqBundleApp` ターゲット**を叩くと、単体で起動できる `.app` になる。

```bash
source ~/.local/aq-mac-env.sh
cd <repo>/DirectX

# Vulkan 構成
cmake --build --preset macos-ninja-debug --target aqBundleApp

# Metal 構成
cmake --build --preset macos-ninja-metal-debug --target aqBundleApp
```

**`aqBundleApp` は ALL に入っていない。** `Assets` が 92MB あり、毎ビルドで
コピーすると開発のイテレーションが目に見えて遅くなるため、明示的に叩いたときだけ
走る。通常の `cmake --build` の挙動は従来と変わらない。

投入されるもの(中身は `Tools/PackageApp/package_app.cmake`):

| 投入先 | 中身 | 構成 |
|---|---|---|
| `Contents/Resources/Game/Assets/` | `Game/Assets` 一式(生成した `Shader/msl` ・ `Shader/spv` を含む) | 共通 |
| `Contents/Frameworks/libvulkan.1.dylib` | Vulkan ローダー | Vulkan のみ |
| `Contents/Frameworks/libMoltenVK.dylib` | ICD(MoltenVK)本体 | Vulkan のみ |
| `Contents/Resources/vulkan/icd.d/MoltenVK_icd.json` | ICD 定義。`library_path` はバンドル内の相対パスへ書き換え済み | Vulkan のみ |

**`Resources` 直下ではなく `Resources/Game/Assets`** に置く点に注意。リソースの
パス解決は `"Assets/..."` を `<コンテンツルート>/Game/Assets/...` へ組み立てる
(UWP の appx も同じ理由で `install/Game/Assets/...` に置いている)。

Metal 構成は**外部 dylib 依存がゼロ**(`otool -L` がシステムフレームワークしか
出さない)なので、Assets を入れるだけで自己完結する。

ソースツリー外へコピーして起動する:

```bash
# Vulkan 構成
rm -rf /tmp/Game.app
cp -R build/macos-ninja/bin/Debug/Game.app /tmp/Game.app
cd /tmp && /tmp/Game.app/Contents/MacOS/Game

# Metal 構成
rm -rf /tmp/Game.app
cp -R build/macos-ninja-metal/bin/Debug/Game.app /tmp/Game.app
cd /tmp && /tmp/Game.app/Contents/MacOS/Game
```

Finder からダブルクリックしても起動する(CWD が `/` でも、`MacMain.mm` が
バンドル内の `Contents/Resources` へ移すため)。

**アプリ側でしている手当ては 2 つ**(`Game/Application/MacMain.mm` の
`SetupBundleEnvironment`。どちらもエンジン初期化より前):

1. **CWD をバンドルの `Contents/Resources` へ移す。**
   リソースは `GetContentRoot()` を見るが、**シェーダのパス解決だけは
   見ていない**(`VulkanShader.cpp` / `MetalShader.mm` /
   `MetalRenderContextImpl.mm` が CWD から上へ `Game/Assets` を探す独自実装)。
   CWD を合わせると両者が同じルートを指す。
2. **Vulkan の `VK_DRIVER_FILES` / `VK_ICD_FILENAMES` にバンドル内の
   `MoltenVK_icd.json` を設定する。** 無いと配布先で `vkCreateInstance` が
   `VK_ERROR_INCOMPATIBLE_DRIVER`(-9)になる。**既に設定されていれば触らない**
   ので、`source ~/.local/aq-mac-env.sh` した開発環境での起動は従来どおり。

どちらも「本物の `.app` から起動したとき」だけ行う。`aqBundleApp` を実行していない
`.app`(Assets 未同梱)では CWD も移さないので、§5.2 の `cd Game && ...` の
開発フローはそのまま使える。

**自己完結の確認**(SDK を入れていない配布先で壊れていないか):

```bash
otool -L /tmp/Game.app/Contents/MacOS/Game       # @rpath と /System 以外が出ないこと
otool -l /tmp/Game.app/Contents/MacOS/Game | grep -A2 LC_RPATH
```

`aqBundleApp` は Vulkan 構成のとき、実行ファイルに残っている
`$VULKAN_SDK/lib` の `LC_RPATH` を `install_name_tool -delete_rpath` で剥がす。
残したままだと「同梱した dylib ではなく開発機の SDK を読んでいるだけ」の状態に
気づけない。剥がした後に残る rpath は `@executable_path/../Frameworks` のみ。

コード署名は範囲外(設計書 §9 P5)。他人の Mac へ配る場合は Gatekeeper の
扱いが別途必要になる。

### 5.3 前提と構成

- `AQ_GRAPHICS_API` は `Vulkan` 固定(道A = MoltenVK)。`dxc` は Vulkan SDK 同梱で、
  `AQ_GRAPHICS_API=Vulkan` のときだけ `.spv` 生成ターゲット(`aqCompileSpv`)が配線される。
  59 エントリすべてが生成でき、実行時は `.spv` だけでシェーダを作れている。
- Bullet は Windows の prebuilt `.lib` ではなく `ThirdParty/BulletPhysics/src` をソースからビルド。
- `-G Xcode`(`macos-xcode` プリセット)も Ninja と同じ結果になることを確認済み
  (Xcode 26.6)。ただしフル Xcode.app が要る。

### 5.4 P2 で潰した Mac 固有の問題

再発したときの手掛かりとして残す。詳細は設計書 §9 P2 の評価欄。

| # | 症状 | 原因と対処 |
|---|---|---|
| 1 | `LinearMath/btScalar.h` が無いと全 Bullet ソースで出る | `src/CMakeLists.txt` はインクルードパスを持たない(ルート側にある)。`ThirdParty/CMakeLists.txt` で 6 ターゲットに付与 |
| 2 | `'sal.h' file not found` | 上流の DirectXMath / DirectX-Headers のどちらも同梱していない。`ThirdParty/WinCompat/sal.h` を自前で追加 |
| 3 | `DirectXTexFlipRotate.cpp` が WIC シンボルで落ちる | このファイルだけ `_WIN32` ガードが無い。非 Windows では除外(未使用) |
| 4 | `EnginePrintf("...")` が `expected expression` | 可変引数ゼロで末尾カンマが残る。MSVC / clang-cl は独自拡張で通す。`Printf(__VA_ARGS__)` へ |
| 5 | `_TRUNCATE` が未定義 | `strncpy_s` の残り。`std::snprintf(buf, sizeof(buf), "%s", …)` へ |
| 6 | リンクで **D3D12 の未定義シンボル** | CMake が `ENGINE_GRAPHICS_Vulkan` を定義していた(コードは `_VULKAN`)。D3D11/D3D12 は元から大文字なので Vulkan 構成でしか出ない |
| 7 | `.mm` で `@interface` が壊れる / `BOOL` の typedef 衝突 | PCH(`aq.h`)→ DirectXTex → スタブ `basetsd.h` が `BOOL`/`interface` を定義。`aq.h` で `__OBJC__` のとき DirectXTex を外す |
| 8 | `ImGui::NewFrame` で `Invalid DisplaySize` | `ImGui_ImplWin32_NewFrame` が非 Windows で呼ばれない。`Application.cpp` で `DisplaySize`/`DeltaTime` を自前で埋める(P4 で `imgui_impl_osx` へ) |
| 9 | validation の `VUID-VkRenderingInfo-pNext-06079/06080` | Retina で drawableSize が 2560x1440。`contentsScale = 1` に固定して 1280x720 に揃える(設計書 §8-13) |
| 10 | 終了時に validation が `currently in use by VkCommandBuffer` を並べる | GPU の完了を待たずに破棄していた。`Application::Finalize` でレンダースレッド停止直後に `vkDeviceWaitIdle` |
| 11 | 終了時に VMA が `Some allocations were not freed` でアサート | `void*` への `delete` でデストラクタが走らずテクスチャが漏れていた(リソース 4 型)。`delete static_cast<T*>(data_)` へ |
| 12 | `vkDestroyDevice(): has 2 leaked objects` | 関数ローカル static(`FontAssetCache` / `GpuClusterCuller`)がデバイスより長生き。`Finalize` 時に明示的に手放す |
| 13 | `macos-xcode` の configure で `No CMAKE_C_COMPILER could be found` | Xcode 導入直後でライセンス未同意。`sudo xcodebuild -license accept` と `-runFirstLaunch` を通す |
| 14 | Xcode の ⌘R で `vkCreateInstance` が -9 で落ちる | スキームに Vulkan の環境変数が無い。`cmake --preset macos-xcode` を実行し直してスキームを作り直す(`Game/CMakeLists.txt` が焼き込む) |
| 15 | Xcode でビルドはできるが実行するとアセットが見つからない | スキームの Working Directory が `DirectX/Game` になっていない。同じく再 configure で直る |
| 16 | Xcode の ⌘B が `aqCompileSpv` の `Command PhaseScriptExecution failed with a nonzero exit code` で落ちる | `.spv` 生成スクリプトが dxc をビルド時に `$VULKAN_SDK` から探しており、Xcode にはその環境が無い。configure 時に絶対パスを焼き込むよう直したので、`cmake --preset macos-xcode` を実行し直す |

---

## 6. 既存 `DirectX.sln` との併存についての注意

- **出力先が別**: MSBuild は `x64/<Config>/`、CMake は `build/<preset>/bin/<Config>/`。
  互いの成果物を上書きしない。中間ファイルも `build/` 配下に閉じる。
- **`build/` はバージョン管理に入れない**。リポジトリ直下の `.gitignore` に
  `build/` を追加しておくこと。
- **ソースの追加/削除は今も `.vcxproj` / `GameSources.props` が正**。
  CMake 側は `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)` で拾うので追記は不要だが、
  MSBuild 側は `vs-project-files` スキルの手順どおり `.vcxproj` と `.filters` へ
  登録すること。両者が食い違うと「MSBuild では通るが CMake では通らない」
  (またはその逆)という状態になる。
- **UWP(Xbox)は CMake では扱わない**。`DebugXbox` / `ReleaseXbox` 構成と
  `GameUWP.vcxproj` は MSBuild 専用の経路のまま。
- **CMake が拾わないファイル**: `aqEngine/Graphics/GPUBuffer.cpp` は
  `Engine.vcxproj` の `ClCompile` に無く現在ビルドされていない旧実装のため、
  CMake 側でも明示的に除外している。`Game/Application/UWPMain.cpp` と
  `aqEngine/Platform/PlatformUWP.cpp` も同様に除外。
- **CRT の組み合わせ**: 既存 vcxproj に合わせて Debug = `/MTd`、Release = `/MD`。
  `ThirdParty/BulletPhysics/lib/Debug` の prebuilt が `/MTd` ビルドのため、
  ここを変えるとリンクエラーになる。

---

## 7. よくあるつまずき

| 症状 | 原因と対処 |
|---|---|
| `windows-clang-cl` でリンカ/Windows SDK が見つからない | 素の PowerShell から実行している。x64 Native Tools Command Prompt を使う |
| `Visual Studio 18 2026` ジェネレータが無いと言われる | CMake が古い。`windows-vs2022` プリセットを使うか CMake を更新する |
| `環境変数 VULKAN_SDK が必要です` で configure が止まる | `AQ_GRAPHICS_API=Vulkan` を指定したが SDK 未導入。SDK を入れるか API を戻す |
| `LNK4099`(PDB が無い)の警告 | Bullet の prebuilt `.lib` に PDB が揃っていないため。`/ignore:4099` で抑止済み |
| ソースを追加したのに CMake がビルドしない | `CONFIGURE_DEPENDS` は生成ツリーが一度でも再 configure されないと効かない。`cmake --preset <name>` を再実行する |
