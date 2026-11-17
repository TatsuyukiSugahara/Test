#pragma once
#include "IAppModule.h"
#include "Rendering/Renderer.h"
#include "Rendering/RenderThread.h"
#include "Rendering/Pipeline/PipelinePresets.h"
#include <memory>
#ifdef AQ_DEBUG_IMGUI
#include "Rendering/Debug/RenderingDebugPanel.h"
#include "Rendering/Debug/LightingDebugPanel.h"
#include "Ocean/Debug/OceanDebugPanel.h"
#include "UI/Debug/UIEditorDebugPanel.h"
#include "Util/Debug/ProfilerDebugPanel.h"
#include "ECS/PrefabEditor.h"
#include "Level/LevelEditor.h"
#include <memory>
#endif

// ImGui の型はポインタで持つだけなので前方宣言で足りる(ImGui を使わない構成でもコンパイルできるように)
struct ImGuiContext;
struct ImDrawData;

namespace aq
{
	class Camera;
	class IEditorLayer;


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
	 * エンジンサブシステムの初期化・更新・終了を担うアプリケーションの土台(常駐)。
	 * ゲーム側は IAppModule を実装し、AppHost に所有させる。フックは module_ 経由で呼ぶ。
	 */
	class AppHost final
	{
	public:
		/** 分割画面の 1 ビュー (カメラ + ビューポート矩形)。camera の寿命は設定側が保証する */
		struct SplitView
		{
			const Camera*                 camera = nullptr;
			rendering::ViewRect           rect;
		};


	private:
		/** 差し替え単位(ゲーム本体など) */
		std::unique_ptr<IAppModule> module_;

		aq::rendering::Renderer     renderer_;
		aq::rendering::RenderThread renderThread_;

		bool renderThreadReady_ = false;

		/** 分割画面ビュー (2 個以上でマルチビュー描画。空 or 1 個は従来の単一ビュー経路) */
		std::vector<SplitView> splitViews_;

		/** BuildRenderCommands で組み、SubmitRenderCommands で RenderThread へ渡すまでのこのフレームの描画 */
		std::unique_ptr<rendering::RenderCommandList> pendingCmdList_;
		graphics::LightingData                        pendingLighting_;
		rendering::ShadowCBData                       pendingShadow_;

		/**
		 * ImGui のコンテキスト。ゲーム用はデバッグ UI を描く既存のもの。
		 * エディタ用はエディタ構成(AQ_EDITOR)でだけ作り、それ以外では nullptr のまま
		 */
		ImGuiContext* gameImGuiContext_   = nullptr;
		ImGuiContext* editorImGuiContext_ = nullptr;

		/** エディタの UI 層(非所有。エディタのエントリが登録する。ランタイムでは常に nullptr) */
		IEditorLayer* editorLayer_ = nullptr;


	public:
		/** @param module 所有して動かすモジュール(nullptr 不可) */
		explicit AppHost(std::unique_ptr<IAppModule> module);
		~AppHost();


	public:
		/** 現在のモジュール */
		inline IAppModule& GetModule() { return *module_; }

		/** メイン描画のレンダラ */
		inline rendering::Renderer& GetRenderer() { return renderer_; }
		/** レンダースレッド(オフスクリーン Submit 用) */
		inline rendering::RenderThread& GetRenderThread() { return renderThread_; }

		/** 分割画面ビューを設定する (次フレームから有効。ステージ退出時などは Clear すること) */
		void SetSplitViews(const std::vector<SplitView>& views) { splitViews_ = views; }
		void ClearSplitViews() { splitViews_.clear(); }

		/**
		 * エディタの UI 層を登録する(所有しない。layer は AppHost より長く生かすこと)。
		 * 呼び出しはエディタ構成(AQ_EDITOR)でだけ意味を持つ。ランタイムで登録してもフックは呼ばれない
		 */
		inline void SetEditorLayer(IEditorLayer* layer) { editorLayer_ = layer; }

		/**
		 * 提出済みの描画が CPU・GPU とも完了するまで待つ。
		 * 自前 GPU バッファを持つエンティティを実行時に破棄する前に呼ぶと、
		 * 在フライトのコマンドリストがそのリソースを参照したまま解放されるのを防げる。
		 *
		 * レンダースレッドのドレインだけでは足りない。D3D12/Vulkan は Present がフェンスを
		 * Signal するだけで GPU 完了を待たないため、ドレイン後も GPU はまだリソースを
		 * 読んでいることがある。GraphicsDevice::WaitIdle まで込みで初めて破棄が安全になる。
		 */
		void WaitForRenderIdle();

