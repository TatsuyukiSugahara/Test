# はじめての aqEngine

箱が 1 個出るだけのサンプルです。**新しくゲームを作るときは、このフォルダをコピーして始めてください。**

このページは 3 歩でできています。上から順にやれば、自分のアセットを読み込んで、
自分で書いた処理を毎フレーム動かすところまで行きます。

- [1 歩目: 動かす](#1-歩目-動かす)
- [2 歩目: 自分のアセットを置く](#2-歩目-自分のアセットを置く)
- [3 歩目: 自分の System を足す](#3-歩目-自分の-system-を足す)

対応しているのは **Windows と macOS** です。iOS / Android / Xbox も
エンジン自体は対応していますが、起動まわりの書き方が違うので、
必要になったら `Game/Application/` の同名ファイルを写してください。

---

## 1 歩目: 動かす

### ビルドする

**Windows** はこのフォルダの `build_windows.bat` を叩きます。

```
Sample\build_windows.bat run
```

CMake が Visual Studio のプロジェクトを `build\windows-vs2026\` へ生成し、`Sample` だけを
ビルドして起動します。Visual Studio で編集やデバッグをしたいときは、生成された
`build\windows-vs2026\AquaDash.slnx` を開いて `Sample` をスタートアッププロジェクトにしてください。
リポジトリ直下の `DirectX.sln` は**エンジン開発用**で、このサンプルは入っていません。

**macOS** はコマンドラインから叩きます。

```
cmake --preset macos-ninja-metal
cmake --build build/macos-ninja-metal --config Release --target Sample
./build/macos-ninja-metal/bin/Release/Sample.app/Contents/MacOS/Sample
```

青い箱が 1 個、空を背景に表示されれば成功です。

### 何が書いてあるか

中身は 3 ファイルしかありません。

| ファイル | 役割 |
| --- | --- |
| `Application.h` / `.cpp` | ゲームの中身。**普段いじるのはここ** |
| `Main.cpp` | Windows の起動処理 |
| `MacMain.mm` | macOS の起動処理 |

`Application.cpp` の `OnInitialize()` がゲームの入口です。やっているのは 3 つだけです。

```cpp
// 影 / ディファード / ポストプロセス / 空を既定構成で組む
SetupStandardRenderers();

// カメラを置く
aq::Camera* camera = aq::CameraManager::Get().GetCamera(aq::CameraType::Main);
camera->SetPosition(aq::math::Vector3(3.0f, 3.0f, -5.0f));
camera->SetTarget(aq::math::Vector3(0.0f, 0.0f, 0.0f));

// 箱を 1 個作る
auto entity = aq::ecs::EntityContext::Get().CreateEntity<
    aq::ecs::TransformComponent,
    aq::ecs::HierarchicalTransformComponent,
    aq::ecs::BoxStaticMeshComponent>();
```

**`Sample/Assets/` は空です。** 箱もシェーダもエンジンが持っているので、
アセットを 1 個も用意しなくても絵が出ます。

### まず試すこと

`Application.cpp` の `SetColor` の値を変えて、箱の色を変えてみてください。

```cpp
entity.GetComponent<aq::ecs::BoxStaticMeshComponent>()
    ->SetColor(aq::math::Vector4(1.0f, 0.3f, 0.0f, 1.0f));   // オレンジ
```

---

## 2 歩目: 自分のアセットを置く

### ファイルを置く場所

`Sample/Assets/` の下です。コードからは **`"Assets/..."` で始まるパス**で書きます。

```
Sample/Assets/Textures/mybox.png   ←  ここに置いたら
"Assets/Textures/mybox.png"        ←  コードではこう書く
```

`"Assets/"` が `Sample/Assets/` を指すのは、起動処理で自分のフォルダ名を
渡しているからです(`Main.cpp` / `MacMain.mm` の `gameRootName`)。

```cpp
initializeParameter.gameRootName = "Sample";
```

**フォルダごとコピーして名前を変えたときは、ここも一緒に変えてください。**
忘れると、前の名前のフォルダを探しに行きます。

### 読み込む

テクスチャなら `ResourceManager` を通します。読み込みは非同期で、
すぐには完成しません(完成するまで描画側が待ちます)。

```cpp
#include "Resource/Resource.h"

auto texture = aq::res::ResourceManager::Get()
    .Load<aq::res::GPUResource>("Assets/Textures/mybox.png");
```

モデル(`.fbx` / `.obj`)なら `aq::res::MeshResource`、
音なら `aq::sound::SoundClip` です。**どれも登録なしで使えます**
(エンジンが起動時に済ませています)。

### 見つからないとき

パスを間違えると、ログに候補が並びます。

```
[asset] 見つかりません: Assets/Textures/mybox.png  (コンテンツ基点: .../DirectX)
[asset]   候補 1/4: Assets/Textures/mybox.png
[asset]   候補 2/4: .../DirectX/Sample/Assets/Textures/mybox.png
```

**候補 2 が自分の置いた場所になっているか**を見てください。
`Game/Assets/...` になっていたら `gameRootName` の渡し忘れです。

ログの出先は Windows / macOS とも、実行時の作業ディレクトリの `startup_timing.log` です。

---

## 3 歩目: 自分の System を足す

ゲームの「毎フレームの処理」は **System** に書きます。
箱をゆっくり回す System を作ってみます。

### コンポーネントと System を書く

`Sample/Application/SpinSystem.h` を作ります。

```cpp
#pragma once
#include "ECS/ECS.h"
#include "ECS/EntityContext.h"
#include "Component/TransformComponentSystem.h"

namespace sample
{
    /** 回転させたいエンティティに付けるデータ */
    struct SpinComponent : public aq::ecs::IComponent
    {
        ecsComponent(sample::SpinComponent);

        float radianPerSecond = 1.5f;   // 回転速度 [rad/s]
        float angle           = 0.0f;   // 現在の角度 [rad]
    };


    /** SpinComponent が付いたエンティティを毎フレーム回す */
    class SpinSystem : public aq::ecs::SystemBase
    {
    public:
        void Update() override
        {
            const float dt = aq::Engine::GetDeltaTime();

            aq::ecs::Foreach<SpinComponent, aq::ecs::TransformComponent>(
                [dt](const aq::ecs::Entity&, SpinComponent* spin, aq::ecs::TransformComponent* transform)
                {
                    spin->angle += spin->radianPerSecond * dt;
                    transform->rotation.SetRotation(aq::math::Vector3(0.0f, 1.0f, 0.0f), spin->angle);
                });
        }
    };
}
```

`aq::ecs::Foreach<...>` が、指定したコンポーネントを**全部持っている**エンティティだけを回します。
ラムダの第 1 引数は `Entity` で、以降が各コンポーネントの**ポインタ**です。

`TransformComponent::rotation` は**クォータニオン**です。オイラー角ではないので
`rotation.y` に足しても回りません。角度は自分で持っておき、
`SetRotation(軸, 角度)` で毎フレーム作り直します。

### 登録する

`Application.h` に `OnRegister` を足します。

```cpp
protected:
    bool OnInitialize() override;
    void OnRegister() override;       // ← 追加
```

`Application.cpp` で登録します。

```cpp
#include "SpinSystem.h"

void Application::OnRegister()
{
    aq::ecs::EntityContext::Get().AddSystem<sample::SpinSystem>();

    // ワールド変換を組み立てる System より前に走らせる。
    // これを書かないと回転の反映が 1 フレーム遅れる。
    aq::ecs::EntityContext::Get()
        .AddDependency<aq::ecs::HierarcicalTransformSystem, sample::SpinSystem>();
}
```

箱を作るときに `SpinComponent` を足せば回り始めます。

```cpp
auto entity = aq::ecs::EntityContext::Get().CreateEntity<
    aq::ecs::TransformComponent,
    aq::ecs::HierarchicalTransformComponent,
    aq::ecs::BoxStaticMeshComponent,
    sample::SpinComponent>();
```

### 順番について

System は登録順ではなく、**宣言した依存関係の順**で走ります。
`AddDependency<A, B>()` は「**A は B の後**」という意味です。

順番の指定を間違えても**ビルドは通ります**。おかしいと思ったら依存を見直してください。
なお、循環してしまった場合は起動時に検出されて止まります。

---

## 4 歩目: 描画パスを足す

描画は「パス」の列で決まっています。影 → G-Buffer → ライティング → 空 → 前方描画 → ポストプロセス → UI、
という並びをエンジンが `Standard` という名前で持っていて、`SetupStandardRenderers()` はそれを組んでいるだけです。
起動ログ(`startup_timing.log`)に実際の列が 1 行出ます。

```
[pipeline] 確定: ShadowPass > ClusterCullPass > GBufferPass > ... > TonemapPass > UIPass
```

この列に、自分のパスを挟めます。ここでは画面の縁を暗くする「ビネット」を、UI の手前に入れます。

### パスを書く

パスは `aq::rendering::IRenderPass` を継承したクラスです。System と同じで、エンジンの標準パスも
自分のパスも同じ型なので、扱いに差はありません。実物は
[Application/VignettePass.h](Application/VignettePass.h) と [VignettePass.cpp](Application/VignettePass.cpp) にあります。
書くのは次の 5 つです。

| 関数 | 何を書くか |
| --- | --- |
| `GetName()` | ログに出る名前 |
| `GetScope()` | フレームに 1 回(`Frame`)か、分割画面のビューごと(`View`)か |
| `IsSupported()` | 動く条件。ビネットは compute シェーダで描くので `IsComputeSupported()` |
| `DeclareResources()` | 読む RT と書く RT の宣言。ビネットは「トーンマップ後の `Output` を読み、新しい `Output` を書く」 |
| `Setup()` | シェーダと自分の RT の生成。掲示板(`PassResources`)に自分の RT を `Output` として登録 |
| `Build()` | コマンドを積む。`FullscreenComputeCommand` にシェーダ・入力・出力・定数を渡すだけ |

RT の受け渡しは「掲示板」で行います。パスは前後のパスの型を知らず、`Scene` / `Depth` / `WorldPos` / `Output`
といったキーで RT を読み書きします。読むと宣言したキーが前のパスに無ければ、起動時に
「`VignettePass` は `WorldPos` を読むが、それを書くパスが前に無い」のように名指しで止まります。

シェーダはエンジン所有の `aqEngine/Assets/Shader/Vignette.fx`(compute)を使っています。
自分のシェーダを書く場合も同じ場所に置きます(ゲーム側フォルダのシェーダを Mac / iOS / Android 向けに
事前コンパイルする経路がまだ無いためです)。

### 列に挟む

`SetupStandardRenderers()` を次の 3 行に置き換えます。標準の列を組み立て途中の形でもらい、
`UIPass` の手前にビネットを挿してから確定させます。

```cpp
#include "VignettePass.h"
#include "Rendering/Pipeline/Passes/UIPass.h"   // 挿す位置の目印にするパスのヘッダ

// OnInitialize の中
auto builder = BuildStandardPipeline();
builder.InsertBefore<aq::rendering::UIPass>(std::make_unique<VignettePass>());
SetRenderPipeline(builder.Build(aq::Engine::Get().GetRenderWidth(), aq::Engine::Get().GetRenderHeight()));
```

起動すると画面の縁が暗くなり、ログの列は `... > TonemapPass > VignettePass > UIPass` になります。
UI はビネットの後ろなので暗くなりません。

挿す以外の操作もあります。

| やりたいこと | 書き方 |
| --- | --- |
| 前に挿す / 後ろに挿す | `InsertBefore<UIPass>(...)` / `InsertAfter<GBufferPass>(...)` |
| 標準のパスを自分のものに替える | `Replace<TonemapPass>(std::make_unique<MyTonemapPass>())` |
| 標準のパスを外す | `Remove<BloomPass>()` |
| 全部自分で並べる | `aq::rendering::PipelineBuilder b; b.Add<ShadowPass>(...).Add<ForwardPass>()...;` |
| 携帯向けの軽い列にする | `aq::RendererPreset p; p.pipeline = aq::rendering::PipelineKind::Mobile; SetupStandardRenderers(p);` |

目印にするパスのヘッダは `Rendering/Pipeline/Passes/<名前>.h` を include します。
`Build()` が失敗したとき(読む RT が無い、など)は理由をログに出して `nullptr` を返すので、
その場合は何も描かれません。ログの `[pipeline]` 行を見てください。

---

## この先

ここから先は、やりたいことに応じて設計書を読んでください。
**設計書は使い方の手引きではなく、なぜそう作ったかの記録**です。

| やりたいこと | 読む場所 |
| --- | --- |
| エンティティを JSON で定義する | [Prefab設計](../設計書/Prefab設計.md) |
| ステージを JSON で組む / 非同期で読む | [Level設計](../設計書/Level設計.md) |
| ECS の仕組みを知る | [ECS設計](../設計書/04_ECS設計.md) |
| 描画パスの並びを変える / 足す | [レンダーパイプライン設計](../設計書/レンダーパイプライン設計.md) |
| 描画の仕組みを知る | [レンダリング設計](../設計書/01_レンダリング設計.md) |
| 音を鳴らす | [Sound設計](../設計書/Sound設計.md) |
| 入力を増やす | [HID設計](../設計書/02_HID設計.md) |

実際に動いているゲームの例は [`Game/`](../Game/)(AquaDash)です。
このサンプルより込み入っていますが、**書き方の型は同じ**です。
