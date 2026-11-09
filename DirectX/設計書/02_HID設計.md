# HID(入力)設計ドキュメント

> 対象コミット: `92b6c54` / 最終更新: 2026-11-09

> ⚠ 鮮度(2026-11-09 確認): 本文は `aqEngine/HID` の**ヘッダと `Input.cpp`** を現行コードと突き合わせて更新済み。各バックエンドの .cpp の中身(キー変換表・仮想パッドの当たり判定とスティック計算・DualSense のレポート処理・GameController の写像・各 Sink の実装)は未読で、該当記述は TODO を付けてある。

対象: `aqEngine/HID/`
関連: [Xbox移植設計](Xbox移植設計.md)

キーボード・マウス・タッチ・ゲームパッドの入力を扱う。プラットフォーム(Win32 / UWP / Xbox / Mac / iOS / Android)の API 差を **バックエンド抽象**で吸収し、ゲーム側は物理入力を意識しない **ActionMap** 層で扱う。

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
[デバイス層]  InputManager → KeyBoard / Mouse / Pad[4]  (+ タッチ点 TouchState)
      │  各デバイスは正規化状態(KeyboardState / MouseState / PadState)に対する判定だけを持つ
      ▼
[バックエンド層]  IKeyboardBackend / IMouseBackend / IPadBackend / ITouchBackend
      └─ 実装はプラットフォームごとに選択(§2 の表。選択は *Backend.h)
```

**ハイブリッドの肝**:
- **デバイスごとにバックエンド抽象を持つ**。生のデバイス取得(と振動)だけをバックエンドが隠蔽し、トリガー/長押し/押下前フレーム比較といった判定ロジックは `KeyBoard` / `Mouse` / `Pad` 側に残る。バックエンドを差し替えても上位は無改修。
- **バックエンドの選択はヘッダ側**(`KeyboardMouseBackend.h` / `PadBackend.h` / `TouchBackend.h`)で `AQ_PLATFORM_*` により `using Default*Backend = ...` を切り替える。`InputManager` に `#if` を持ち込まないよう、組み立てが要る環境向けにファクトリ(`CreateDefaultPadBackend` / `CreateDefaultMouseBackend`)もヘッダ側に置く。
- **タッチ環境(Android / iOS)** は、タッチを「仮想パッド(パッドへ合成)」と「ポインタ(マウスへ合成)」の 2 経路へ流し、ゲーム側は入力ソースを区別しない。
- **OS のイベントを溜めて読む方式(Mac / Android / iOS)** は、プラットフォーム層が `*InputSink` へ投入し、バックエンドはそれを読むだけ(Sink は `IPlatform` にもバックエンド IF にも露出しない)。

---

---

## 1. デバイス層(`Input.h` / `Input.cpp`)

`Input.h` に **キーボード・マウス・パッド・InputManager・フリー関数**が集約されている。キー/マウス/パッド/タッチの**状態型と IF は各 `I*Backend.h` に分離**され、`Input.h` はそれらを include する(`KeyBoardType`・`KeyboardState`・`MouseState`・`PadButton/PadAxis/PadState`・`TouchPoint/TouchState`)。

