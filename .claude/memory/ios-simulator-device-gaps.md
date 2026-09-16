---
name: ios-simulator-device-gaps
description: iOS シミュレータの GPU 能力が実機より低く、3 つの機能で劣化経路を通っている。実機確認は必須
metadata:
  type: project
---

iOS シミュレータの GPU は **family Apple2 / read-write テクスチャ Tier 0 / BC 非対応**で、
実機(A14 以降 = Apple7、iOS 16.4 以降)よりかなり低い。P2 で 3 つ踏んだ。

| 機能 | 要件 | シミュレータ | 非対応時 | 対処(実装済み) |
|---|---|---|---|---|
| サンプラのボーダーカラー | Apple7 / Mac2 | 不可 | **Validation がアサートで即死** | `metal::IsSamplerBorderColorSupported()` → `ClampToEdge` |
| read-write テクスチャ(compute) | **Tier2**(HDR RGBA16Float を読み書き) | Tier 0 | 同じく**アサート即死** | tier 実測 → `SetComputeSupported(false)`(ポストプロセス無し) |
| BC 圧縮テクスチャ | `supportsBCTextureCompression` | 不可 | テクスチャが**静かに全滅** | `IsBlockCompressionSupported()` → RGBA8 へ展開 |

いずれも `MetalGraphicsDeviceImpl::Initialize` で実測してログへ出し、フラグへ流している。
**Mac(Apple9 / Tier2)は全部対応ありなので挙動は変わらない。**

**How to apply:** 「シミュレータで動いたから大丈夫」と判断しないこと。
実機では 3 つとも本来の経路を通るはずで、**とくに compute が有効になると
ポストプロセス一式が初めて iOS で走る**ので、そこで新しい問題が出る可能性がある。
P5(実機)の必須確認項目。設計書は `iOS移植設計.md` §4.7 が一次資料。
関連: [[ios-port-phase-status]] [[ios-simulator-workflow]]
