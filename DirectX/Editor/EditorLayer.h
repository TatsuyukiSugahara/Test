#pragma once
#include "Editor/IEditorLayer.h"
#include "EditorContext.h"
#include "GameViewInput.h"
#include "PlayModeController.h"
#include "PlayStopSelfTest.h"
#include "Panels/HierarchyPanel.h"
#include "Panels/InspectorPanel.h"
#include "Panels/LevelFileMenu.h"
#include <cstdint>


namespace editor
{
	/**
	 * エディタの UI 層。ゲームビュー、ヒエラルキー、インスペクター、メインメニューバー(File メニューと再生・停止のツールバー)を持つ。
	 * AppHost がエディタ用の ImGui コンテキストを current にして呼ぶ。寿命は EditorMain が持つ
	 */
	class EditorLayer final : public aq::IEditorLayer
	{
	private:
		/** ゲームビューへの入力の転送 */
		GameViewInput gameViewInput_;

		/** 再生・一時停止・停止の遷移 */
		PlayModeController playModeController_;

		/** Play/Stop 自己テスト */
		PlayStopSelfTest playStopSelfTest_;

		/** 編集の状態(編集モデルへの参照・選択・コマンドの列) */
		EditorContext editorContext_;

		/** パネル */
		HierarchyPanel hierarchyPanel_;
		InspectorPanel inspectorPanel_;
		LevelFileMenu  levelFileMenu_;

		/** 初期配置の要否を確かめたか(最初のフレームで 1 回だけ確かめる) */
		bool isLayoutChecked_ = false;

		/** 起動直後の最初のフレームか(ゲームビューにフォーカスを与える) */
		bool isFirstFrame_ = true;

		/** 起動引数で自己テストを指示された(最初のフレームで始める) */
		bool startSelfTestOnLaunch_ = false;


	public:
		void OnBeginFrame(ImGuiContext* gameContext) override;
		void OnBuildUI(const aq::rendering::RenderTargetHandle gameViewRT) override;
		void OnEndFrame() override;

		/** 起動引数 -editor-selftest を受けて、最初のフレームから自己テストを始める(終わったら終了コードを返して終了する) */
		inline void RequestSelfTestOnLaunch() { startSelfTestOnLaunch_ = true; }


	private:
		/** ゲームビュー。displayRT をアスペクト比を保って縮小表示し、このフレームの状態を返す */
		GameViewState DrawGameView(const aq::rendering::RenderTargetHandle gameViewRT);

		/** メインメニューバー。左から File メニュー、再生のツールバー、今のパス */
		void DrawMainMenuBar();

		/** メインメニューバーの中に再生・一時停止・再開・停止のボタン、現在のモード、自己テストを置く */
		void DrawPlayToolbar();

		/**
		 * ヒエラルキーの配置の保存が無いとき、最初のフレームに 1 回だけドックスペースを
		 * 左(ヒエラルキー)・中央(ゲームビュー)・右(インスペクター)に分ける。分けたら true
		 */
		bool SetupInitialLayout(const uint32_t dockspaceId);
	};
}
