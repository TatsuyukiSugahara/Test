#pragma once
#include <imgui/imgui.h>


namespace editor
{
	/**
	 * ゲームビューの状態。
	 * 座標はエディタ用コンテキストの io.MousePos と同じ空間(= メインウィンドウのクライアント座標)
	 */
	struct GameViewState
	{
		bool  visible          = false;     // ゲームビューに displayRT の画像を描いた
		bool  focused          = false;     // ゲームビューのウィンドウがエディタ内でフォーカスを持つ
		bool  editorHoldsMouse = false;     // エディタがマウスを使っている(ポップアップが開いている、別の項目をマウスで操作中)
		float imageMinX = 0.0f, imageMinY = 0.0f;   // 画像の左上
		float imageMaxX = 0.0f, imageMaxY = 0.0f;   // 画像の右下(黒帯を含まない)
	};




	/**
	 * ゲームビューへの入力の転送。
	 * エディタ用コンテキストの入力キューを覗き見て、ゲームビュー向けの入力をゲーム用コンテキストへ積み直し、
	 * ゲーム本体の入力(InputManager)にフォーカスとポインタの範囲を指示する
	 */
	class GameViewInput
	{
	private:
		/** 転送するキーボードのキーの数(ImGuiKey_NamedKey_BEGIN 〜 ImGuiKey_GamepadStart の手前) */
		static constexpr int KEYBOARD_KEY_COUNT = ImGuiKey_GamepadStart - ImGuiKey_NamedKey_BEGIN;
		/** 転送する修飾キーの数(Ctrl / Shift / Alt / Super) */
		static constexpr int MOD_KEY_COUNT = 4;
		/** 送った押下の控えに使うキーの枠の数 */
		static constexpr int KEY_SLOT_COUNT = KEYBOARD_KEY_COUNT + MOD_KEY_COUNT;


	private:
		/** 前フレームのゲームビューの状態(今フレームの振り分けに使う) */
		GameViewState prevState_;

		/** 覗き見の位置(最後に読んだイベントの EventId。0 は未読) */
		uint32_t lastEventId_ = 0;

		/** 自分で覚えるマウス位置(エディタ用コンテキストの座標) */
		bool  mouseKnown_ = false;
		float mouseX_     = 0.0f;
		float mouseY_     = 0.0f;

		/** ゲーム用コンテキストへ送った状態の控え */
		bool mousePosSent_ = false;                 // 有効なマウス位置を送っている(外れたら -FLT_MAX を 1 回送る)
		bool buttonSent_[ImGuiMouseButton_COUNT] = {}; // 押下を送ったボタン
		bool keySent_[KEY_SLOT_COUNT]            = {}; // 押下を送ったキー
		bool gameFocus_                          = true; // ゲーム用コンテキストへ最後に伝えたフォーカス(ImGui の初期値に合わせる)


	public:
		/**
		 * フレームの先頭(OnBeginFrame)から呼ぶ。エディタ用コンテキストが current であること。
		 * 前フレームの状態で今フレームの入力を振り分けてゲーム用へ転送し、ゲームの入力を設定する
		 */
		void BeginFrame(ImGuiContext* gameContext);

		/** OnBuildUI でゲームビューを描いた後に呼ぶ。次のフレームの振り分けに使う */
		void SetViewState(const GameViewState& state) { prevState_ = state; }


	private:
		/** 覚えているマウス位置が「中」(ゲームビューの画像の上で、エディタがマウスを使っていない)か */
		bool IsInside(const bool known, const float x, const float y) const;

		/** ゲーム用へ押下を送ったボタンが 1 つでもあるか */
		bool IsAnyButtonSent() const;

		/** 今のマウス位置をゲーム用へ送る(中または押下中なら換算した位置、外れたら 1 回だけ -FLT_MAX) */
		void ForwardMousePos(ImGuiIO& gameIO);

		/** 送った状態の控えを消す(ゲーム用がフォーカスを失って全部離した扱いにしたとき) */
		void ClearSentInputs();


	private:
		/** 転送対象のキーなら控えの枠の番号、対象外なら -1 */
		static int GetKeySlot(const ImGuiKey key);
	};
}