| クラス / 要素 | 責務 |
|---|---|
| `KeyBoardType`(enum、`IKeyboardBackend.h`) | キー識別子。**バックエンド非依存の連番**(Left/Right/Up/Down/W/A/S/D/G/Space/Enter/Escape/Num1〜Num4)。`DIK_*` 等のスキャンコードには依存せず、対応付けは各バックエンドの変換表が持つ。 |
| `KeyboardState`(`IKeyboardBackend.h`) | `keys[256]`(`KeyBoardType` 順、押下 = 0x80)。 |
| `KeyBoard` | `SetBackend` + `Update(dt, blocked)`。`now_/old_`(`KeyboardState`)の前後比較で `IsTriggered/IsPressed/IsReleased/IsLongPress` を提供。`holdTimers_` で長押し秒数を計測。`blocked` なら取得結果を全キー離しに置き換えてから判定。 |
| `MouseState`(`IMouseBackend.h`) | `DIMOUSESTATE2` の中立版。`dx/dy/wheel`・`buttons[8]`(押下 = 0x80)・`cursorX/Y`(クライアント左上原点)。 |
| `MouseButton` / `MouseAxis`(enum) | マウスボタン(Left/Right/Middle)と軸(DeltaX/DeltaY/WheelDelta)。 |
| `Mouse` | `SetBackend` + `Update(dt, blocked)`。ボタン判定 + `GetAxis` + `GetCursorPos`。`blocked` なら全ボタン離し・移動 0 に置き換える。 |
| `TouchPoint` / `TouchState`(`ITouchBackend.h`) | タッチ 1 点(`id`・`x/y`・`pressed`)と集合(最大 `MAX_POINT_COUNT` = 10 点、`count` つき)。座標は Mouse の `cursorX/Y` と同じ「クライアント左上原点・ピクセル」。離した瞬間のフレームだけ `pressed = false` で 1 回返る。 |
| `Pad` | **パッドの高レベル判定**(プラットフォーム非依存)。`PadState` に対する `IsTriggered/IsPressed/IsReleased/IsLongPress/GetAxis`。出力は (a) 旧 API `Vibrate/StopVibration`(バックエンド直叩き)と (b) **出力バッファ API**(`Rumble(left,right,durationSec)`・`SetTriggerResistance(trigger,startPos,strength)`・`DiscardRumble`)。(b) は値の atomic への積み込みだけでワーカースレッドから呼べ、バックエンドへの送信は `Update` 内の `ApplyOutput` がメインスレッドで差分のみ行う(`durationSec` 経過で自動停止)。最大 4 台(`MAX_PAD_COUNT`)。 |
| `InputManager` | **入力の統括窓口(シングルトン)**。キーボード/マウス/タッチ/パッドの各バックエンド(`unique_ptr`)と `KeyBoard`/`Mouse`/`Pad[4]` を所有。`Setup()` でキーボード/マウスのバックエンドを生成・初期化(`InputWindowDesc` を `Engine` から組み立てる)。タッチとパッドのバックエンドはコンストラクタで生成。`Update()` で全デバイスを更新(高精度クロックで dt 算出)。`GetTouchState()`(生のタッチ点)/ `GetPointerTouchState()`(パッドが使っていない指だけ)。抑制は後述(ImGui 用 `SuppressKeyboard/SuppressMouse` と、フォーカス外用 `SetFocusBlocked` の 2 系統)。 |
| `InputWindowDesc`(`InputWindowDesc.h`) | キーボード/マウスのバックエンドへ渡すウィンドウ情報。`clientWindow`(描画先・座標変換の基準)/ `cooperativeWindow`(DirectInput の協調レベル設定先)/ `background`(true で `DISCL_BACKGROUND`)。エディタへ埋め込んだ子ウィンドウは DirectInput の協調ウィンドウにできないため、埋め込み時は別の非表示トップレベルウィンドウを協調用に渡す(`Engine::GetInputCooperativeWindow` / `IsEmbedded`)。 |
| `MultiTouchDoubleTapDetector`(`TouchGesture.h`) | 複数指(既定 4 本)のダブルタップ検出(ヘッダのみ)。キーボードも中クリックも無いスマホで、デバッグ UI の表示切替に使う(`AppHost` が `GetTouchState()` を渡して毎フレーム `Update`)。タッチを持たない環境では `count` が常に 0 のため必ず false。 |
| フリー関数群 | `IsKeyTriggered`/`IsMousePressed`/`IsPadTriggered`/`GetPadAxis`/`IsPadConnected` 等。`InputManager::Get()` へのグローバルアクセスをラップ。`ActionInput` の `Eval` から呼ばれる。ImGui 抑制中は false/0 を返す(キー/マウス)。 |

