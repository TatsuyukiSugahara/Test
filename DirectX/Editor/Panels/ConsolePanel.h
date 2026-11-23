#pragma once
#include "../ConsoleLog.h"
#include <imgui/imgui.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>


namespace editor
{
	/**
	 * コンソール(エンジンのログの表示)。
	 * 毎フレーム ConsoleLog から新しい行を手元の写しへ足し、重要度と文字で絞り込んで表示する。
	 * どのモードでも動く(Play 中のログも見る)。閉じられるウィンドウで、表示は Window メニューから切り替える
	 */
	class ConsolePanel
	{
	public:
		/** ウィンドウ名。ドッキング配置は editor_layout.ini にこの名前で保存される */
		static constexpr const char* WINDOW_NAME = "Console";


	private:
		/** 手元の写し(ConsoleLog と同じく ConsoleLog::MAX_LINES 行で切る) */
		std::deque<ConsoleLine> lines_;

		/** 写した一番新しい行の通し番号 */
		uint64_t lastSerial_ = 0;

		/** 表示する行(lines_ の添字)。毎フレーム作り直す */
		std::vector<int> visibleRows_;

		/** 文字の絞り込み */
		ImGuiTextFilter filter_;

		/** ウィンドウを出すか */
		bool isOpen_ = true;

		/** 一番下を見ているとき、新しい行に合わせて下へ送るか */
		bool autoScroll_ = true;

		/** 重要度ごとの表示 */
		bool showInfo_    = true;
		bool showWarning_ = true;
		bool showError_   = true;


	public:
		/**
		 * ウィンドウを描く(閉じていても新しい行は写す)
		 * @param placeFloatingIfNew 配置の保存が無いとき、画面下の中央に浮かせて出すか
		 */
		void Draw(ConsoleLog& log, const bool placeFloatingIfNew);


	public:
		inline bool IsOpen() const               { return isOpen_; }
		inline void SetOpen(const bool isOpen)   { isOpen_ = isOpen; }


	private:
		/** ConsoleLog の新しい行を手元の写しへ足し、上限を超えた古い行を捨てる */
		void PullNewLines(const ConsoleLog& log);

		/** 上の操作の列(Clear・Auto-scroll・Copy・重要度の切り替えと件数・絞り込み) */
		void DrawToolbar(ConsoleLog& log);

		/** 行の本体(表示する行だけを ImGuiListClipper で描く) */
		void DrawLines();

		/** 重要度の切り替えと文字の絞り込みを通るか */
		bool IsVisible(const ConsoleLine& line) const;

		/** 表示中の行を改行でつないでクリップボードへ写す */
		void CopyVisibleToClipboard() const;
	};
}
