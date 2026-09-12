#pragma once
// Android(NativeActivity + native_app_glue)向けプラットフォーム実装。
// 他構成では中身を空にして、既存ビルドに一切影響させない。
#if defined(AQ_PLATFORM_ANDROID)
#include <string>
#include "Platform/IPlatform.h"

// native_app_glue の C 構造体。Android のヘッダをこのヘッダへ持ち込まないため前方宣言する。
struct android_app;
struct ANativeWindow;
struct AInputEvent;

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

			/** アセット基点(APK から展開したディレクトリ)。直下に Game/Assets/... が並ぶ */
			std::string contentRoot_;
			bool        contentRootResolved_;

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
			/**
			 * APK の assets を内部ストレージへ展開する(展開済みなら何もしない)。
			 *
			 * APK 内の assets は通常のファイルパスでは開けないが、既存のリソース読み込みは
			 * すべて fopen / std::filesystem 前提で書かれている。ファイル IO 全体を
			 * 抽象化する代わりに、初回起動時に一度コピーしてしまう方式を採る。
			 * AAssetManager はディレクトリ列挙ができないため、APK 側に同梱した
			 * ファイル一覧(asset_index.txt)を読んで 1 本ずつ取り出す。
			 *
			 * @return 展開後の基点が使える状態なら true
			 */
			bool EnsureContentExtracted();

			/** APK 内の 1 ファイルを丸ごと読む。見つからなければ false */
			bool ReadAsset(const char* assetPath, std::string& out) const;

			/** native_app_glue の onAppCmd から呼ばれる実体 */
			void OnAppCmd(int32_t cmd);

			/**
			 * native_app_glue の onInputEvent から呼ばれる実体。
			 * イベントを種類ごとに振り分けて AndroidInputSink へ投入する。
			 *
			 * @return 処理したら 1、していなければ 0(0 のときは OS の既定動作に流れる)
			 */
			int32_t OnInputEvent(AInputEvent* event);

			/** 画面タッチ(ポインタ系のモーションイベント)。複数指をまとめて投入する */
			int32_t OnTouchMotion(AInputEvent* event);

			/** スティック / トリガー(ジョイスティック系のモーションイベント) */
			int32_t OnJoystickMotion(AInputEvent* event);

			/** パッドのボタン(キーイベント) */
			int32_t OnPadKey(AInputEvent* event);

			/** 保留中のイベントを 1 巡処理する。blockUntilEvent = true ならイベントが来るまで待つ */
			void PollOnce(bool blockUntilEvent);


		private:
			/** native_app_glue へ渡す C 関数。app->userData から this を取り出して転送する */
			static void AppCmdThunk(android_app* app, int32_t cmd);

			/** 同上。入力イベント用 */
			static int32_t InputEventThunk(android_app* app, AInputEvent* event);
		};
	}
}
#endif // AQ_PLATFORM_ANDROID
