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

**Windows** は `DirectX.sln` を開き、`Sample` をスタートアッププロジェクトにして実行します。

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

## この先

ここから先は、やりたいことに応じて設計書を読んでください。
**設計書は使い方の手引きではなく、なぜそう作ったかの記録**です。

| やりたいこと | 読む場所 |
| --- | --- |
| エンティティを JSON で定義する | [Prefab設計](../設計書/Prefab設計.md) |
| ステージを JSON で組む / 非同期で読む | [Level設計](../設計書/Level設計.md) |
| ECS の仕組みを知る | [ECS設計](../設計書/04_ECS設計.md) |
| 描画を差し替える / 足す | [レンダリング設計](../設計書/01_レンダリング設計.md) |
| 音を鳴らす | [Sound設計](../設計書/Sound設計.md) |
| 入力を増やす | [HID設計](../設計書/02_HID設計.md) |

実際に動いているゲームの例は [`Game/`](../Game/)(AquaDash)です。
このサンプルより込み入っていますが、**書き方の型は同じ**です。
