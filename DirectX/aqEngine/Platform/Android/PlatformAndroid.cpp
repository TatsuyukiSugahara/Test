#include "aq.h"
// Android(NativeActivity)専用実装。他構成では本体をガードして空 TU にする
// (Win32 は PlatformWin32.cpp、UWP は PlatformUWP.cpp、Mac は PlatformMac.mm が代替)。
#if defined(AQ_PLATFORM_ANDROID)
#include "Platform/Android/PlatformAndroid.h"
#include <android_native_app_glue.h>
#include <android/looper.h>
#include <android/native_window.h>
#include <sys/stat.h>


namespace aq
{
	namespace platform
	{
		PlatformAndroid::PlatformAndroid(android_app* app)
			: app_(app)
			, window_(nullptr)
			, exitRequested_(false)
			, contentRoot_()
			, userDataDir_()
			, userDataDirResolved_(false)
		{
			if (app_ != nullptr)
			{
				// native_app_glue は C の関数ポインタしか受け取らないので、
				// userData に this を積んでサンク経由でメンバ関数へ転送する。
				app_->userData = this;
				app_->onAppCmd = &PlatformAndroid::AppCmdThunk;
			}
		}


		PlatformAndroid::~PlatformAndroid()
		{
			if (app_ != nullptr)
			{
				// glue はこの後もイベントを処理しうるので、解放済みの this を
				// 掴ませないようにコールバックを外す。
				app_->onAppCmd = nullptr;
				app_->userData = nullptr;
			}
		}


		bool PlatformAndroid::CreateMainWindow(const WindowDesc& desc, aq::graphics::NativeWindowHandle& out)
		{
			// Win32 / Mac と違い、ウィンドウはこちらからは作れない。
			// 要求サイズも OS が決めるので desc は使わない(画面全体になる)。
			(void)desc;

			if (app_ == nullptr)
			{
				return false;
			}

			// APP_CMD_INIT_WINDOW が来るまでイベントを回して待つ。ここで待たないと
			// 描画対象が無い状態で Vulkan のサーフェスを作ろうとして失敗する。
			aq::StartupMark("[android] waiting for window");
			while (window_ == nullptr && !exitRequested_)
			{
				PollOnce(true);
			}

			if (window_ == nullptr)
			{
				aq::StartupMark("[android] window never arrived");
				return false;
			}

			out.handle = window_;
			aq::StartupMarkf("[android] window ok (%dx%d)",
			                 ANativeWindow_getWidth(window_),
			                 ANativeWindow_getHeight(window_));
			return true;
		}


		bool PlatformAndroid::PumpEvents()
		{
			// ウィンドウが無い間はフレームを回しても提示先が無い。スピンして電池を
			// 食うだけなので、イベントが来るまでブロックして待つ。
			// 「描画できない間はメインループを止める」形を IPlatform へ持ち上げるのは
			// サーフェス再生成とまとめて扱う必要があるため後続フェーズで行う。
			PollOnce(!IsRenderable());

			if (app_ != nullptr && app_->destroyRequested != 0)
			{
				exitRequested_ = true;
			}
			return !exitRequested_;
		}


		const char* PlatformAndroid::GetContentRoot()
		{
			// APK 内の assets は通常のファイルパスでは開けないため、アセットを
			// 端末のストレージへ展開してその場所を返す方式にする。
			// 展開そのものは未実装なので、今は「基点なし」を返す。
			// TODO(P2): APK の assets を内部ストレージへ展開し、そのパスを返す。
			return contentRoot_.empty() ? nullptr : contentRoot_.c_str();
		}


		const char* PlatformAndroid::GetUserDataDirectory()
		{
			if (!userDataDirResolved_)
			{
				userDataDirResolved_ = true;

				if (app_ != nullptr && app_->activity != nullptr
				    && app_->activity->internalDataPath != nullptr)
				{
					userDataDir_ = app_->activity->internalDataPath;
					if (!userDataDir_.empty() && userDataDir_.back() != '/')
					{
						userDataDir_ += '/';
					}

					// internalDataPath はアプリ専用のコンテナなので、ここへさらに
					// アプリ名の階層は足さない(IPlatform の契約どおり)。
					// 通常は OS が用意済みで、その場合 mkdir は EEXIST になるだけ。
					mkdir(userDataDir_.c_str(), S_IRWXU | S_IRWXG);
				}
			}
			return userDataDir_.empty() ? nullptr : userDataDir_.c_str();
		}


		void PlatformAndroid::OnAppCmd(int32_t cmd)
		{
			switch (cmd)
			{
			case APP_CMD_INIT_WINDOW:
				window_ = (app_ != nullptr) ? app_->window : nullptr;
				aq::StartupMark("[android] cmd INIT_WINDOW");
				break;

			case APP_CMD_TERM_WINDOW:
				// 描画対象が破棄される。復帰時には別のウィンドウが渡ってくるため、
				// サーフェス/スワップチェーンの作り直しが必要になる(後続フェーズ)。
				window_ = nullptr;
				aq::StartupMark("[android] cmd TERM_WINDOW");
				break;

			case APP_CMD_PAUSE:
				OnSuspend();
				break;

			case APP_CMD_RESUME:
				OnResume();
				break;

			case APP_CMD_DESTROY:
				exitRequested_ = true;
				aq::StartupMark("[android] cmd DESTROY");
				break;

			default:
				break;
			}
		}


		void PlatformAndroid::PollOnce(bool blockUntilEvent)
		{
			if (app_ == nullptr)
			{
				return;
			}

			// ALooper_pollOnce は識別子(>= 0)を返したときだけ処理対象がある。
			// 負値は WAKE / TIMEOUT などで、その時点で溜まっているものは無い。
			int timeoutMs = blockUntilEvent ? -1 : 0;
			for (;;)
			{
				int                  events = 0;
				android_poll_source* source = nullptr;
				const int result = ALooper_pollOnce(timeoutMs, nullptr, &events,
				                                    reinterpret_cast<void**>(&source));
				if (result < 0)
				{
					break;
				}

				if (source != nullptr)
				{
					source->process(app_, source);
				}
				if (app_->destroyRequested != 0)
				{
					exitRequested_ = true;
					break;
				}

				// 1 巡目でブロックしたら、2 巡目以降は溜まっている分だけ捌いて抜ける。
				timeoutMs = 0;
			}
		}


		void PlatformAndroid::AppCmdThunk(android_app* app, int32_t cmd)
		{
			if (app == nullptr || app->userData == nullptr)
			{
				return;
			}
			static_cast<PlatformAndroid*>(app->userData)->OnAppCmd(cmd);
		}
	}
}

#endif // AQ_PLATFORM_ANDROID
