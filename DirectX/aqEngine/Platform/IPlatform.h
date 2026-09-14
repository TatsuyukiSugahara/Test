#pragma once
#include <cstdint>
#include <functional>
#include "Graphics/GraphicsTypes.h"

namespace aq
{
	namespace platform
	{
		// メインウィンドウ生成の要求パラメータ。
		// Win32 では通常ウィンドウ、UWP では CoreWindow のサイズヒントとして使う。
		struct WindowDesc
		{
			int32_t     width  = 1280;
			int32_t     height = 720;
			const char* title  = "Application";
		};

		// プラットフォーム抽象。
		// ウィンドウ生成・イベントループ・ライフサイクル・コンテンツ基点を隠蔽し、
		// Engine から Win32 / UWP / GDK の差分を切り離す。
		// 実装: PlatformWin32(現行 / 道A の回帰確認用), PlatformUWP(道A 本命, 将来),
		//       PlatformGDK(道B, 将来)。
		class IPlatform
		{
		public:
			virtual ~IPlatform() = default;

			// メインウィンドウを生成し、プラットフォーム非依存ハンドルを out に返す。
			// Win32: HWND、UWP: CoreWindow^ を void* として格納する。
			virtual bool CreateMainWindow(const WindowDesc& desc, aq::graphics::NativeWindowHandle& out) = 0;

			// 保留中のイベントを処理する。終了要求を受けたら false を返す。
			virtual bool PumpEvents() = 0;

			/**
			 * フレーム駆動をプラットフォームへ委譲する。
			 *
			 * 既定実装はデスクトップ相当:PumpEvents() が false を返すまで frame() を回す。
			 * Win32 / UWP / Mac / Android はメインループを自分で所有できるので、これで足りる。
			 *
			 * この IF が要るのは **iOS だけがループを所有できない**ため。UIKit では
			 * UIApplicationMain が run loop を握って戻ってこないので、呼び出し側で
			 * `while (PumpEvents())` を回す形が構造的に成立しない。そこで iOS 実装は
			 * これを override して CADisplayLink にフレーム駆動を登録し、
			 * **すぐ return する**(以降は OS がコールバックでフレームを進める)。
			 * 設計書/iOS移植設計.md §3.3。
			 *
			 * @param frame 1 フレーム分の処理。既定実装の間だけ有効な参照ではなく、
			 *              すぐ return する実装ではコピーして保持すること。
			 */
			virtual void RunFrameLoop(const std::function<void()>& frame)
			{
				while (PumpEvents()) { frame(); }
			}

			// PLM ライフサイクル。UWP では suspend 中にメモリを 128MB 以下へ落とす起点。
			// Win32 では未使用（既定実装は何もしない）。
			virtual void OnSuspend() {}
			virtual void OnResume()  {}

			// 描画してよい状態か。false の間、Engine はフレームを丸ごと飛ばす。
			// Android はバックグラウンドへ回ると ANativeWindow を取り上げられ、提示先が
			// 無い状態になる。窓を手放さないプラットフォーム(Win32 / UWP / Mac)は常に true。
			virtual bool IsRenderable() const { return true; }

			// 描画対象のウィンドウが差し替わったことを 1 回だけ取り出す(読み取りで消費するラッチ)。
			// true を返したときだけ out に新しいハンドルが入り、Engine はサーフェスと
			// スワップチェーンを作り直してから次のフレームへ進む。
			// 同じ窓を使い続けるプラットフォームでは起こらないので既定は false。
			virtual bool ConsumeSurfaceChanged(aq::graphics::NativeWindowHandle& /*out*/) { return false; }

			// アセット読み込みの基点パス。
			// Win32: ソースツリー / 実行ディレクトリ、UWP: パッケージ install フォルダ。
			virtual const char* GetContentRoot() = 0;

			// ユーザーデータ(セーブ・設定・ゴーストなど)を書き込んでよいディレクトリ。
			// 末尾にセパレータを含む。存在しなければ作成し、用意できなければ nullptr を返す
			// (GetContentRoot と同じ流儀)。Assets/ は読み取り専用の配布物なので使えない。
			//
			// 「アプリ名のサブフォルダを付けるか」は各実装の判断とする。Win32 / Mac は
			// ユーザーホームを他アプリと共有するのでアプリ名の階層が要るが、Android / iOS は
			// アプリのコンテナ自体が既にアプリ専用で、そこへさらにアプリ名を足すのは冗長なため。
			// 戻り値はプラットフォーム初期化後にしか確定しない(Android の書き込み可能パスは
			// Activity 由来で実行時にしか分からない)。よってコンパイル時定数や、初期化前に
			// 呼べる静的関数の形にはしない。
			virtual const char* GetUserDataDirectory() = 0;
		};
	}
}
