#include "aq.h"
// Android(NativeActivity)専用実装。他構成では本体をガードして空 TU にする
// (Win32 は PlatformWin32.cpp、UWP は PlatformUWP.cpp、Mac は PlatformMac.mm が代替)。
#if defined(AQ_PLATFORM_ANDROID)
#include "Platform/Android/PlatformAndroid.h"
#include "HID/Android/AndroidInputSink.h"
#include <android_native_app_glue.h>
#include <android/asset_manager.h>
#include <android/input.h>
#include <android/looper.h>
#include <android/native_window.h>
#include <sys/stat.h>
#include <chrono>
#include <cstdio>
#include <filesystem>


namespace aq
{
	namespace platform
	{
		namespace
		{
			/**
			 * 取り込むジョイスティックの軸。
			 *
			 * 左スティック(X / Y)・右スティック(Z / RZ)・トリガー(LTRIGGER / RTRIGGER。
			 * BRAKE / GAS で送ってくる機種もある)・十字キー(HAT_X / HAT_Y。
			 * DPAD キーを送らない機種向け)。
			 * モーションイベントには軸の一覧が入っていないので、こちらから引く軸を決めておく。
			 */
			static constexpr int32_t JOYSTICK_AXES[] =
			{
				AMOTION_EVENT_AXIS_X,
				AMOTION_EVENT_AXIS_Y,
				AMOTION_EVENT_AXIS_Z,
				AMOTION_EVENT_AXIS_RZ,
				AMOTION_EVENT_AXIS_LTRIGGER,
				AMOTION_EVENT_AXIS_RTRIGGER,
				AMOTION_EVENT_AXIS_BRAKE,
				AMOTION_EVENT_AXIS_GAS,
				AMOTION_EVENT_AXIS_HAT_X,
				AMOTION_EVENT_AXIS_HAT_Y,
			};

			/**
			 * パッドのボタンとして扱う入力ソース(クラスビットを除いた装置ビット)。
			 *
			 * 物理キーボードの矢印キーまで十字キー扱いにしないため装置側で絞る
			 * (Android では物理キーボード/マウスを想定しない)。クラスビットは
			 * キーボードもゲームパッドも同じ BUTTON なので、比較前に落とす必要がある。
			 */
			static constexpr int32_t PAD_KEY_SOURCES =
				(AINPUT_SOURCE_GAMEPAD | AINPUT_SOURCE_JOYSTICK | AINPUT_SOURCE_DPAD)
				& ~AINPUT_SOURCE_CLASS_MASK;
		}


		PlatformAndroid::PlatformAndroid(android_app* app)
			: app_(app)
			, window_(nullptr)
			, exitRequested_(false)
			, contentRoot_()
			, contentRootResolved_(false)
			, userDataDir_()
			, userDataDirResolved_(false)
		{
			if (app_ != nullptr)
			{
				// native_app_glue は C の関数ポインタしか受け取らないので、
				// userData に this を積んでサンク経由でメンバ関数へ転送する。
				app_->userData     = this;
				app_->onAppCmd     = &PlatformAndroid::AppCmdThunk;
				app_->onInputEvent = &PlatformAndroid::InputEventThunk;
			}
		}


