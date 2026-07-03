# HID(入力)設計ドキュメント

> 対象コミット: `144ae8a` / 最終更新: 2026-07-03

対象: `aqEngine/HID/`
関連: [Xbox移植設計](../aqEngine/Platform/Xbox移植設計.md)

キーボード・マウス・ゲームパッドの入力を扱う。プラットフォーム(Win32 / UWP / Xbox)の API 差を **バックエンド抽象**で吸収し、ゲーム側は物理入力を意識しない **ActionMap** 層で扱う。

---

## 0. 設計の全体像

**3 層構造**になっている。

```
[ゲーム側]  enum class GameAction { Jump, Fire, ... }
      │  ActionInput<GameAction>::IsTriggered(Jump)
      ▼
[アクション層]  ActionInput / ActionMap / InputBinding
      │  物理入力(キー/マウスボタン/パッドボタン)へ解決
      ▼
[デバイス層]  InputManager → KeyBoard / Mouse / Pad
      │  Pad は正規化状態(PadState)に対する判定だけを持つ
      ▼
[バックエンド層]  IPadBackend
      ├─ XInputPadBackend    (Win32 / デスクトップ)
      └─ WinRTGamepadBackend (UWP / Xbox / PC-UWP)
```

**ハイブリッドの肝**:
- **キーボード/マウス** … Win32 は DirectInput(`dinput.h`)。UWP では DirectInput/XInput が使えないため `AQ_PLATFORM_UWP` で **no-op(中立状態)にフォールバック**(将来 Phase 4 の GameInput 対応予定)。
- **ゲームパッド** … `IPadBackend` 抽象で Win32=XInput / UWP=Windows.Gaming.Input を差し替え。判定ロジック(トリガー/長押し/デッドゾーン後の状態管理)は `Pad` 側に残るためバックエンド差し替えで上位無改修。

---

## 1. デバイス層(`Input.h` / `Input.cpp`)

`Input.h` に **キーボード・マウス・パッド・InputManager・フリー関数**が集約されている。

| クラス / 要素 | 責務 |
|---|---|
| `KeyBoardType`(enum) | キー識別子。Win32 は `DIK_*` 値、UWP は中立の連番値(`now_[256]` のインデックス用)。`#if !defined(AQ_PLATFORM_UWP)` で分岐。 |
| `KeyBoard` | キーボード状態。`now_/old_`(256 バイト)で前後フレーム比較し `IsTriggered/IsPressed/IsReleased/IsLongPress` を提供。`holdTimers_` で長押し秒数を計測。UWP では `Initialize(LPDIRECTINPUT8)` を持たない。 |
| `MouseButton` / `MouseAxis`(enum) | マウスボタン(Left/Right/Middle)と軸(DeltaX/DeltaY/WheelDelta)。 |
| `MouseStateNeutral`(UWP のみ) | `DIMOUSESTATE2` の代替。既存判定コードが参照するフィールド(lX/lY/lZ/rgbButtons)だけを持つ全ゼロ中立状態。 |
| `Mouse` | マウス状態。ボタン判定 + `GetAxis`(移動/ホイール量)+ `GetCursorPos`。Win32=DirectInput、UWP=中立。 |
| `Pad` | **パッドの高レベル判定**(プラットフォーム非依存)。生取得・振動は `IPadBackend` に委譲し、本体は `PadState`(正規化済み)に対する `IsTriggered/IsPressed/IsReleased/IsLongPress/GetAxis`、`Vibrate/StopVibration` を持つ。最大 4 台。 |
| `InputManager` | **入力の統括窓口(シングルトン)**。`KeyBoard`/`Mouse`/`IPadBackend`/`Pad[4]` を所有。`Setup()` で初期化、`Update()` で全デバイス更新(高精度クロックで dt 算出)。`SuppressKeyboard/SuppressMouse`(ImGui が入力を使用中はフリー関数が false を返す抑制フラグ)。 |
| フリー関数群 | `IsKeyTriggered`/`IsMousePressed`/`IsPadTriggered`/`GetPadAxis`/`IsPadConnected` 等。`InputManager::Get()` へのグローバルアクセスをラップ。`ActionInput` の `Eval` から呼ばれる。 |

**レビュー観点**: `#if !defined(AQ_PLATFORM_UWP)` の分岐が各所にある。UWP パスで DirectInput 型が完全に排除され、中立状態でビルド・動作するか(入力なしで落ちない)。抑制フラグの適用漏れ(ImGui 使用中の誤入力)。

---

## 2. バックエンド層(パッドの API 抽象)

