# CMake ビルド セットアップ手順

Mac 移植(→ [設計書/Mac移植設計.md](../../設計書/Mac移植設計.md) §9 P0)のために追加した
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

## 5. Mac(P2 で検証)

```bash
cmake --preset macos-ninja      # または macos-xcode
cmake --build --preset macos-ninja-debug
```

- `AQ_GRAPHICS_API` は `Vulkan` 固定(道A = MoltenVK)。
  Vulkan SDK for macOS の導入手順は P2 で `Tools/SetupVulkan/README.md` に追記する。
- Bullet は Windows の prebuilt `.lib` ではなく
  `ThirdParty/BulletPhysics/src` をソースからビルドする。
- `ThirdParty/DirectXMath` と `ThirdParty/DirectX-Headers`(非 Windows 用 `sal.h`)の
  同梱、`stb_image`、`Platform/Mac` 一式は P1〜P2 の作業。**現時点の Mac 構成は
  まだ通らない**(ビルド定義だけ先に置いてある)。

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
