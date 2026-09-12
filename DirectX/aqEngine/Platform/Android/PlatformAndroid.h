#pragma once
// Android(NativeActivity + native_app_glue)向けプラットフォーム実装。
// 他構成では中身を空にして、既存ビルドに一切影響させない。
#if defined(AQ_PLATFORM_ANDROID)
#include <string>
#include "Platform/IPlatform.h"

// native_app_glue の C 構造体。Android のヘッダをこのヘッダへ持ち込まないため前方宣言する。
struct android_app;
struct ANativeWindow;

namespace aq
{
	namespace platform
	{
		// NativeActivity の上で IPlatform を実装する。
		//
		// Win32 / Mac と決定的に違うのは「ウィンドウをこちらから作らない」こと。
		// アプリ起動直後は描画対象が無く、OS が APP_CMD_INIT_WINDOW を投げてきた時点で
		// 初めて ANativeWindow が手に入る。そのため CreateMainWindow はイベントを
		// 回して窓が来るまで待つ実装になる。
		//
		// NativeWindowHandle には ANativeWindow* を格納する
		// (Vulkan の VK_KHR_android_surface が要求するのがこれ)。
		class PlatformAndroid : public IPlatform
		{
		// ── メンバ変数 ──
		private:
			/** native_app_glue が渡してくるアプリ状態(所有しない) */
			android_app* app_;

			/** 現在の描画対象。バックグラウンドでは nullptr になる */
			ANativeWindow* window_;

			/** 終了要求(Activity の破棄要求) */
			bool exitRequested_;

			/** アセット基点。展開方式にするまでは空(GetContentRoot は nullptr を返す) */
			std::string contentRoot_;

			/** ユーザーデータ(セーブ等)の書き込み先。末尾セパレータ付き */
			std::string userDataDir_;
			bool        userDataDirResolved_;


		// ── メンバ関数 ──
		public:
			explicit PlatformAndroid(android_app* app);
			~PlatformAndroid() override;


		public:
			bool        CreateMainWindow(const WindowDesc& desc, aq::graphics::NativeWindowHandle& out) override;
			bool        PumpEvents() override;
			const char* GetContentRoot() override;
			const char* GetUserDataDirectory() override;

			/** 描画可能か(ウィンドウが生きているか)。false の間はフレームを回してはいけない */
			inline bool IsRenderable() const { return window_ != nullptr; }

			/** 現在の ANativeWindow。ウィンドウが無ければ nullptr */
			inline ANativeWindow* GetWindow() const { return window_; }


		private:
			/** native_app_glue の onAppCmd から呼ばれる実体 */
			void OnAppCmd(int32_t cmd);

			/** 保留中のイベントを 1 巡処理する。blockUntilEvent = true ならイベントが来るまで待つ */
			void PollOnce(bool blockUntilEvent);


		private:
			/** native_app_glue へ渡す C 関数。app->userData から this を取り出して転送する */
			static void AppCmdThunk(android_app* app, int32_t cmd);
		};
	}
}
#endif // AQ_PLATFORM_ANDROID
