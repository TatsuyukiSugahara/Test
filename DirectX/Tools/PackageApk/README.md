# PackageApk — Android の APK を作る(Gradle 不使用)

`libGame.so` とアセットから、実機にインストールできる署名済み APK を 1 コマンドで作るツール。
Android 移植の P1(実機でクリア画面)/ P2(実シーン)用。設計は
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
| `-AssetsDir` | `Game\Assets` | APK へ同梱するアセットのルート(4 章) |
| `-AssetsPrefix` | `Game/Assets` | `assets/<prefix>/...` というエントリ名の頭。展開先で再現される相対パスになる |
| `-NoAssets` | オフ | アセットを一切入れない。ネイティブだけ差し替えて試すとき用(APK は約 25MB) |
| `-CompressionLevel` | `Fastest` | 追加するエントリの圧縮レベル(`Optimal` / `Fastest` / `NoCompression`) |
| `-IncludeValidationLayer` | オフ | Vulkan 検証レイヤを同梱する(6 章の制約あり) |
| `-ValidationLayerPath` | (自動探索) | 検証レイヤ `.so` を明示指定する |
| `-Keystore` | `%USERPROFILE%\.android\debug.keystore` | 署名鍵。無ければ `keytool` で作る |
| `-AndroidSdkRoot` / `-BuildToolsVersion` / `-AndroidPlatform` / `-JdkRoot` | この開発機の値 | ツールの場所 |

