#pragma once
#include <cstdint>
#include <string>


namespace aq
{
	/**
	 * 起動モード(-mode=)
	 */
	enum class LaunchMode : uint8_t
	{
		Play,
		Edit,   // 受け取って保持するだけ。まだ未実装なので play として動く
	};




	/**
	 * 起動引数の解釈結果。
	 *
	 * 形式は `-key=value`(`--key=value` も同じ扱い)。値の中の空白は `"` で囲む。
	 * 知らないキーや値の誤りはログに出して無視し、既定値のまま残す。
	 * プラットフォームには依存しない(引数を渡せるのは Win32 の lpCmdLine と Mac の argv)。
	 */
	struct LaunchOptions
	{
		/** 起動するモジュール名(-app=)。空なら登録順の先頭 */
		std::string appName;

		/** 起動モード(-mode=play|edit) */
		LaunchMode mode = LaunchMode::Play;

		/** エディタ連携の待ち受けポート(-editor-port=)。0 なら無効 */
		uint16_t editorPort = 0;

		/** 埋め込み先の親ウィンドウ(-parent-hwnd=)。Win32 のみ有効。0 なら埋め込まない */
		uintptr_t parentWindow = 0;

		/**
		 * 固定の FPS(-fixed-fps=)。0 なら実時間。指定すると、デルタ時間が実時間に関わらず 1 / fixedFps 秒になる。
		 * 画面比較のため、時間で動く物の進みをフレーム数で決まるようにする(撮影用。設計書/エンジン課題解消設計.md §4.3)。
		 */
		uint32_t fixedFps = 0;

		/**
		 * 更新を止めるフレーム番号(-freeze-frame=)。0 なら止めない。
		 * この番号のフレームを描き終えたら Update を呼ばなくなり、最後に提示した画面が残る(撮影用)。
		 * 外部からのスクリーンショットを、フレーム数で決まる同じ画面に揃えるために使う。
		 */
		uint32_t freezeFrame = 0;


		/**
		 * argv 形式の引数を解釈する
		 * @param argc 引数の個数
		 * @param argv 引数の配列。argv[0] はプログラム名として読み飛ばす
		 */
		static LaunchOptions Parse(int argc, const char* const* argv);

		/**
		 * 1 本のコマンドライン文字列を解釈する(Win32 の lpCmdLine 用)
		 * @param cmdLine プログラム名を含まないコマンドライン。nullptr や空文字列なら既定値
		 */
		static LaunchOptions ParseCommandLine(const char* cmdLine);
	};
}