| ファイル | 責務 |
|---|---|
| `IPadBackend.h` | **パッド抽象**。`PadButton`(A/B/X/Y/LB/RB/LT/RT/D-Pad/Start/Back/LStick/RStick)・`PadAxis`(LX/LY/RX/RY/LTrigger/RTrigger、スティック[-1,1]・トリガー[0,1])・`PadState`(正規化済み。connected + buttons[] + axes[])を定義。IF は `Poll(index, out)` と `SetVibration(index, left, right)` の 2 つだけ。生取得と振動のみを隠蔽し、判定ロジックは `Pad` に残す設計。 |
| `PadBackend.h` | **バックエンド選択**(`SoundBackend.h` と同じ流儀)。`AQ_PLATFORM_UWP` で `using DefaultPadBackend = XInputPadBackend / WinRTGamepadBackend` を切り替える。`Pad`/`InputManager` は無改修。 |
| `XInputPadBackend.h/.cpp` | **XInput 実装**(Win32)。旧 `Pad` が直接持っていた XInput 依存(状態取得・ボタン対応表 `GetButtonState`・デッドゾーン正規化 `NormalizeAxis`・振動)を移設。 |
| `WinRTGamepadBackend.h/.cpp` | **Windows.Gaming.Input 実装**(UWP/Xbox/PC-UWP)。WinRT の `Gamepad` を使う(GDK の GameInput とは別物。UWP アプリコンテナで利用可能)。XInput は UWP で `xinput.lib` 非互換のことがあるため WinRT を使う。 |

**ハイブリッドの肝**: `PadState` が正規化済みの API 非依存構造体なので、XInput/WinRT どちらでも同じ意味論。ボタン列挙は「配列インデックスとして使う」値で、バックエンド固有ビットに依存しない。

---

## 3. アクション層(物理入力とゲームアクションの仲介)

| ファイル | 責務 |
|---|---|
| `InputBinding.h` | **1 つの物理入力バインディング**。`InputBinding{ type(Key/MouseButton/PadButton), code, padIndex }`。生成ヘルパ `BindKey/BindMouse/BindPad`。アナログスティック用 `StickBinding{ axisX, axisY, padIndex }` と `BindLStick/BindRStick`。 |
| `ActionMap.h` / `ActionMap.cpp` | **アクション ID → バインディング列**の対応表。`Bind(action, binding)`(同一アクションに複数登録可 = OR 評価)、`BindStick`、`Find`/`FindStick`。テンプレートで任意の `enum class`(uint32_t ベース)を受ける。 |
| `ActionInput.h` | **アクション問い合わせのファサード**(テンプレート `ActionInput<TAction>`)。`ActionMap*` を保持(所有しない)。`IsTriggered/IsPressed/IsReleased/IsLongPress(action)` を、バインディング列を走査して OR 評価(`Query`/`Eval`)。`GetStick(action)` は `StickBinding` から `GetPadAxis` 2 軸を読んで `Vector2` を返す。`Eval` は `InputBinding::Type` で分岐し、デバイス層のフリー関数を呼ぶ。 |

**設計意図**: ゲームロジックは `GameAction::Jump` のような意味単位だけを扱い、「Space キー or A ボタン」の対応は `ActionMap` に外出しできる(キーコンフィグ・入力デバイス切替が容易)。

---

## 4. データフロー(1 フレーム)

```
InputManager::Update()                     ... Application::Update 冒頭で駆動
  ├─ KeyBoard::Update(dt)                  ... now_→old_ シフト、DirectInput/中立から取得、holdTimers 更新
  ├─ Mouse::Update(dt)
  └─ Pad[i]::Update(dt)
        └─ backend_->Poll(i, now_)         ... XInput / WinRT から PadState を取得

ゲームロジック
  actionInput.IsTriggered(GameAction::Jump)
     └─ ActionMap::Find → InputBinding 列を OR 評価
          └─ Eval → IsKeyTriggered / IsPadTriggered ...（フリー関数）
               └─ InputManager::Get().GetKeyBoardPtr()->IsTriggered(...)
```

ImGui 連携: `Application::Update` で `io.WantCaptureKeyboard/Mouse` を `SuppressKeyboard/SuppressMouse` に反映し、UI 操作中はゲーム入力を抑制する。

---

## 5. レビュー時のチェックポイント

- [ ] **UWP フォールバックの完全性**: `AQ_PLATFORM_UWP` 時にキーボード/マウスが中立で落ちないか。DirectInput 型がリークしていないか。
- [ ] **バックエンド抽象の純度**: `Pad`/`InputManager` に XInput/WinRT 固有型が漏れていないか(`IPadBackend` に閉じているか)。
- [ ] **状態管理の所在**: トリガー/長押し判定が `Pad`(=プラットフォーム非依存)側にあり、バックエンドは Poll/Vibrate のみか。
- [ ] **デッドゾーン/正規化**: `NormalizeAxis` のデッドゾーン処理が XInput/WinRT で一貫しているか(同じ操作感)。
- [ ] **抑制フラグ**: ImGui 使用中の入力抑制が全経路(フリー関数)に効いているか。
- [ ] **将来拡張**: Phase 4 の GameInput バックエンド追加時、`IPadBackend` 追加 + `PadBackend.h` の分岐追加だけで済む構造か。
