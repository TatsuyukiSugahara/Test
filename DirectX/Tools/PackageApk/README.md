# PackageApk — Android の APK を作る(Gradle 不使用)

`libGame.so` から、実機にインストールできる署名済み APK を 1 コマンドで作るツール。
Android 移植の P1(実機でクリア画面)用。設計は
[設計書/Android移植設計.md](../../設計書/Android移植設計.md)。

| もの | 場所 |
|---|---|
| スクリプト | `Tools/PackageApk/package_apk.ps1` |
| マニフェスト | `Android/AndroidManifest.xml` |
| 出力(Debug) | `build/android-arm64/AquaDash-debug.apk` |

---

## 1. 前提

- **ネイティブは先に CMake でビルドしておく**。このツールはビルドしない。
  手順は [Tools/SetupCMake/README.md](../SetupCMake/README.md) の 6 章。

  ```powershell
  $env:ANDROID_NDK_HOME = "C:\Users\<user>\AndroidNDK\android-ndk-r27c"
  $vs = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake"
  $env:PATH = "$vs\Ninja;" + $env:PATH
  & "$vs\CMake\bin\cmake.exe" --preset android-arm64
  & "$vs\CMake\bin\cmake.exe" --build --preset android-arm64-debug
  ```

- Android SDK の build-tools と JDK。この開発機での既定値はスクリプトのパラメータに
  埋めてあるので、通常は何も指定しなくてよい。

  | もの | この開発機での場所 | 使うもの |
  |---|---|---|
  | SDK build-tools 36.0.0 | `C:\Program Files (x86)\Android\android-sdk\build-tools\36.0.0` | `aapt2.exe` / `zipalign.exe` / `apksigner.bat` |
  | SDK platform | 同上 `platforms\android-34\android.jar` | `aapt2 link` のリンク先 |
  | SDK platform-tools | 同上 `platform-tools\adb.exe` | インストールと logcat |
  | JDK 17 | `C:\Program Files (x86)\Android\openjdk\jdk-17.0.14` | `keytool.exe`(鍵生成)、`apksigner` が使う JRE |

- **Gradle と Android Studio は不要**(2 章)。

---

## 2. なぜ Gradle を使わないのか

設計書は Gradle 前提だが、このツールは **Gradle を使わない**。

- この開発機に Gradle が入っておらず、Gradle を導入すると初回ビルドで
  ディストリビューションと依存を**ネットワークから取りに行く**。
  オフラインで完結させたいので避けた。
- このアプリは **Java/Kotlin のコードを 1 行も持たない**。`android.app.NativeActivity`
  をそのまま使い、マニフェストに `android:hasCode="false"` を書いているので
  `classes.dex` が不要。R クラスも要らない(リソースは framework 参照のみ)。
- その結果 Gradle に残る仕事は「zip を作って整列して署名する」だけで、
  それは `aapt2` / `zipalign` / `apksigner` を順に呼べば足りる。

将来 `GameActivity`(`androidx.games`)や Paddleboat、Oboe の AAR を使うなら
AAR の取り込みが必要になるので、そのときは Gradle へ移す。

---

## 3. 使い方

既定値のまま実行すれば Debug の APK ができる。

```powershell
powershell -ExecutionPolicy Bypass -File Tools\PackageApk\package_apk.ps1
```

主なパラメータ:

| パラメータ | 既定 | 意味 |
|---|---|---|
| `-Config` | `Debug` | `Debug` のときだけ `aapt2 link --debug-mode` で `android:debuggable="true"` を注入する |
| `-Abi` | `arm64-v8a` | APK 内の `lib/<abi>/` と、`.so` を探すビルドディレクトリ名を決める |
| `-SoPath` | `build\android-<abi>\lib\<Config>\libGame.so` | 同梱するネイティブライブラリ |
| `-OutApk` | `build\android-<abi>\AquaDash-<config>.apk` | 出力 APK |
| `-AssetsDir` | (未指定) | APK の `assets/` へ入れるディレクトリ。**P1 では使わない**(`.spv` とアセットは P2) |
| `-IncludeValidationLayer` | オフ | Vulkan 検証レイヤを同梱する(5 章の制約あり) |
| `-ValidationLayerPath` | (自動探索) | 検証レイヤ `.so` を明示指定する |
| `-Keystore` | `%USERPROFILE%\.android\debug.keystore` | 署名鍵。無ければ `keytool` で作る |
| `-AndroidSdkRoot` / `-BuildToolsVersion` / `-AndroidPlatform` / `-JdkRoot` | この開発機の値 | ツールの場所 |

