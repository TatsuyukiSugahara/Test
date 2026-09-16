---
name: resource-data-void-ptr
description: ResourceBase::data_ は void* なので、delete する側が必ず元の *Data 型へ static_cast しないとデストラクタが走らない
metadata:
  type: project
---

`aqEngine/Resource/Resource.h` の `ResourceBase::data_` は **`void*`**。
派生リソースのデストラクタで `delete data_;` と書くと、**メモリは解放されるが
デストラクタが呼ばれない**(void* への delete)。`TextureData::~TextureData` が
動かず SRV = VkImage + VMA アロケーションが丸ごと漏れる、という形で出る。

正しい書き方は `delete static_cast<TextureData*>(data_);`。

**Why:** コンパイラが何も言わない(clang でも警告 0)ため、リソース型を追加した
ときに素直に `delete data_;` と書くと静かに漏れる。2026-09-10 に残っていた 4 型
(Mesh / PMD / GPU / Shader)を修正済みだが、**新しいリソース型を足すときに再発しうる**。

**How to apply:** `ResourceBase` を継承する型を追加/レビューするときは、
デストラクタが `static_cast` してから delete しているかを必ず見る。
同様に、**関数ローカル static / ファイルスコープのグローバルなキャッシュ**は GPU デバイスより
長生きするので `Finalize` で明示的に手放す必要がある。これまでに見つけて直したのは
`FontAssetCache`・`GpuClusterCuller`・`InstancedStaticMesh` の名前レジストリ `g_named` の 3 つ。
**Vulkan の VMA が Debug でアサートしてくれるので Mac で最初に露見する**が、いずれも
プラットフォーム非依存の問題。新しくグローバルなキャッシュを足すときは同じ罠を疑う。
関連: [[mac-port-phase-status]]