**抑制(入力停止)は 2 系統**:
- **ImGui 由来**: `Application::Update` 相当の `AppHost::Update` が `io.WantCaptureKeyboard/Mouse` を `SuppressKeyboard/SuppressMouse` に反映(`AQ_IMGUI` ビルド時)。フリー関数が false/0 を返す。
- **フォーカス外**: `SetFocusBlocked(bool)`(`atomic<bool> focusBlocked_`)。`AppHost` は**埋め込み時のみ** `!HasInputFocus()` を毎フレーム渡す(通常起動はフォーカス外でもパッドを読む従来挙動)。立っている間の `Update` はキーボード/マウス/全パッドを中立値として扱い(`Pad` は `connected` だけ実値を残し、Released を落とさない)、振動・トリガー抵抗の強度を 0 で送る。入るとき・明けるときに `DiscardRumble` で保留中の振動を捨てる。

**レビュー観点**: UWP / Mac / iOS / Android で Null バックエンドや合成バックエンドに切り替わったとき、入力なしで落ちないか。抑制の適用漏れ(ImGui 使用中・フォーカス外の誤入力)。`InputManager` のメンバ宣言順(バックエンド → `KeyBoard/Mouse/Pad`、タッチ → パッド)が破棄順の前提になっている(生ポインタ参照のため)。

---

## 2. バックエンド層(デバイスの API 抽象)

### 2.1 抽象 IF

| ファイル | 責務 |
|---|---|
| `IKeyboardBackend.h` | `Initialize(const InputWindowDesc&)` と `Poll(KeyboardState&)`。`KeyBoardType` / `KeyboardState` もここで定義。 |
| `IMouseBackend.h` | `Initialize(const InputWindowDesc&)` と `Poll(MouseState&)`。`MouseState` もここで定義。 |
| `IPadBackend.h` | **パッド抽象**。`PadButton`(A/B/X/Y/LB/RB/LT/RT/D-Pad/Start/Back/LStick/RStick)・`PadAxis`(LX/LY/RX/RY/LTrigger/RTrigger、スティック[-1,1]・トリガー[0,1])・`PadState`(正規化済み。connected + buttons[] + axes[])を定義。IF は `Poll(index, out)` と `SetVibration(index, left, right)`(純粋仮想)に加え、既定実装つきの `SetTriggerResistance`(アダプティブトリガー。既定 no-op。`strength = 0` で解除)と `IsTouchConsumed(touchId)`(その指をパッド操作に使っているか。既定 false)。 |
| `ITouchBackend.h` | `Poll(TouchState&)`。仮想スティックの当たり判定や UI のタップ判定は上位(`VirtualPadBackend` / UI 層)に残す。 |

### 2.2 バックエンドの選択(`KeyboardMouseBackend.h` / `PadBackend.h` / `TouchBackend.h`)

`SoundBackend.h` と同じ流儀で、`AQ_PLATFORM_*` により `Default*Backend` を切り替える。未対応プラットフォームは `#error`(タッチのみ既定が Null)。

| プラットフォーム | キーボード / マウス | パッド | タッチ |
|---|---|---|---|
| Win32 | `DirectInputKeyboardBackend` / `DirectInputMouseBackend` | `Win32PadBackend`(XInput 優先 + 空きスロットへ DualSense) | `NullTouchBackend` |
| UWP(Xbox / PC-UWP) | `NullKeyboardBackend` / `NullMouseBackend` | `WinRTGamepadBackend` | `NullTouchBackend` |
| Mac | `CocoaKeyboardBackend` / `CocoaMouseBackend` | `GameControllerPadBackend` | `NullTouchBackend` |
| Android | `NullKeyboardBackend` / `TouchMouseBackend` | `CompositePadBackend`(`AndroidPadBackend` + `VirtualPadBackend`) | `AndroidTouchBackend` |
| iOS | `NullKeyboardBackend` / `TouchMouseBackend` | `CompositePadBackend`(`GameControllerPadBackend` + `VirtualPadBackend`) | `iOSTouchBackend` |