処理は次の順に進み、途中で失敗したらその場で止まる(中間物は
`<OutApk>.work\` に残るので中身を確認できる。成功時は消える)。

1. デバッグ用 keystore の確認 / 生成(`keytool -genkeypair`、パスワードと別名は
   Android SDK の慣例どおり `android` / `androiddebugkey`)
2. `aapt2 link` で `AndroidManifest.xml` から素の APK を作る
3. `lib/<abi>/libGame.so`(と検証レイヤ)を zip エントリとして追加
4. `zipalign -p -f 4`
5. `apksigner sign` → `apksigner verify --verbose --print-certs`
6. `aapt2 dump badging` で中身を表示

### 生成物

```
build/android-arm64/AquaDash-debug.apk      約 22 MB(Debug の .so 84MB を圧縮)
```

APK の中身は 4 ファイルだけ:

```
AndroidManifest.xml
resources.arsc
lib/arm64-v8a/libGame.so
META-INF/...             (署名)
```

---

## 4. 実機へのインストールと確認

```powershell
$adb = "C:\Program Files (x86)\Android\android-sdk\platform-tools\adb.exe"

# 端末の確認(初回は端末側で「USB デバッグを許可」を承認する)
& $adb devices

# インストール(-r は再インストール。署名が違う場合は先にアンインストールする)
& $adb install -r build\android-arm64\AquaDash-debug.apk

# 起動(ランチャーから叩いてもよい)
& $adb shell am start -n com.aqengine.aquadash/android.app.NativeActivity
```

ログは logcat で見る。`DebugOutputAndroid.cpp` は `__android_log_print` に
タグ `AquaDash` で出すので、まずはそれと Vulkan / クラッシュ関連を拾う。

```powershell
# 事前にバッファを消してから流す
& $adb logcat -c
& $adb logcat -v time AquaDash:V ActivityManager:I DEBUG:V libc:V vulkan:V VALIDATION:V *:S

# ネイティブクラッシュのスタックを解決する(NDK の ndk-stack)
& $adb logcat | & "$env:ANDROID_NDK_HOME\ndk-stack.cmd" -sym build\android-arm64\lib\Debug
```

アンインストール:

```powershell
& $adb uninstall com.aqengine.aquadash
```

---

## 5. 既知の制約

- **Vulkan 検証レイヤは同梱できていない。** NDK r27c / r23c と Windows 版 Vulkan SDK の
  どこにも `libVkLayer_khronos_validation.so`(aarch64)が無い
  (新しい NDK は検証レイヤを同梱しなくなった)。`-IncludeValidationLayer` を付けても
  警告を出して先へ進む。使うには Vulkan-ValidationLayers のリリース(`android-binaries`)から
  取得し `-ValidationLayerPath` で渡す。
- **ABI は 1 つずつ。** マルチ ABI の APK は作らない。`-Abi` を変えて個別に作る。
- **アイコンとリソースが無い。** ランチャーには既定アイコンで並ぶ。`res/` を持たせるには
  `aapt2 compile` の工程を足す必要がある(P6 のパッケージング整備で検討)。
- **Release もデバッグ鍵で署名する。** `-Config Release` は警告を出すだけ。
  配布用の鍵は P6 の作業。
- **`-AssetsDir` は受け口だけ。** P1 では絵を出すのに必要なアセットが無いので使わない。
  中では `aapt2 link -A` へ渡すだけなので、P2 で `.spv` とアセットを渡せばそのまま入る。
  ただし **Windows の aapt2 はサブディレクトリの区切りを `\` のまま zip エントリ名にする**
  (`assets/spv\dummy.spv` になる)。`AAssetManager` はエントリ名を `/` 区切りで引くので、
  P2 でサブフォルダごとアセットを入れるならここを回避する必要がある
  (フラットに置く / `-A` を使わず自前で zip エントリを足す、のどちらか)。
- **`.so` は圧縮して入れている。** マニフェストが `extractNativeLibs="true"` を宣言し、
  インストール時に展開される前提のため。`zipalign -p` のページ整列は非圧縮 `.so` 向けの
  指示なので、この構成では効いていない(将来 `extractNativeLibs="false"` にするなら
  `.so` を無圧縮で入れる方へ変える)。
- **設計書とのずれ**: 設計書は `DirectX/Android/app/src/main/AndroidManifest.xml`(Gradle の
  ディレクトリ構成)を想定しているが、Gradle を使わないので `DirectX/Android/AndroidManifest.xml`
  に 1 枚だけ置いている。
