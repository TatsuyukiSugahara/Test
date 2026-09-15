#pragma once
#include "IApplication.h"
#include "Rendering/Renderer.h"
#include "Rendering/RenderThread.h"
#include "Rendering/Occlusion/HiZRenderer.h"
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
	 * ゲームが Shadow / Deferred / PostProcess / Sky を 1 つずつ生成して配線していた
	 * 定型を、`Application::SetupStandardRenderers()` 1 呼び出しへ畳むための引数。
	 * **既定値のまま渡せば従来と同じ絵になる。**
	 *
	 * 解像度はここで持たない。`Engine::GetRenderWidth/Height()` から取るため
	 * (ゲームが Engine に渡した値を、もう一度ゲームが書き写す理由がない)。
	 */
	struct RendererPreset
	{
		/** 影。false でシャドウなし */
		bool enableShadow = true;
		/** 影の設定。既定値は ShadowSettings 側が持つ */
		rendering::ShadowSettings shadow = {};
		/** ShadowDepth のシェーダ。既定はエンジン所有 */
		const char* shadowVSPath = "aqEngine/Assets/Shader/ShadowDepth.fx";

		/** ディファード。false でフォワードのみ */
		bool enableDeferred = true;

		/** ポストプロセス(MotionBlur / Bloom / Tonemap)。false で素通し */
		bool  enablePostProcess = true;
		float bloomThreshold    = 1.0f;
		float bloomIntensity    = 0.45f;
		uint32_t bloomBlurPasses = 4;

		/** 空。false で背景はクリア色のまま */
		bool        enableSky   = true;
		/** キューブマップ。既定はエンジン所有 */
		const char* skyCubemapPath = "aqEngine/Assets/Sky/DefaultSkyCube.dds";
	};


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
			rendering::Renderer::ViewRect rect;
		};


	protected:
		aq::rendering::Renderer     renderer_;
		aq::rendering::RenderThread renderThread_;

	private:
		bool renderThreadReady_ = false;
		std::unique_ptr<aq::rendering::HiZRenderer> hiZRenderer_;  // オクリュージョン基盤 (Hi-Z)

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
		 * 標準的な描画構成(Shadow + Deferred + PostProcess + Sky)をまとめて組む。
		 *
		 * `OnInitialize()` から 1 行呼べば従来と同じ絵になる。**呼ばなければ何も生成しない**
		 * ので、個別に `SetShadowRenderer` 等を使う従来のやり方もそのまま通る。
		 *
		 * 解像度は `Engine` から取る。ディファードが有効なときに G-Buffer の worldPos を
		 * ポストプロセスへ渡す配線もここで行う(「Deferred が有効ならこう繋ぐ」は
		 * エンジンの都合であって、ゲームが知る必要がない)。
		 *
		 * 生成に失敗したパスは黙って飛ばす。空やブルームが出ないだけで描画は継続する
		 * (Deferred の decal と同じ作法)。
		 *
		 * @param preset 各パスの有効/無効と設定値。既定のままで従来構成
		 */
		void SetupStandardRenderers(const RendererPreset& preset = {});

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