**ファクトリ**: `CreateDefaultPadBackend(const TouchState*)`(Android/iOS は物理 → 仮想の順で `CompositePadBackend` を組み、それ以外は `DefaultPadBackend` をそのまま生成)と `CreateDefaultMouseBackend(const TouchState* pointerTouch)`(Android/iOS は `TouchMouseBackend` に**パッドが使っていない指の集合**を渡し、それ以外は `DefaultMouseBackend`)。`InputManager` は `#if` を持たない。

### 2.3 実装ファイル

| ファイル | 責務 |
|---|---|
| `Win32/DirectInputKeyboardBackend.h/.cpp`<br>`Win32/DirectInputMouseBackend.h/.cpp` | **DirectInput 実装**(Win32 専用)。デバイス生成・協調レベル設定(`InputWindowDesc`)・状態取得・再 Acquire。キーボードは DIK → `KeyBoardType` の変換もここ。マウスは相対軸モードと `::GetCursorPos` + `ScreenToClient` によるカーソル座標。 |
| `NullKeyboardBackend.h` / `NullMouseBackend.h` / `NullPadBackend.h` / `NullTouchBackend.h` | **Null バックエンド**。常に全ゼロ(パッドは未接続)を返す。UWP のキーボード/マウス、タッチの無い環境のタッチで使う。`NullPadBackend` は「実装が揃うまでの足場」で、現行の `PadBackend.h` の選択には使われていない(Mac / iOS は `GameControllerPadBackend` へ移行済み)。 |
| `XInputPadBackend.h/.cpp` | **XInput 実装**(Win32)。状態取得・ボタン対応表 `GetButtonState`・デッドゾーン正規化 `NormalizeAxis`・振動。 |
| `DualSensePadBackend.h/.cpp` | **DualSense(PS5 パッド)の HID 直読み**(Win32 専用。SetupAPI / hid.dll)。入力・振動に加え**アダプティブトリガー**(L2/R2 の抵抗: 開始位置 + 一定強度)まで扱う。USB が主対象で、Bluetooth はベストエフォート(実機未確認とヘッダに明記)。最大 4 台。デバイスを定期的に列挙し、読みは OVERLAPPED による非ブロッキング(最新レポートだけを保持)、書きも非同期(OVERLAPPED とバッファは `Device` が保持)。出力は dirty のときだけ送る。TODO: レポートの具体的なバイト配置・列挙間隔は .cpp 未確認。 |
| `Win32PadBackend.h/.cpp` | **Win32 の合成**。`XInputPadBackend` と `DualSensePadBackend` を持ち、スロットは XInput 優先で埋め、XInput が未接続のスロットへ同じ番号の DualSense を割り当てる。振動/トリガー抵抗は、直近の `Poll` でそのスロットを担当したバックエンド(`Owner`)へ転送。 |
| `WinRTGamepadBackend.h/.cpp` | **Windows.Gaming.Input 実装**(UWP/Xbox/PC-UWP)。WinRT の `Gamepad` を使う(GDK の GameInput とは別物。UWP アプリコンテナで利用可能)。XInput は UWP で `xinput.lib` 非互換のことがあるため WinRT を使う。振動のみ(アダプティブトリガーなし)。 |
| `Mac/CocoaInputSink.h/.cpp` | **Mac のキーボード/マウスのイベント溜め**(シングルトン)。投入側は `PlatformMac::PumpEvents`(`OnKey`/`OnModifierFlagsChanged`/`OnMouseButton`/`OnMouseMove`/`OnScrollWheel`/`OnFocusLost`)、取得側は `FetchKeyboard`/`FetchMouse`。Cocoa 依存値は投入側で解決済み(ヘッダに ObjC は現れない)。**押下の取りこぼし対策**として「前回の取得以降に一度でも押された」を保持し、その回は押下として返す(1 フレームが長引いたとき押して離すまで進んでも Triggered が成立する)。相対量は整数部のみ渡して小数部を持ち越す。Command 押下中は macOS が keyUp を配送しないため、Command が離された時点で押下状態を一掃する。スレッドはゲームスレッド 1 本でロックなし。 |
| `Mac/CocoaKeyboardBackend.h/.cpp`<br>`Mac/CocoaMouseBackend.h/.cpp` | `CocoaInputSink` を読むだけの薄いバックエンド。`Initialize` はウィンドウを見ず常に成功(ヘッダ記述)。カーソルのビュー座標変換と Y 反転は NSView を持つ `PlatformMac` 側で済んでいる。TODO: Carbon キーコード → `KeyBoardType` の変換表は .cpp 未確認。 |
| `Apple/GameControllerPadBackend.h/.mm` | **GameController.framework 実装**(Apple 共通 = macOS / iOS)。`GCController.controllers` の並び順を index に対応させ、`extendedGamepad` を `PadState` へ正規化する。Xbox / DualShock / DualSense / Joy-Con は OS が同プロファイルへ正規化するので HID 直読みは不要。**振動は no-op**(`SetVibration` に `TODO(P4)`: `GCDeviceHaptics` + `CHHapticEngine`)。**アダプティブトリガーは DualSense のみ有効**(`GCDualSenseGamepad` 判定で、それ以外は無視)。ゲームスレッド(iOS はメインスレッド)1 本から呼ぶ。TODO: ボタン/軸の写像の詳細は .mm 未確認。 |
| `Android/AndroidInputSink.h/.cpp` | **Android のタッチ/物理パッドのイベント溜め**(シングルトン)。投入側は `PlatformAndroid`(`OnTouchDown/Move/Up/Cancel`・`OnPadButton`・`OnPadAxis`・`OnFocusLost`)、取得側は `FetchTouch`/`FetchPad`。取りこぼし対策(`pressedSinceFetch`)は Cocoa と同じ考え方。指が離れても次の取得まではスロットを残す(`pressed = false` を 1 回返すため)。ハットスイッチ由来の十字キーはキー由来と別に持って OR、トリガーは `AXIS_LTRIGGER/RTRIGGER` と `AXIS_BRAKE/GAS` の大きい方を採る。スティックは `XInputPadBackend::NormalizeAxis` と同じ式のデッドゾーン処理で正規化。NDK に接続問い合わせ API が無いため、パッドのイベントを一度でも受けたら接続とみなす。 |
| `Android/AndroidPadBackend.h/.cpp` | `AndroidInputSink::FetchPad` を読む物理コントローラ。**index 0 だけ実体、1 以上は未接続**(NDK でデバイス別にスロットを分ける API が無いため)。振動は no-op(NDK に API が無く JNI が必要)。 |
| `Android/AndroidTouchBackend.h/.cpp` | `AndroidInputSink::FetchTouch` を読むだけ。`AMotionEvent` の座標はウィンドウ内ピクセルなので変換不要。 |
| `iOS/iOSInputSink.h/.cpp` | **iOS のタッチのイベント溜め**(シングルトン)。投入側は `AqMetalView` の `touches*:withEvent:`(`OnTouchBegan/Moved/Ended/Cancelled`)、取得側は `FetchTouch`。UIKit は整数 ID を渡さないため `UITouch*` のアドレスを同一性の鍵(`touchKey`)にするだけで、retain も逆参照もしない。上位へ返す id は**スロットの添字**(アドレスは 64bit で再利用されうるため外へ出さない)。`OnTouchCancelled` は全点を解放する(忘れると指が張り付く)。キーボード/パッドは扱わない。**スレッドはメインスレッド 1 本でロックなし**(`CADisplayLink` 駆動が前提。別スレッド駆動を入れるなら排他が要る、とヘッダに明記)。 |
| `iOS/iOSTouchBackend.h/.cpp` | `iOSInputSink::FetchTouch` を読むだけ。`contentsScale = 1.0` 固定で 1 ポイント = 1 ピクセルのため座標変換は不要(ヘッダ記述)。 |
| `VirtualPadBackend.h/.cpp` | **タッチを仮想パッドに見せる**パッドバックエンド(Android / iOS 共有・プラットフォーム非依存)。取り込み済みの `TouchState` を読み(`ITouchBackend` を直接叩かない: イベント駆動のラッチが取得で消費されるため)、`Layout`(正規化中心 + 画面高さ比の半径の円: 左スティック・A・B・Start)との当たり判定で `PadState` へ変換する。左スティックを掴んだ指の id を保持。`IsTouchConsumed` で操作に使った指を上位へ伝える。描画は呼び出し側(`AppHost` が `GetLayout()` を読んで表示)。振動/トリガーは no-op。TODO: スティックの計算式・ボタン判定は .cpp 未確認。 |
| `CompositePadBackend.h/.cpp` | **複数バックエンドの合成**(プラットフォーム非依存)。`Add` した順に保持し、ボタンは OR・軸は絶対値の大きい方・いずれかが接続なら接続扱い(「最後に入力があった方」方式は状態を持つので採らない)。`SetVibration`/`SetTriggerResistance` は束ねた各バックエンドへ渡す想定。`IsTouchConsumed` はどれか 1 つでも true なら true。TODO: 出力転送の細部は .cpp 未確認。 |
| `TouchMouseBackend.h/.cpp` | **タッチをポインタ(マウス)に見せる**(Android / iOS 共有)。渡された `TouchState`(パッドが使っていない指だけ)の 1 本目をカーソル、触れている間を左ボタン押下として `MouseState` に変換。物理マウス想定ではなく、タッチを既存のポインタ経路(UIInputSystem / ImGui)へ載せる合成で、ImGui 抑制(`SuppressMouse`)もそのまま効く。指が無いフレームは直前のカーソル位置を返し続ける。 |

