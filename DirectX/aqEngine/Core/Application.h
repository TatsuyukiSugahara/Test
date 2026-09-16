#pragma once
#include "IApplication.h"
#include "Rendering/Renderer.h"
#include "Rendering/RenderThread.h"
#include "Rendering/Pipeline/PipelinePresets.h"
#include <memory>
#ifdef AQ_DEBUG_IMGUI
#include "Rendering/Debug/RenderingDebugPanel.h"
#include "Rendering/Debug/LightingDebugPanel.h"
#include "Ocean/Debug/OceanDebugPanel.h"
#include "UI/Debug/UIEditorDebugPanel.h"
#include "UI/Debug/TextStyleEditorPanel.h"
#include "UI/Debug/UIAnimationEditor.h"
#include "Util/Debug/ProfilerDebugPanel.h"
#include "ECS/PrefabEditor.h"
#include "Level/LevelEditor.h"
#include <memory>
#endif

namespace aq
{
	class Camera;


	/**
	 * 標準的な描画構成の設定値(設計書/使いやすさ改善設計.md P2-B)。
	 *
	 * 実体は `rendering::RendererPreset`(`Rendering/Pipeline/PipelinePresets.h`)。
	 * `Rendering/` が `Core/` に依存しないよう定義をそちらへ移し、ここでは互換のため
	 * `aq::RendererPreset` として使えるよう using で再公開する(ゲームは
	 * `aq::RendererPreset` と書いている)。
	 */
	using RendererPreset = rendering::RendererPreset;


	/**
	 * エンジンサブシステムの初期化・更新・終了を担うアプリケーション基底クラス。
	 * ゲーム側は OnInitialize / OnFinalize / OnUpdate / OnRegister / OnPreRender を override する。
	 */
	class Application : public IApplication
	{
	public:
		/** 分割画面の 1 ビュー (カメラ + ビューポート矩形)。camera の寿命は設定側が保証する */
		struct SplitView
		{
			const Camera*                 camera = nullptr;
			rendering::ViewRect           rect;
		};


	protected:
		aq::rendering::Renderer     renderer_;
		aq::rendering::RenderThread renderThread_;

	private:
		bool renderThreadReady_ = false;

		/** 分割画面ビュー (2 個以上でマルチビュー描画。空 or 1 個は従来の単一ビュー経路) */
		std::vector<SplitView> splitViews_;


	public:
		/** 分割画面ビューを設定する (次フレームから有効。ステージ退出時などは Clear すること) */
		void SetSplitViews(const std::vector<SplitView>& views) { splitViews_ = views; }
		void ClearSplitViews() { splitViews_.clear(); }

		/**
		 * 提出済みの描画が CPU・GPU とも完了するまで待つ。
		 * 自前 GPU バッファを持つエンティティを実行時に破棄する前に呼ぶと、
		 * 在フライトのコマンドリストがそのリソースを参照したまま解放されるのを防げる。
		 *
		 * レンダースレッドのドレインだけでは足りない。D3D12/Vulkan は Present がフェンスを
		 * Signal するだけで GPU 完了を待たないため、ドレイン後も GPU はまだリソースを
		 * 読んでいることがある。GraphicsDevice::WaitIdle まで込みで初めて破棄が安全になる。
		 */
		void WaitForRenderIdle() override;

	public:
		bool Initialize(aq::graphics::RenderContext& renderContext) override;
		void Finalize() override;
		void Update() override;
		void FlushRender() override;
		void Register() override;

	protected:
		/**
		 * 標準的な描画構成(Shadow + Deferred + Hi-Z + PostProcess + Sky)をまとめて組む。
		 *
		 * `OnInitialize()` から 1 行呼べば従来と同じ絵になる。中身は
		 * `PipelinePresets::Standard(preset, uiCallback)` で `PipelineBuilder` を組み、
		 * `Build()` した `RenderPipeline` を `renderer_.SetPipeline()` へ渡すだけ
		 * (設計書/レンダーパイプライン設計.md)。個別のパス構成を自分で組みたいゲームは
		 * これを呼ばず `SetRenderPipeline()` を使う。
		 *
		 * 解像度は `Engine` から取る。ディファードが有効なときに G-Buffer の worldPos を
		 * ポストプロセスへ渡す配線もここで行う(「Deferred が有効ならこう繋ぐ」は
		 * エンジンの都合であって、ゲームが知る必要がない)。
		 *
		 * 生成に失敗したパスは黙って飛ばす。空やブルームが出ないだけで描画は継続する
		 * (Deferred の decal と同じ作法)。パイプライン全体の Build() が失敗した場合は
		 * assert してログのみ出し、パイプラインを設定しない。
		 *
		 * @param preset 各パスの有効/無効と設定値。既定のままで従来構成
		 */
		void SetupStandardRenderers(const RendererPreset& preset = {});

