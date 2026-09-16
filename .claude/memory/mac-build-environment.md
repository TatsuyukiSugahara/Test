---
name: mac-build-environment
description: この Mac(Apple Silicon)での aqEngine ビルド環境の実際の構成 — Homebrew は無く、cmake/ninja/Vulkan SDK は ~/.local と ~/VulkanSDK に手置きしてある
metadata: 
  node_type: memory
  type: project
  originSessionId: b433d6ee-e55e-4f94-8efe-95de52960ea5
  modified: 2026-09-10T06:07:21.028Z
---

この Mac(macOS 26.6.2 / Apple Silicon)には **Homebrew が入っていない**。
Xcode.app は 2026-09-10 に導入済み(26.6。ライセンス同意 / runFirstLaunch も完了)。
ビルドツールは同日に sudo 不要でユーザーローカルへ手動導入した。

- `~/.local/opt/cmake`(CMake 4.4.3 / macOS universal tarball)→ `~/.local/bin/cmake` に symlink
- `~/.local/bin/ninja`(1.13.2)
- `~/VulkanSDK/1.4.357.1`(LunarG。MoltenVK / dxc / validation layer)
- **`source ~/.local/aq-mac-env.sh`** で PATH と `VULKAN_SDK` がまとめて入る。ビルド・実行の前に必ずこれを叩く

**How to apply:** Mac でビルドするときは `source ~/.local/aq-mac-env.sh` を先頭に付ける
(付け忘れると `cmake: command not found` か `環境変数 VULKAN_SDK が必要です` で止まる)。
`brew install` を提案しない。`macos-ninja` / `macos-xcode` はどちらも動く
(通常は Ninja のほうが速い)。

手順そのものは `DirectX/Tools/SetupCMake/README.md` §5 に書いてあるので、そちらが一次資料。
関連: [[mac-port-phase-status]]
