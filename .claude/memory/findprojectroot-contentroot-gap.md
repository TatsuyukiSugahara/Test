---
name: findprojectroot-contentroot-gap
description: FindProjectRoot が 6 ファイルに重複していて、GetContentRoot() 対応が入ったのは 2 本だけ
metadata:
  type: project
---

アセットのパス解決 `FindProjectRoot()` は **6 ファイルに重複実装**されている。
Android 移植(P2)で「`Engine::GetContentRoot()` を最優先で見て、返れば CWD の
上方探索をしない」形へ直したが、**直したのは 2 本だけ**。

| ファイル | GetContentRoot 対応 |
|---|---|
| `Resource/Resource.cpp` | **済** |
| `Graphics/Vulkan/VulkanShader.cpp` | **済**(あわせてスレッドセーフな static 初期化にした) |
| `Graphics/Metal/MetalShader.mm` | **未** |
| `Graphics/Metal/MetalRenderContextImpl.mm` | **未** |
| `Graphics/D3D11/D3D11Shader.cpp` / `D3D12Shader.cpp` | 未(Windows 専用なので実害なし) |

**Metal の 2 本は iOS で必ず詰まる**(バンドルは read-only で CWD が当てにならないため、
シェーダが 1 本も読めず黒画面になる)。Android は Vulkan なので踏まなかった。

**Metal の 2 本は互いの写しで、コード中に「片方だけ変えないこと」と明記されている。**
必ず同時に直すこと。Mac も同じコードを通るので Mac の回帰確認が要る。

同じ Android P2 で `SimpleJson::ParseFile` / `ImageLoader` / `WavDecoder` /
`WavStreamDecoder` は `ResolveExistingResourcePath()` 経由になった。
**このため iOS で `chdir` は不要**(Mac は今も `MacMain.mm` で chdir している)。

**How to apply:** iOS 移植 P2 で直す。関連: [[ios-port-phase-status]]