		/**
		 * UI パス込みの標準パイプラインを `PipelineBuilder` のまま返す(設計書/レンダーパイプライン設計.md P2)。
		 * `PipelinePresets::ForCurrentPlatform(preset, uiCallback)` を呼ぶだけの薄い包み。
		 * `SetupStandardRenderers()` はこれを `Build()` して `SetRenderPipeline()` するだけの 2 行になった。
		 *
		 * ゲームが `InsertBefore<UIPass>(...)` 等で列を組み替えてから `SetRenderPipeline()` したいときに使う
		 * (`preset.pipeline` の既定は `Standard` なので、何もしなければ従来と同じ列になる)。
		 */
		rendering::PipelineBuilder BuildStandardPipeline(const RendererPreset& preset = {});

		/**
		 * ゲームが自前で組んだ RenderPipeline を丸ごと設定する。
		 * SetupStandardRenderers() を使わず PipelineBuilder で独自の構成を組みたいゲーム向けの入口。
		 * メイン RT とビューポートは Engine から取り、RenderDebugSync 用に Renderer へ渡す。
		 */
		void SetRenderPipeline(std::unique_ptr<rendering::RenderPipeline> pipeline);

		/** ゲーム固有の初期化（エンジンサブシステム初期化後に呼ばれる） */
		virtual bool OnInitialize() { return true; }
		/** ゲーム固有の終了処理（エンジンサブシステム終了前に呼ばれる） */
		virtual void OnFinalize() {}
		/** ゲーム固有の更新（Input/ECS/ResourceManager 更新後に呼ばれる） */
		virtual void OnUpdate() {}
		/** ゲーム固有のリソース・システム登録 */
		virtual void OnRegister() {}
		/** メインパス Submit 前に呼ばれる（オフスクリーンパス等を Submit する） */
		virtual void OnPreRender() {}
#ifdef AQ_IMGUI
		/** ImGui::NewFrame() 直後に呼ばれる。ゲーム固有の ImGui ウィンドウをここで構築する */
		virtual void OnImGuiRender() {}
#endif
#ifdef AQ_DEBUG_IMGUI
		/** メインメニューバー内にゲーム固有のメニュー項目を追加する */
		virtual void OnDebugRenderMenu() {}
		/** ImGui ウィンドウ構築（EntityContext::DebugRender() の後に呼ばれる） */
		virtual void OnDebugRender() {}
#endif

	private:
		void Render();

#ifdef AQ_IMGUI
		bool imguiReady_ = false;
#endif
#ifdef AQ_DEBUG_IMGUI
		bool                                                showDebugUI_ = true;
		std::unique_ptr<aq::ocean::OceanDebugPanel>         oceanDebugPanel_;
		std::unique_ptr<aq::rendering::RenderingDebugPanel> renderingDebugPanel_;
		std::unique_ptr<aq::ui::UIEditorDebugPanel>         uiEditorDebugPanel_;
		std::unique_ptr<aq::ui::TextStyleEditorPanel>       textStyleEditorPanel_;
		std::unique_ptr<aq::ui::UIAnimationEditor>          uiAnimationEditor_;
		std::unique_ptr<aq::profile::ProfilerDebugPanel>    profilerDebugPanel_;
		std::unique_ptr<aq::ecs::PrefabEditorPanel>         prefabEditorPanel_;   // 全シーン共通の Prefab エディタ
		std::unique_ptr<aq::level::LevelEditorPanel>        levelEditorPanel_;    // 全シーン共通の Level エディタ
#endif
	};
}