	public:
		/** エンジンサブシステムを初期化し、モジュールの OnInitialize を呼ぶ */
		bool Initialize(aq::graphics::RenderContext& renderContext);
		/** モジュールの OnFinalize を呼んでから、エンジンサブシステムを終了する */
		void Finalize();

		/**
		 * ゲームロジックの更新。RenderContext は受け取らない。
		 * 描画処理は RenderCommandList に積んで RenderThread::Submit() で行う。
		 */
		void Update();

		/**
		 * Engine が Update() の直後に呼ぶ。
		 * RenderThread がフレームの描画・CopyToBackBuffer・Present を完了するまでここで待機する。
		 */
		void FlushRender();

		/**
		 * エンジン標準の System を登録し、モジュールの OnRegister を呼ぶ。
		 * Initialize の後に呼ばれる。false を返すと初期化失敗として扱われる
		 */
		bool Register();

	public:
		/**
		 * 標準的な描画構成(Shadow + Deferred + Hi-Z + PostProcess + Sky)をまとめて組む。
		 *
		 * モジュールの `OnInitialize()` から 1 行呼べば従来と同じ絵になる。中身は
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
		 * Release でもログを出し(Debug は assert も)、パイプラインを設定せず false を返す。
		 * **呼び出し側は false を初期化失敗として返すこと**(黙って続けると全描画が消える)。
		 *
		 * @param preset 各パスの有効/無効と設定値。既定のままで従来構成
		 * @return 組めて設定できたら true。パイプラインの Build が失敗したら false
		 */
		bool SetupStandardRenderers(const RendererPreset& preset = {});

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
		 *
		 * @param pipeline `PipelineBuilder::Build()` の結果。Build 失敗の nullptr をそのまま渡してよい
		 * @return 設定できたら true。nullptr(Build 失敗)なら Release でもログを出して false を返す。
		 *         呼び出し側は false を初期化失敗として返すこと
		 */
		bool SetRenderPipeline(std::unique_ptr<rendering::RenderPipeline> pipeline);

		/**
		 * 描画解像度の実行時変更(設計書/エンジン課題解消設計.md P4)。Engine が手順を駆動する。
		 * すべてゲームスレッドで、WaitForRenderIdle 済みの区間にだけ呼ぶ。
		 *
		 * CanResizeRender    : パイプラインがあり、全パスが Independent か Resizable なら true(デバイスを変える前に判定する)
		 * PrepareRenderResize: 全パスの PrepareResize。失敗したら false(成功済みのパスは取り消し済み)
		 * AbortRenderResize  : 準備の取り消し
		 * CommitRenderResize : 全パスの CommitResize と、Renderer / メインカメラの寸法更新
		 */
		bool CanResizeRender();
		bool PrepareRenderResize(uint32_t width, uint32_t height);
		void AbortRenderResize();
		void CommitRenderResize(uint32_t width, uint32_t height);

	private:
		/** BuildRenderCommands → (エディタのフック) → SubmitRenderCommands の順に 1 フレームの描画を組んで渡す */
		void Render();
		/** ゲーム用 ImGui の構築と描画コマンドの構築(pendingCmdList_ などへ積む) */
		void BuildRenderCommands();
		/** 積んだ描画コマンドを RenderThread へ渡す。editorDrawData は Game では常に nullptr */
		void SubmitRenderCommands(ImDrawData* editorDrawData);

#ifdef AQ_IMGUI
		bool imguiReady_ = false;
#endif
#ifdef AQ_DEBUG_IMGUI
		bool                                                showDebugUI_ = true;
		std::unique_ptr<aq::ocean::OceanDebugPanel>         oceanDebugPanel_;
		std::unique_ptr<aq::rendering::RenderingDebugPanel> renderingDebugPanel_;
		std::unique_ptr<aq::ui::UIEditorDebugPanel>         uiEditorDebugPanel_;
		std::unique_ptr<aq::profile::ProfilerDebugPanel>    profilerDebugPanel_;
		std::unique_ptr<aq::ecs::PrefabEditorPanel>         prefabEditorPanel_;   // 全シーン共通の Prefab エディタ
		std::unique_ptr<aq::level::LevelEditorPanel>        levelEditorPanel_;    // 全シーン共通の Level エディタ
#endif


	private:
		static AppHost* instance_;

	public:
		static AppHost& Get()     { return *instance_; }
		static bool IsAvailable() { return instance_ != nullptr; }
	};
}