**ハイブリッドの肝**: `PadState` が正規化済みの API 非依存構造体なので、どのバックエンドでも同じ意味論。ボタン列挙は「配列インデックスとして使う」値で、バックエンド固有ビットに依存しない。

---

## 3. アクション層(物理入力とゲームアクションの仲介)

| ファイル | 責務 |
|---|---|
| `InputBinding.h` | **1 つの物理入力バインディング**。`InputBinding{ type(Key/MouseButton/PadButton), code, padIndex }`。生成ヘルパ `BindKey/BindMouse/BindPad`。アナログスティック用 `StickBinding{ axisX, axisY, padIndex }` と `BindLStick/BindRStick`。 |
| `ActionMap.h` / `ActionMap.cpp` | **アクション ID → バインディング列**の対応表。`Bind(action, binding)`(同一アクションに複数登録可 = OR 評価)、`BindStick`/`ClearStick`、`Find`/`FindStick`。テンプレートで任意の `enum class`(uint32_t ベース)を受ける。 |
| `ActionInput.h` | **アクション問い合わせのファサード**(テンプレート `ActionInput<TAction>`)。`ActionMap*` を保持(所有しない)。`IsTriggered/IsPressed/IsReleased/IsLongPress(action)` を、バインディング列を走査して OR 評価(`Query`/`Eval`/`EvalLongPress`)。`GetStick(action)` は `StickBinding` から `GetPadAxis` 2 軸を読んで `Vector2` を返す。`Eval` は `InputBinding::Type` で分岐し、デバイス層のフリー関数を呼ぶ。 |