処理は次の順に進み、途中で失敗したらその場で止まる(中間物は
`<OutApk>.work\` に残るので中身を確認できる。成功時は消える)。

1. デバッグ用 keystore の確認 / 生成(`keytool -genkeypair`、パスワードと別名は
   Android SDK の慣例どおり `android` / `androiddebugkey`)
2. `aapt2 link` で `AndroidManifest.xml` から素の APK を作る
3. `lib/<abi>/libGame.so`(と検証レイヤ)を追加リストへ積む
4. アセットを走査して `assets/Game/Assets/...` のエントリ名を組み立て、
   `asset_index.txt` / `asset_stamp.txt` を生成する(4 章)
5. 2 の APK をコピーして 3・4 を流し込む
6. `zipalign -p -f 4` ★**署名の前**。逆にすると署名が壊れる
7. `apksigner sign` → `apksigner verify --verbose --print-certs`
8. `aapt2 dump badging` で中身を表示
9. 最終 APK の検査(エントリ名に `\` が無いこと / `resources.arsc` が無圧縮であること /
   `zipalign -c -v 4` が通ること)

### 生成物

```
build/android-arm64/AquaDash-debug.apk    約 98 MB(アセット 192 ファイル / 96.7 MB + .so 84 MB)
```

APK の中身:

```
AndroidManifest.xml
resources.arsc                       ← 無圧縮 (Stored) + 4 バイト整列でなければならない
lib/arm64-v8a/libGame.so
assets/asset_index.txt               ← 同梱したファイルの一覧
assets/asset_stamp.txt               ← 展開済み判定用の印
assets/Game/Assets/...               ← アセット本体(192 ファイル)
META-INF/...                         (署名)
```

---

## 4. アセットの同梱(P2)

### エントリ名の規則

エンジン側は `GetContentRoot()` の下に **ソースツリー相対の `Game/Assets/...`** がある前提で
パスを解決する(`Resource.cpp` / シェーダローダがその規則)。そこで APK 内も
その構造をそのまま持たせ、端末側は「`assets/` の中身を内部ストレージへ展開して
そのパスを `GetContentRoot()` にする」だけで済むようにしている。

| 元のファイル | APK 内のエントリ名 |
|---|---|
| `Game\Assets\Shader\spv\Foo.main.vs.spv` | `assets/Game/Assets/Shader/spv/Foo.main.vs.spv` |
| `Game\Assets\Character\Albedo.png` | `assets/Game/Assets/Character/Albedo.png` |

区切りは**必ず `/`**。

### なぜ `aapt2 link -A` を使わないのか

**Windows の `aapt2 -A` はサブディレクトリの区切りを `\` のまま zip エントリ名に残す**
(`assets/spv\dummy.spv` になる)。`AAssetManager` はエントリ名を `/` 区切りで引くので、
サブフォルダ配下のアセットが一切開けなくなる。

そのため `-A` は使わず、`System.IO.Compression.ZipArchive` でエントリ名を自分で
組み立てて追加している。スクリプトは最後に**エントリ名へ `\` が混ざっていないことを
検査**し、1 つでもあれば `exit 1` する。

### `asset_index.txt`

`AAssetDir` はサブディレクトリを返さないため、**端末側は APK 内を再帰的に列挙できない**。
そこで展開に必要なファイル一覧を APK 自身に持たせる。

- 場所: `assets/asset_index.txt`
- 1 行 1 ファイル。内容は `Game/Assets/...` 形式の相対パス(`/` 区切り、`assets/` は含まない)
- **UTF-8 / BOM なし / LF**
- 自分自身と `asset_stamp.txt` は一覧に含めない
- 並び順はフルパスでソートして固定(実行ごとに揺れない)

```
Game/Assets/animData/idle.tka
Game/Assets/animData/jump.tka
Game/Assets/animData/run.tka
Game/Assets/animData/walk.tka
Game/Assets/Audio/Main.audiobank.json
Game/Assets/Character/Albedo.png
...
Game/Assets/Shader/spv/BloomBrightExtract.main.cs.spv
```

### `asset_stamp.txt`

展開済みかどうかの判定に使う印。

- 場所: `assets/asset_stamp.txt`
- **`<ファイル数> <合計バイト数>`** の 1 行(半角スペース区切り)
- **UTF-8 / BOM なし / LF**

```
192 101394210
```

端末側は展開先に置いた同名ファイルと**文字列比較**し、違えば全展開し直す。
アセットを 1 つ足しただけでも数値が変わるので展開がやり直される。

---

## 5. 実機へのインストールと確認

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

## 6. 既知の落とし穴と制約

### ★`resources.arsc` は無圧縮(Stored)でなければならない

**Android 30(R)以降は、`resources.arsc` が「無圧縮(Stored)」かつ「4 バイト境界に整列」で
なければインストールを拒否する。**

```
adb: failed to install AquaDash-debug.apk:
  Failure [-124: Failed parse during installPackageLI:
  Targeting R+ (version 30 and above) requires the resources.arsc of installed APKs
  to be stored uncompressed and aligned on a 4-byte boundary]
```

整列は `zipalign` が直せるが、**圧縮は zip へ入れる時点で決まり `zipalign` では解けない**。

さらに厄介なのは、**Windows PowerShell(.NET Framework)の `ZipArchive` は
`CompressionLevel.NoCompression` を渡しても Stored ではなく Deflate(レベル 0)で書く**こと。
40 バイトの `resources.arsc` が 45 バイトに膨らむ、という形で現れる
(`CompressedLength > Length`)。つまり「新しい zip へ全エントリを詰め直して無圧縮指定」では
条件を満たせない。

そこでこのスクリプトは、

- `aapt2 link` が **Stored で書いた `resources.arsc` を触らない**
- APK は `ZipArchiveMode.Update` で開き、**追加するエントリだけ**を書く
  (Update モードは触らなかったエントリを元のバイト列のまま残す)
- 最後に `resources.arsc` の `CompressedLength == Length` を検査し、
  違えば明確なメッセージで `exit 1`

という形にしている。`aapt2` 以外の方法で `resources.arsc` を作り直すコードを足すときは、
必ずこの検査が通ることを確認すること。

なお `ZipArchiveMode.Update` は追加するエントリをいったんメモリに抱えるため、
アセット 97MB + `.so` 84MB を足すとピークで **約 380MB** のワーキングセットを使う。
現状は問題ないが、アセットが倍増するようなら別の手段(`aapt` の `add` など)へ
切り替える必要がある。

### その他

- **APK が 100MB 近い。** アセット 96.7MB + `.so` 84MB を圧縮して約 98MB。
  さらに `extractNativeLibs="true"` と「起動時にアセットを内部ストレージへ展開する」方式なので、
  **端末側では APK とは別に同容量が展開される**(= ストレージを二重に消費する)。
  Play 配信を考えるなら AAB + Play Asset Delivery が必要になるが、当面は非対象。
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
- **アセットは `Game\Assets` を丸ごと入れている。** `.hlsl` のシェーダソースのように
  端末では使わないものも含む。絞るなら除外パターンを足す(現状は付けていない)。
- **`.so` は圧縮して入れている。** マニフェストが `extractNativeLibs="true"` を宣言し、
  インストール時に展開される前提のため。`zipalign -p` のページ整列は非圧縮 `.so` 向けの
  指示なので、この構成では効いていない(将来 `extractNativeLibs="false"` にするなら
  `.so` を無圧縮で入れる方へ変える)。
- **設計書とのずれ**: 設計書は `DirectX/Android/app/src/main/AndroidManifest.xml`(Gradle の
  ディレクトリ構成)を想定しているが、Gradle を使わないので `DirectX/Android/AndroidManifest.xml`
  に 1 枚だけ置いている。
