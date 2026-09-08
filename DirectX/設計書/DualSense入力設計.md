# DualSense 入力設計

> 対象コミット: 6635a5f / 最終更新: 2026-09-08
> 対象: `aqEngine/HID/`(Win32 のみ。UWP は既存 WinRTGamepadBackend のまま)

DualSense(PS5 パッド)をネイティブ対応する。XInput エミュレータ(Steam / DS4Windows)非依存で、
入力・振動に加えて **アダプティブトリガー(L2/R2 の抵抗)** まで扱えるようにする。

## 1. 方式

**HID 直読み**。DualSense は標準 HID デバイスで、レポート形式はコミュニティで解明済み
(DS5W / SDL が同方式)。Windows.Gaming.Input の RawGameController は振動・トリガー抵抗を
出力できないため採用しない。

- 対象デバイス: VID `0x054C`(Sony)、PID `0x0CE6`(DualSense)/ `0x0DF2`(DualSense Edge)
- 列挙: SetupAPI(HID クラス)で検出し `CreateFile`(FILE_FLAG_OVERLAPPED)で開く
- 読み取り: overlapped の非ブロッキング読みをポンプし**最新レポートだけを保持**。
  `Poll()` は保持値を正規化して返す(メインスレッドの既存 Input 更新から呼ばれる)
- 切断: 読み書き失敗でクローズ → 1 秒間隔で再列挙(毎フレーム列挙はしない)

### 接続形態

| | 入力 | 出力(振動/トリガー) |
|---|---|---|
| USB | input report `0x01`(64B) | output report `0x02` |
| Bluetooth | 接続直後は簡易 `0x01`。feature report `0x05` を読むと拡張モード(`0x31`)へ移行 | output report `0x31` + **CRC32**(seed 付き。仕様は DS5W 準拠) |

P14 の必須スコープは **USB**。Bluetooth は同レポートの差分(ヘッダ+CRC)として実装は入れるが、
実機評価は USB で通ればよい(BT はベストエフォート)。

## 2. マッピング(input report 0x01 / USB オフセット)

| バイト | 内容 | → PadState |
|---|---|---|
| 1..4 | LX, LY, RX, RY(0..255) | `axes[LX..RY]`。**Y は上=0 なので符号反転**して XInput と同じ「上=+1」に揃える |
| 5, 6 | L2, R2(0..255) | `axes[LTrigger/RTrigger]`(0..1) |
| 8 下位4bit | 十字キー(ハット 0..8) | DUp/DDown/DLeft/DRight にデコード |
| 8 上位4bit | □ × ○ △ | X=□, A=×, B=○, Y=△(XInput の物理位置に合わせる) |
| 9 | L1 R1 L2 R2 Create Options L3 R3 | LB RB LT RT Back Start LStick RStick |

- スティックのデッドゾーンは XInputPadBackend と同じ考え方で正規化する(中心 128、遊び幅は実機で調整)。
- タッチパッド押下・PS ボタン・マイクは割り当て先が無いためスコープ外。

## 3. 出力(振動+アダプティブトリガー)

output report(USB `0x02`)に振動とトリガーエフェクトを毎回まとめて書く
(dirty 時のみ送信。valid フラグで有効項目を指定)。

- **振動**: motorLeft(低周波)/ motorRight(高周波)を 0..255 で指定
  (DualSense はボイスコイル駆動だが互換ランブル値が用意されている)
- **トリガー抵抗**: L2 / R2 それぞれ約 11 バイトのエフェクト領域。
  P14 で使うのは **Feedback モード(開始位置+強度の一定抵抗)** のみ。
  `strength = 0` で Off(解除)を書く。Weapon / Vibration モードは将来拡張。
  バイトレイアウトは DS5W / SDL の実装を一次資料とし、実機で確認しながら合わせる。

## 4. エンジン API と合成バックエンド

```cpp
// IPadBackend へ追加 (既定 no-op。XInput / WinRT はそのまま)
virtual void SetTriggerResistance(uint32_t index, PadAxis trigger,
                                  float startPos, float strength) {}
```

- `Pad` に同名のパススルーを追加(`SetVibration` と同じ形)。
- **スレッド境界**: ゲーム側の呼び出し元(CoinSystem / SpeedCharacterSystem)はワーカースレッドで
  走るため、`Pad` の出力系 API(振動 / トリガー抵抗)は**値を保持するだけ**にし、
  メインスレッドの入力更新(`Pad::Update`)がフレームに 1 回まとめてバックエンドへ適用する
  (dirty 時のみ送信=出力レポートの自然なレート制限にもなる)。
  振動は `Rumble(left, right, durationSec)` の形にし、残り時間の減衰と自動解除も
  メインスレッド側で行う(ワーカーからはタイマー管理不要)。
- **Win32PadBackend(合成)を新設**し、`PadBackend.h` の Win32 側 `DefaultPadBackend` を差し替える:
  - `Poll(index)`: まず XInput の同スロットを見る。未接続なら **index 番目の DualSense** を読む
  - `SetVibration` / `SetTriggerResistance`: その index を実際に担当しているバックエンドへ転送
  - XInput パッドと DualSense が同時に居る場合はスロットが XInput 優先で埋まる(既知の単純規則)

## 5. ゲーム側デモ(AquaDash。効果を体感できる最小配線)

- **速度連動のトリガー抵抗**: `SpeedCharacterSystem` が毎フレーム
  `R2 抵抗 = 速度/最高速に応じて強く`(0 速で解除)を設定。加速するほど R2 が重くなる。
- **コイン取得で短い振動**(0.1 秒程度。`CoinSystem` の取得処理から)。
- どちらもパッド未接続なら no-op。キーボード操作には影響しない。

## 6. 責務表

| ファイル | 責務 |
|---|---|
| `HID/DualSensePadBackend.{h,cpp}`(新規) | HID 列挙/読み書き・レポート解析・出力レポート構築(CRC 含む) |
| `HID/Win32PadBackend.{h,cpp}`(新規) | XInput と DualSense の合成・index 割り当て |
| `HID/IPadBackend.h` | `SetTriggerResistance` 既定実装の追加 |
| `HID/PadBackend.h` | Win32 の `DefaultPadBackend` を `Win32PadBackend` へ |
| `HID/Input.h/.cpp` | `Pad::SetTriggerResistance` パススルー |
| Game(`SpeedCharacterComponentSystem.cpp` / `CoinComponentSystem.cpp`) | デモ配線(§5) |

## 7. チェックポイント

- [ ] USB 接続の DualSense でスティック走行・×ジャンプ・L2/R2 のアナログ値が入る(要実機=ユーザー確認)
- [ ] XInput パッドを挿した場合の従来動作が変わらない(無い場合は「キーボードのみで従来どおり」で代替確認)
- [ ] コイン取得で振動する
- [ ] 速度が上がると R2 が重くなり、停止で解除される
- [ ] パッド未接続・抜き差しでクラッシュせず、再接続で復帰する
- [ ] (ベストエフォート)Bluetooth 接続でも入力が入る