**設計意図**: ゲームロジックは `GameAction::Jump` のような意味単位だけを扱い、「Space キー or A ボタン」の対応は `ActionMap` に外出しできる(キーコンフィグ・入力デバイス切替が容易)。タッチ環境では仮想パッドが `PadButton` として見えるので、ActionMap 側の変更なしにタッチ操作が通る。

---

## 4. データフロー(1 フレーム)

```
(プラットフォーム層)  PumpEvents 等が OS のイベントを *InputSink へ投入
                      (Mac: CocoaInputSink / Android: AndroidInputSink / iOS: iOSInputSink)

AppHost::Update                            ... ImGui 抑制 / 埋め込み時のフォーカス外停止を反映
  ├─ SuppressKeyboard/Mouse(io.WantCapture*)   (AQ_IMGUI 時)
  └─ SetFocusBlocked(!HasInputFocus())         (埋め込み時のみ)

InputManager::Update()
  ├─ touchBackend_->Poll(touch_)           ... タッチはパッドより先に取り込む(仮想パッドが同フレームの値を読むため)
  ├─ KeyBoard::Update(dt, blocked)         ... now_→old_ シフト、バックエンドから取得、holdTimers 更新
  ├─ Pad[i]::Update(dt)                    ... Poll → フォーカス外なら中立化 → ApplyOutput(振動/トリガー抵抗の差分送信)
  ├─ BuildPointerTouchState()              ... touch_ から IsTouchConsumed な指を除いて pointerTouch_ を作る
  └─ Mouse::Update(dt, blocked)            ... パッドの後。TouchMouseBackend は pointerTouch_ を読む

ゲームロジック
  actionInput.IsTriggered(GameAction::Jump)
     └─ ActionMap::Find → InputBinding 列を OR 評価
          └─ Eval → IsKeyTriggered / IsPadTriggered ...（フリー関数）
               └─ InputManager::Get().GetKeyBoardPtr()->IsTriggered(...)
```