		PlatformAndroid::~PlatformAndroid()
		{
			if (app_ != nullptr)
			{
				// glue はこの後もイベントを処理しうるので、解放済みの this を
				// 掴ませないようにコールバックを外す。
				app_->onAppCmd     = nullptr;
				app_->onInputEvent = nullptr;
				app_->userData     = nullptr;
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
			// 初回呼び出しで展開する。誰が最初に呼んでも成立するように、
			// 展開の起点はここ 1 箇所に寄せている(2 回目以降は即返る)。
			if (!contentRootResolved_)
			{
				EnsureContentExtracted();
			}
			return contentRoot_.empty() ? nullptr : contentRoot_.c_str();
		}


		bool PlatformAndroid::ReadAsset(const char* assetPath, std::string& out) const
		{
			out.clear();
			if (app_ == nullptr || app_->activity == nullptr
			    || app_->activity->assetManager == nullptr)
			{
				return false;
			}

			AAsset* asset = AAssetManager_open(app_->activity->assetManager,
			                                   assetPath, AASSET_MODE_STREAMING);
			if (asset == nullptr)
			{
				return false;
			}

			char buffer[8192];
			int  read = 0;
			while ((read = AAsset_read(asset, buffer, sizeof(buffer))) > 0)
			{
				out.append(buffer, static_cast<size_t>(read));
			}
			AAsset_close(asset);

			// read < 0 は読み取りエラー。途中まで積んだ内容は使えない。
			if (read < 0)
			{
				out.clear();
				return false;
			}
			return true;
		}


		bool PlatformAndroid::EnsureContentExtracted()
		{
			if (contentRootResolved_)
			{
				return !contentRoot_.empty();
			}
			contentRootResolved_ = true;

			if (app_ == nullptr || app_->activity == nullptr
			    || app_->activity->assetManager == nullptr
			    || app_->activity->internalDataPath == nullptr)
			{
				aq::StartupMark("[android] no asset manager / data path");
				return false;
			}

			const std::string base      = std::string(app_->activity->internalDataPath) + "/content";
			const std::string stampPath = base + "/asset_stamp.txt";

			// APK 側の印。パッケージング時に「ファイル数 合計バイト数」を書いてある。
			std::string apkStamp;
			if (!ReadAsset("asset_stamp.txt", apkStamp))
			{
				aq::StartupMark("[android] asset_stamp.txt not found in APK");
				return false;
			}

			// 展開先の印と一致すれば展開済み。APK を入れ替えると印が変わるので作り直される。
			{
				std::string diskStamp;
				if (std::FILE* fp = std::fopen(stampPath.c_str(), "rb"))
				{
					char   buffer[256];
					const size_t read = std::fread(buffer, 1, sizeof(buffer), fp);
					std::fclose(fp);
					diskStamp.assign(buffer, read);
				}
				if (!diskStamp.empty() && diskStamp == apkStamp)
				{
					contentRoot_ = base;
					aq::StartupMark("[android] assets already extracted");
					return true;
				}
			}

			std::string index;
			if (!ReadAsset("asset_index.txt", index))
			{
				aq::StartupMark("[android] asset_index.txt not found in APK");
				return false;
			}

			// 印が違う = 別の APK になっている。消えたファイルが残らないよう作り直す。
			std::error_code ec;
			std::filesystem::remove_all(base, ec);
			std::filesystem::create_directories(base, ec);

			const auto startTime = std::chrono::steady_clock::now();
			size_t     fileCount = 0;
			uint64_t   byteCount = 0;

			size_t lineBegin = 0;
			while (lineBegin < index.size())
			{
				size_t lineEnd = index.find('\n', lineBegin);
				if (lineEnd == std::string::npos)
				{
					lineEnd = index.size();
				}

				std::string relative = index.substr(lineBegin, lineEnd - lineBegin);
				lineBegin = lineEnd + 1;

				// CRLF で書かれていても読めるようにしておく。
				while (!relative.empty() && (relative.back() == '\r' || relative.back() == ' '))
				{
					relative.pop_back();
				}
				if (relative.empty())
				{
					continue;
				}

				AAsset* asset = AAssetManager_open(app_->activity->assetManager,
				                                   relative.c_str(), AASSET_MODE_STREAMING);
				if (asset == nullptr)
				{
					aq::StartupMarkf("[android] asset open failed: %s", relative.c_str());
					return false;
				}

				const std::filesystem::path destination = std::filesystem::path(base) / relative;
				std::filesystem::create_directories(destination.parent_path(), ec);

				std::FILE* fp = std::fopen(destination.c_str(), "wb");
				if (fp == nullptr)
				{
					AAsset_close(asset);
					aq::StartupMarkf("[android] asset write failed: %s", relative.c_str());
					return false;
				}

				char buffer[65536];
				int  read = 0;
				bool ok   = true;
				while ((read = AAsset_read(asset, buffer, sizeof(buffer))) > 0)
				{
					if (std::fwrite(buffer, 1, static_cast<size_t>(read), fp) != static_cast<size_t>(read))
					{
						ok = false;
						break;
					}
					byteCount += static_cast<uint64_t>(read);
				}
				std::fclose(fp);
				AAsset_close(asset);

				if (!ok || read < 0)
				{
					aq::StartupMarkf("[android] asset copy failed: %s", relative.c_str());
					return false;
				}
				++fileCount;
			}

			// 印は最後に書く。途中で失敗したら印が無いので次回やり直しになる。
			if (std::FILE* fp = std::fopen(stampPath.c_str(), "wb"))
			{
				std::fwrite(apkStamp.data(), 1, apkStamp.size(), fp);
				std::fclose(fp);
			}

			const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - startTime).count();
			aq::StartupMarkf("[android] extracted %zu files / %llu bytes in %lld ms",
			                 fileCount,
			                 static_cast<unsigned long long>(byteCount),
			                 static_cast<long long>(elapsed));

			contentRoot_ = base;
			return true;
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

			case APP_CMD_LOST_FOCUS:
				// 通知シェードや別アプリへ切り替わった。押しっぱなしのまま持ち越すと
				// 復帰後に勝手に動き続けるので、入力状態を落とす。
				hid::AndroidInputSink::Get().OnFocusLost();
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


		int32_t PlatformAndroid::OnInputEvent(AInputEvent* event)
		{
			if (event == nullptr)
			{
				return 0;
			}

			switch (AInputEvent_getType(event))
			{
			case AINPUT_EVENT_TYPE_MOTION:
			{
				// スティック操作もモーションイベントで届く。タッチと同じ扱いにすると
				// 画面を触っていない指が生えてしまうので、入力ソースで振り分ける。
				const int32_t sourceClass = AInputEvent_getSource(event) & AINPUT_SOURCE_CLASS_MASK;
				if (sourceClass == AINPUT_SOURCE_CLASS_JOYSTICK)
				{
					return OnJoystickMotion(event);
				}
				if (sourceClass == AINPUT_SOURCE_CLASS_POINTER)
				{
					return OnTouchMotion(event);
				}
				return 0;
			}

			case AINPUT_EVENT_TYPE_KEY:
				return OnPadKey(event);

			default:
				return 0;
			}
		}


		int32_t PlatformAndroid::OnTouchMotion(AInputEvent* event)
		{
			hid::AndroidInputSink& sink = hid::AndroidInputSink::Get();

			const int32_t action = AMotionEvent_getAction(event);
			const int32_t masked = action & AMOTION_EVENT_ACTION_MASK;

			// action の上位バイトには「変化したのが何番目のポインタか」が入っている。
			// POINTER_DOWN / POINTER_UP でのみ意味を持ち、DOWN / UP では 0 になる。
			const size_t pointerIndex = static_cast<size_t>(
				(action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);

			// ポインタの識別子は添字ではなく getPointerId で引く。添字は指を離すと詰められるが、
			// 識別子は離すまで変わらない(上位はこちらで同じ指を追う)。
			switch (masked)
			{
			case AMOTION_EVENT_ACTION_DOWN:
			case AMOTION_EVENT_ACTION_POINTER_DOWN:
				sink.OnTouchDown(AMotionEvent_getPointerId(event, pointerIndex),
				                 AMotionEvent_getX(event, pointerIndex),
				                 AMotionEvent_getY(event, pointerIndex));
				return 1;

			case AMOTION_EVENT_ACTION_MOVE:
			{
				// MOVE だけは「変化した 1 点」ではなく、触れている全点が入っている。
				const size_t pointerCount = AMotionEvent_getPointerCount(event);
				for (size_t i = 0; i < pointerCount; ++i)
				{
					sink.OnTouchMove(AMotionEvent_getPointerId(event, i),
					                 AMotionEvent_getX(event, i),
					                 AMotionEvent_getY(event, i));
				}
				return 1;
			}

			case AMOTION_EVENT_ACTION_UP:
			case AMOTION_EVENT_ACTION_POINTER_UP:
				sink.OnTouchUp(AMotionEvent_getPointerId(event, pointerIndex),
				               AMotionEvent_getX(event, pointerIndex),
				               AMotionEvent_getY(event, pointerIndex));
				return 1;

			case AMOTION_EVENT_ACTION_CANCEL:
				// ジェスチャが OS 側に奪われた。全点を無効にする(タップは成立させない)。
				sink.OnTouchCancel();
				return 1;

			default:
				// HOVER / SCROLL など。タッチスクリーンには来ない。
				return 0;
			}
		}


		int32_t PlatformAndroid::OnJoystickMotion(AInputEvent* event)
		{
			hid::AndroidInputSink& sink = hid::AndroidInputSink::Get();

			// 軸はポインタ 0 番から引く(ジョイスティックのポインタは常に 1 つ)。
			for (const int32_t axis : JOYSTICK_AXES)
			{
				sink.OnPadAxis(axis, AMotionEvent_getAxisValue(event, axis, 0));
			}
			return 1;
		}


		int32_t PlatformAndroid::OnPadKey(AInputEvent* event)
		{
			const int32_t keyCode = AKeyEvent_getKeyCode(event);

			// 戻るキーは飲み込まない。0 を返して glue -> NativeActivity へ流すことで
			// アプリが終了する(現状これが唯一の終了手段)。
			if (keyCode == AKEYCODE_BACK)
			{
				return 0;
			}

			if (((AInputEvent_getSource(event) & ~AINPUT_SOURCE_CLASS_MASK) & PAD_KEY_SOURCES) == 0)
			{
				return 0;
			}

			const int32_t action = AKeyEvent_getAction(event);
			if (action != AKEY_EVENT_ACTION_DOWN && action != AKEY_EVENT_ACTION_UP)
			{
				// ACTION_MULTIPLE は文字入力用。パッドからは来ない。
				return 0;
			}

			// 自動リピートの DOWN も素通しでよい(レベルは変わらないため)。
			// 写像に無いキーコードは消費しない = 0 を返す。
			const bool pressed = (action == AKEY_EVENT_ACTION_DOWN);
			return hid::AndroidInputSink::Get().OnPadButton(keyCode, pressed) ? 1 : 0;
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


		int32_t PlatformAndroid::InputEventThunk(android_app* app, AInputEvent* event)
		{
			if (app == nullptr || app->userData == nullptr)
			{
				return 0;
			}
			return static_cast<PlatformAndroid*>(app->userData)->OnInputEvent(event);
		}
	}
}

#endif // AQ_PLATFORM_ANDROID