ワーカースレッド(ゲーム側システム)からの振動は `Pad::Rumble` / `SetTriggerResistance` が値を積み、メインスレッドの `Pad::Update` が送信する。

---

## 5. レビュー時のチェックポイント

- [ ] **Null / 合成バックエンドの完全性**: UWP(キーボード/マウス Null)、Android/iOS(キーボード Null)で落ちないか。DirectInput / XInput / HID 型が Win32 専用ヘッダの外へ漏れていないか(各ヘッダは `AQ_PLATFORM_*` ガード)。
- [ ] **バックエンド抽象の純度**: `Pad`/`InputManager` にプラットフォーム固有型が漏れていないか(`I*Backend` に閉じているか)。
- [ ] **状態管理の所在**: トリガー/長押し判定が `KeyBoard`/`Mouse`/`Pad`(=プラットフォーム非依存)側にあり、バックエンドは Poll/出力のみか。
- [ ] **デッドゾーン/正規化**: XInput / WinRT / Android(`NormalizeStick`)/ GameController で同じ操作感か(式の一貫性。Android は XInput と同式と明記されているが、他は .cpp 未確認)。
- [ ] **抑制フラグ**: ImGui 使用中・フォーカス外の入力停止が全経路(フリー関数・パッド出力)に効いているか。
- [ ] **押下の取りこぼし**: Cocoa / Android / iOS の各 Sink で、ヒッチ時に押して離すまで進んだ入力が 1 回 Triggered になるか。
- [ ] **タッチの指の取り合い**: 仮想パッドが掴んだ指が `IsTouchConsumed` 経由でポインタ(UI / ImGui)へ回っていないか。`InputManager::Update` の順序(タッチ → パッド → ポインタ構築 → マウス)が崩れていないか。
- [ ] **スレッド前提**: Mac / Android の Sink はゲームスレッド 1 本、iOS はメインスレッド 1 本でロックなし。フレーム駆動や配送スレッドを変えるときは排他を足す。
- [ ] **将来拡張**: Phase 4 の GameInput バックエンド追加時、`IPadBackend` 追加 + `PadBackend.h` の分岐追加だけで済む構造か。UWP のキーボード/マウス(Null)の実入力化。
- [ ] **未実装の出力**: `GameControllerPadBackend` の振動(`TODO(P4)`)、`AndroidPadBackend` の振動(JNI 要)、Android の複数パッド(index 1 以降は未接続)。
