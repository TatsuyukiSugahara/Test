#include "aq.h"
// Apple(macOS / iOS)以外では空 TU。
#if defined(AQ_PLATFORM_APPLE)
#include "Sound/CoreAudio/CoreAudioSoundBackend.h"
#include "Sound/Mixer/MixerSoundVoice.h"

#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>

#if defined(AQ_PLATFORM_MAC)
// HAL(AudioObjectGetPropertyData / kAudioDevicePropertyLatency など)は macOS 専用。
// **iOS には <CoreAudio/CoreAudio.h> というアンブレラヘッダ自体が無い**ので、
// macOS のときだけ include する(設計書/iOS移植設計.md §6)。
#include <CoreAudio/CoreAudio.h>
#else
#import <Foundation/Foundation.h>
#import <AVFAudio/AVAudioSession.h>

// 本 TU は iOS ビルドでだけ Objective-C(AVAudioSession)を使う。手動参照カウント
// (MRR)前提で書いており、CMake は -fobjc-arc を渡していない。ARC を有効にすると
// retain / release がコンパイルエラーになるため、早期に落とす。
#if __has_feature(objc_arc)
#error "CoreAudioSoundBackend.mm は ARC 非対応です(-fobjc-arc を外してください)"
#endif
#endif


namespace aq
{
	namespace sound
	{
		namespace
		{
			/** 出力サンプルレート。SoftwareMixer の想定(§5)と揃える */
			constexpr uint32_t OUTPUT_SAMPLE_RATE = 48000u;

			/** 出力チャンネル数(ステレオ固定) */
			constexpr uint16_t OUTPUT_CHANNELS = 2u;


#if defined(AQ_PLATFORM_MAC)
			/** デバイスの UInt32 プロパティを 1 つ読む。取れなければ 0 */
			UInt32 GetDeviceUInt32(AudioObjectID device, AudioObjectPropertySelector selector)
			{
				AudioObjectPropertyAddress address{};
				address.mSelector = selector;
				address.mScope    = kAudioObjectPropertyScopeOutput;
				address.mElement  = kAudioObjectPropertyElementMain;

				UInt32 value = 0u;
				UInt32 size  = sizeof(value);
				if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &value) != noErr)
				{
					return 0u;
				}
				return value;
			}


			/**
			 * 出力の推定遅延 [秒] を求める(macOS)。
			 *
			 * `kAudioUnitProperty_Latency` は AudioUnit **自身**の遅延しか返さず、
			 * DefaultOutput では 0 になる。A/V 同期(`SoundStream`)が欲しいのは
			 * 「書いた PCM が実際に鳴るまで」なので、HAL 側の 3 つを足す:
			 *   デバイス遅延 + セーフティオフセット + バッファ長(いずれもフレーム)。
			 * どれか取れなくても 0 として扱い、全体が 0 になっても再生自体には影響しない。
			 */
			double QueryOutputLatencySeconds(AudioComponentInstance unit)
			{
				AudioObjectID device = kAudioObjectUnknown;
				UInt32        size   = sizeof(device);
				if (AudioUnitGetProperty(unit, kAudioOutputUnitProperty_CurrentDevice,
				                         kAudioUnitScope_Global, 0, &device, &size) != noErr
				    || device == kAudioObjectUnknown)
				{
					return 0.0;
				}

				const UInt32 deviceLatency = GetDeviceUInt32(device, kAudioDevicePropertyLatency);
				const UInt32 safetyOffset  = GetDeviceUInt32(device, kAudioDevicePropertySafetyOffset);
				const UInt32 bufferFrames  = GetDeviceUInt32(device, kAudioDevicePropertyBufferFrameSize);

				// デバイスの実サンプルレート(出力レートと違うことがある)。
				Float64 deviceRate = 0.0;
				{
					AudioObjectPropertyAddress address{};
					address.mSelector = kAudioDevicePropertyNominalSampleRate;
					address.mScope    = kAudioObjectPropertyScopeOutput;
					address.mElement  = kAudioObjectPropertyElementMain;
					UInt32 rateSize   = sizeof(deviceRate);
					if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &rateSize, &deviceRate) != noErr)
					{
						deviceRate = 0.0;
					}
				}
				if (deviceRate <= 0.0) { deviceRate = static_cast<Float64>(OUTPUT_SAMPLE_RATE); }

				const double totalFrames = static_cast<double>(deviceLatency)
				                         + static_cast<double>(safetyOffset)
				                         + static_cast<double>(bufferFrames);

				double seconds = totalFrames / static_cast<double>(deviceRate);

				// AudioUnit 自身の遅延も足す(通常 0 だが、取れるなら含める)。
				Float64 unitLatency = 0.0;
				UInt32  unitSize    = sizeof(unitLatency);
				if (AudioUnitGetProperty(unit, kAudioUnitProperty_Latency,
				                         kAudioUnitScope_Global, 0, &unitLatency, &unitSize) == noErr)
				{
					seconds += static_cast<double>(unitLatency);
				}
				return seconds;
			}
#else
			/** NSError の説明文。nil でも安全に %s へ渡せる文字列を返す */
			const char* ErrorText(NSError* error)
			{
				if (error == nil) { return "unknown"; }

				const char* text = [[error localizedDescription] UTF8String];
				return (text != nullptr) ? text : "unknown";
			}


			/**
			 * AVAudioSession を有効化する(iOS)。**これをやらないと音が出ない。**
			 *
			 * カテゴリは `AVAudioSessionCategoryAmbient`。ゲームの BGM / SE であって
			 * 音楽プレイヤーではないので、他アプリの再生を止めるべきではない
			 * (`Playback` は他アプリを止めてしまう。ミュートスイッチも無視する)。
			 *
			 * 冪等。中断からの復帰でも同じものを呼び直してよい。
			 * @return 有効化できたか(失敗しても再生は諦めない。音が出ないだけで動くべき)
			 */
			bool ActivateAudioSession()
			{
				AVAudioSession* session = [AVAudioSession sharedInstance];

				NSError* error = nil;
				if (![session setCategory:AVAudioSessionCategoryAmbient error:&error])
				{
					aq::StartupMarkf("  [sound] AVAudioSession のカテゴリ設定に失敗: %s", ErrorText(error));
				}

				error = nil;
				if (![session setActive:YES error:&error])
				{
					aq::StartupMarkf("  [sound] AVAudioSession の有効化に失敗: %s", ErrorText(error));
					return false;
				}
				return true;
			}


			/**
			 * AVAudioSession を無効化する(iOS)。
			 *
			 * `NotifyOthersOnDeactivation` を付けて、こちらが退いたことを他アプリへ知らせる
			 * (中断させていた相手が再生を再開できる)。失敗しても続行する。
			 */
			void DeactivateAudioSession()
			{
				NSError* error = nil;
				if (![[AVAudioSession sharedInstance] setActive:NO
				      withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation
				      error:&error])
				{
					aq::StartupMarkf("  [sound] AVAudioSession の無効化に失敗: %s", ErrorText(error));
				}
			}


			/**
			 * 出力の推定遅延 [秒] を求める(iOS)。
			 *
			 * HAL のデバイスプロパティ(`kAudioDevicePropertyLatency` ほか)は macOS 専用で
			 * iOS には無い。代わりに AVAudioSession から取り、macOS 側の
			 * 「デバイス遅延 + セーフティオフセット + バッファ長」と意味を揃える:
			 *   outputLatency(出力経路の遅延)+ IOBufferDuration(1 コールバック分の長さ)。
			 * 取れなければ 0(A/V 同期の精度が落ちるだけで再生には影響しない)。
			 */
			double QueryOutputLatencySeconds(AudioComponentInstance /*unit*/)
			{
				AVAudioSession* session = [AVAudioSession sharedInstance];

				const double outputLatency  = static_cast<double>([session outputLatency]);
				const double bufferDuration = static_cast<double>([session IOBufferDuration]);

				double seconds = 0.0;
				if (outputLatency  > 0.0) { seconds += outputLatency; }
				if (bufferDuration > 0.0) { seconds += bufferDuration; }
				return seconds;
			}
#endif


			/**
			 * AudioUnit のレンダーコールバック。**出力スレッド**から呼ばれる。
			 *
			 * インターリーブ float32 で開いているのでバッファは 1 本。
			 * `SoftwareMixer::Render` はロックも確保もしないので、そのまま書かせてよい。
			 */
			OSStatus RenderCallback(void*                       inRefCon,
			                        AudioUnitRenderActionFlags* /*ioActionFlags*/,
			                        const AudioTimeStamp*       /*inTimeStamp*/,
			                        UInt32                      /*inBusNumber*/,
			                        UInt32                      inNumberFrames,
			                        AudioBufferList*            ioData)
			{
				auto* backend = static_cast<CoreAudioSoundBackend*>(inRefCon);
				if (backend == nullptr || ioData == nullptr || ioData->mNumberBuffers == 0)
				{
					return noErr;
				}

				auto* out = static_cast<float*>(ioData->mBuffers[0].mData);
				if (out == nullptr)
				{
					return noErr;
				}

				backend->RenderFrames(out, static_cast<uint32_t>(inNumberFrames));
				return noErr;
			}
		}


		CoreAudioSoundBackend::CoreAudioSoundBackend()
			: outputUnit_(nullptr)
			, mixer_()
			, outputFormat_()
			, outputFrames_(0u)
			, latencySeconds_(0.0)
			, suspended_(false)
			, interrupted_(false)
			, outputRunning_(false)
#if defined(AQ_PLATFORM_IOS)
			, interruptionObserver_(nullptr)
#endif
			, initialized_(false)
		{
		}


		CoreAudioSoundBackend::~CoreAudioSoundBackend()
		{
			Finalize();
		}


		bool CoreAudioSoundBackend::Initialize()
		{
			if (initialized_) { return true; }

			outputFormat_.sampleRate    = OUTPUT_SAMPLE_RATE;
			outputFormat_.channels      = OUTPUT_CHANNELS;
			outputFormat_.bitsPerSample = 32u;
			outputFormat_.isFloat       = true;

			if (!mixer_.Initialize(outputFormat_))
			{
				aq::StartupLog("  [sound] SoftwareMixer の初期化に失敗");
				return false;
			}

#if defined(AQ_PLATFORM_IOS)
			// セッションの有効化は**出力ユニットを作る前**。非アクティブなセッションの
			// まま RemoteIO を開いても鳴らない。失敗しても続行する(§6)。
			ActivateAudioSession();
#endif

			// 出力ユニット。macOS は既定の出力デバイス(デバイスの切り替えは OS 側が
			// 面倒を見る)、iOS は RemoteIO。**iOS SDK にも DefaultOutput の定数宣言は
			// あるが動かない**ので、ここは必ず分岐させる(設計書/iOS移植設計.md §6)。
			AudioComponentDescription desc{};
			desc.componentType         = kAudioUnitType_Output;
#if defined(AQ_PLATFORM_MAC)
			desc.componentSubType      = kAudioUnitSubType_DefaultOutput;
#else
			desc.componentSubType      = kAudioUnitSubType_RemoteIO;
#endif
			desc.componentManufacturer = kAudioUnitManufacturer_Apple;

			AudioComponent component = AudioComponentFindNext(nullptr, &desc);
			if (component == nullptr)
			{
				aq::StartupLog("  [sound] 既定の出力 AudioUnit が見つからない");
				return false;
			}

			AudioComponentInstance unit = nullptr;
			if (AudioComponentInstanceNew(component, &unit) != noErr || unit == nullptr)
			{
				aq::StartupLog("  [sound] AudioComponentInstanceNew に失敗");
				return false;
			}
			outputUnit_ = unit;

#if defined(AQ_PLATFORM_IOS)
			// RemoteIO のバス(エレメント)番号は **出力 = 0 / 入力 = 1** で、DefaultOutput
			// と違って IO の有効/無効を持つ。既定が「出力は有効・入力は無効」なので
			// これを立てなくても鳴るが、下のフォーマット設定が触っているバス 0 が
			// 出力側であることを明示するために書いておく(失敗しても続行する)。
			// フォーマットは「ユニットへ流し込む側」なので、同じバス 0 の **Input スコープ**。
			{
				UInt32 enableOutput = 1u;
				if (AudioUnitSetProperty(unit, kAudioOutputUnitProperty_EnableIO,
				                         kAudioUnitScope_Output, 0, &enableOutput, sizeof(enableOutput)) != noErr)
				{
					aq::StartupMarkf("  [sound] RemoteIO の出力バス有効化に失敗(既定で有効なので続行)");
				}
			}
#endif

			// インターリーブ float32。kAudioFormatFlagIsNonInterleaved を立てないことで
			// 「1 バッファに L R L R ...」になり、ミキサの出力をそのまま書ける。
			AudioStreamBasicDescription format{};
			format.mSampleRate       = static_cast<Float64>(OUTPUT_SAMPLE_RATE);
			format.mFormatID         = kAudioFormatLinearPCM;
			format.mFormatFlags      = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
			format.mChannelsPerFrame = OUTPUT_CHANNELS;
			format.mBitsPerChannel   = 32u;
			format.mFramesPerPacket  = 1u;
			format.mBytesPerFrame    = format.mChannelsPerFrame * sizeof(float);
			format.mBytesPerPacket   = format.mBytesPerFrame * format.mFramesPerPacket;

			if (AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat,
			                         kAudioUnitScope_Input, 0, &format, sizeof(format)) != noErr)
			{
				aq::StartupLog("  [sound] 出力フォーマット(48kHz/float32/2ch interleaved)を設定できない");
				Finalize();
				return false;
			}

			AURenderCallbackStruct callback{};
			callback.inputProc       = &RenderCallback;
			callback.inputProcRefCon = this;
			if (AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback,
			                         kAudioUnitScope_Input, 0, &callback, sizeof(callback)) != noErr)
			{
				aq::StartupLog("  [sound] レンダーコールバックを設定できない");
				Finalize();
				return false;
			}

			if (AudioUnitInitialize(unit) != noErr)
			{
				aq::StartupLog("  [sound] AudioUnitInitialize に失敗");
				Finalize();
				return false;
			}

			// 出力遅延。取れなくても再生自体はできるので失敗は致命ではない。
			latencySeconds_ = QueryOutputLatencySeconds(unit);

			if (AudioOutputUnitStart(unit) != noErr)
			{
				aq::StartupLog("  [sound] AudioOutputUnitStart に失敗");
				Finalize();
				return false;
			}
			outputRunning_ = true;

#if defined(AQ_PLATFORM_IOS)
			// 購読はユニットが動き出してから(通知が半端な状態の this を叩かないように)。
			RegisterInterruptionObserver();
#endif

			initialized_ = true;
			aq::StartupLog("  [sound] CoreAudio 出力を開始 (48kHz / float32 / 2ch)");
			return true;
		}


		void CoreAudioSoundBackend::Finalize()
		{
#if defined(AQ_PLATFORM_IOS)
			// 解除は Initialize と対称に、ユニットを畳む前。解除し忘れると
			// 破棄済みの this へ通知が飛ぶ(Initialize の途中失敗でも必ず通る)。
			UnregisterInterruptionObserver();
#endif

			if (outputUnit_ != nullptr)
			{
				auto unit = static_cast<AudioComponentInstance>(outputUnit_);
				// 停止 → Uninitialize → Dispose の順。停止前に破棄するとコールバックが
				// 走っている最中に足元を崩すことになる。
				AudioOutputUnitStop(unit);
				AudioUnitUninitialize(unit);
				AudioComponentInstanceDispose(unit);
				outputUnit_ = nullptr;
			}
			outputRunning_ = false;
			suspended_     = false;
			interrupted_   = false;

#if defined(AQ_PLATFORM_IOS)
			// ユニットを手放してからセッションを降りる。
			DeactivateAudioSession();
#endif

			// initialized_ は見ない。Initialize が AudioUnit を開く前に失敗しても、
			// 先に初期化したミキサをここで畳む必要があるため(SoftwareMixer::Finalize は冪等)。
			mixer_.Finalize();
			initialized_ = false;
		}


		std::unique_ptr<ISoundVoice> CoreAudioSoundBackend::CreateVoice(const SoundFormat& format, SoundBusId bus)
		{
			const SoftwareMixer::VoiceId voiceId = mixer_.AcquireVoice(format, bus);
			if (voiceId == SoftwareMixer::INVALID_VOICE_ID)
			{
				return nullptr;
			}
			return std::make_unique<MixerSoundVoice>(&mixer_, voiceId);
		}


		void CoreAudioSoundBackend::SetBusVolume(SoundBusId bus, float volume)
		{
			mixer_.SetBusVolume(bus, volume);
		}


		void CoreAudioSoundBackend::SetMasterVolume(float volume)
		{
			mixer_.SetMasterVolume(volume);
		}


		SoundClock CoreAudioSoundBackend::GetOutputClock() const
		{
			SoundClock clock;
			clock.outputFrames   = outputFrames_.load(std::memory_order_relaxed);
			clock.sampleRate     = outputFormat_.sampleRate;
			clock.latencySeconds = latencySeconds_;
			clock.underrunCount  = mixer_.GetUnderrunCount();
			return clock;
		}


		void CoreAudioSoundBackend::Update()
		{
			// 出力スレッドで参照が切れた RefSoundClip の解放はここで行う(§5 / architecture.md §5)。
			mixer_.Update();
		}


		void CoreAudioSoundBackend::RenderFrames(float* out, uint32_t frames)
		{
			mixer_.Render(out, frames);
			outputFrames_.fetch_add(frames, std::memory_order_relaxed);
		}


		/**
		 * 停止理由(背面 / 中断)の現状に合わせて、出力ユニットを止める・動かす。
		 *
		 * 「背面へ回る」と「中断が来る」は**順序が前後しうる**(中断中にホームへ戻る等)。
		 * そこで理由をフラグで別々に持ち、**両方が下りたときだけ**動かす。実際の
		 * Start / Stop は `outputRunning_` と突き合わせるので、二重に止める / 二重に
		 * 再開することは無い。
		 *
		 * 止めるのは AudioUnit だけで、ミキサのボイス状態(読み位置・音量)は保つ。
		 * よって復帰後は続きから鳴る。
		 */
		void CoreAudioSoundBackend::ApplyOutputRunState()
		{
			if (outputUnit_ == nullptr) { return; }
			auto unit = static_cast<AudioComponentInstance>(outputUnit_);

			const bool shouldRun = !suspended_ && !interrupted_;
			if (shouldRun == outputRunning_) { return; }

			if (shouldRun)
			{
#if defined(AQ_PLATFORM_IOS)
				// 中断中に他アプリがセッションを持っていっているので、動かす前に取り直す。
				// 冪等なので背面からの復帰で通っても構わない。
				ActivateAudioSession();
#endif
				if (AudioOutputUnitStart(unit) != noErr)
				{
					// outputRunning_ は false のままにして、次に呼ばれたとき再試行させる。
					aq::StartupMarkf("  [sound] AudioOutputUnitStart に失敗(停止したまま続行)");
					return;
				}
				outputRunning_ = true;
			}
			else
			{
				AudioOutputUnitStop(unit);
				outputRunning_ = false;

				// AAudio 版(AAudioSoundBackend::OnSuspend)は「積んである分を捨てないと
				// 復帰の瞬間に直前の音が鳴り直して二重に聞こえる」として requestFlush する。
				// これは AAudio が **push 型**(こちらが先にバッファへ書き込んで積む)だから。
				// CoreAudio は **pull 型**で、レンダーコールバックが呼ばれた分しか PCM を
				// 作らない。こちら側に積んである PCM は無く、ミキサの読み位置も止めた時点の
				// ままなので、**AAudio の flush に相当する処理は原理的に要らない**。
				// ただし HAL / RemoteIO が内部に抱えた 1 バッファ分だけは残りうるので、
				// AudioUnitReset でそれだけ落としておく(CoreAudio での flush 相当)。
				AudioUnitReset(unit, kAudioUnitScope_Global, 0);
			}
		}


		/**
		 * 背面へ回った(`Engine::SyncSoundActivity` → `SoundEngine::OnSuspend`)。
		 *
		 * この経路は `IPlatform::IsRenderable()` の変化で呼ばれる。**P4 時点の
		 * `PlatformiOS::IsRenderable()` はまだ常に true を返す(P5 の作業)ので、
		 * iOS ではまだ呼ばれない。**P5 で `IsRenderable` が入った瞬間に効くよう、
		 * 先に実装しておく。macOS も `IsRenderable` は常に true なので現状は呼ばれないが、
		 * 将来呼ばれても壊れないよう同じコードを通す。
		 */
		void CoreAudioSoundBackend::OnSuspend()
		{
			suspended_ = true;
			ApplyOutputRunState();
		}


		void CoreAudioSoundBackend::OnResume()
		{
			suspended_ = false;

			// 中断の Ended を取りこぼした(ShouldResume が立っていなかった等)場合の
			// 逃げ道。前面へ戻ってこられたのなら中断はもう終わっている。
			interrupted_ = false;

			ApplyOutputRunState();
		}


#if defined(AQ_PLATFORM_IOS)
		/**
		 * 中断が始まった(電話着信・他アプリの再生)。
		 *
		 * OS は既にこちらのユニットを黙らせているが、明示的に止めないと復帰時の状態が
		 * 不定になる(止めないまま Start を重ねると二重再生の元になる)。
		 */
		void CoreAudioSoundBackend::OnInterruptionBegan()
		{
			interrupted_ = true;
			ApplyOutputRunState();
		}


		/**
		 * 中断が終わった。
		 * @param shouldResume `AVAudioSessionInterruptionOptionShouldResume` が立っていたか
		 */
		void CoreAudioSoundBackend::OnInterruptionEnded(const bool shouldResume)
		{
			// ShouldResume が無いのは「まだ鳴らすな」という OS の指示なので止めたままにする。
			// この取りこぼしは前面復帰(OnResume)が拾う。
			if (!shouldResume)
			{
				aq::StartupMarkf("  [sound] 中断が終了(ShouldResume 無し)。停止したまま待つ");
				return;
			}

			interrupted_ = false;

			// ApplyOutputRunState が setActive:YES をやり直してから Start する。
			ApplyOutputRunState();
		}


		/**
		 * `AVAudioSessionInterruptionNotification` を購読する。
		 *
		 * 配送先を **mainQueue** に固定しているのは、`OnSuspend` / `OnResume`(CADisplayLink
		 * が回すフレーム = メインスレッド)と同じスレッドへ寄せて、停止理由のフラグを
		 * 排他無しで触れるようにするため。
		 *
		 * MRR なので `addObserverForName:` が返すオブザーバ(autorelease されている)は
		 * 自分で retain して持ち、`UnregisterInterruptionObserver` で release する。
		 */
		void CoreAudioSoundBackend::RegisterInterruptionObserver()
		{
			if (interruptionObserver_ != nullptr) { return; }

			CoreAudioSoundBackend* backend = this;
			id observer = [[NSNotificationCenter defaultCenter]
				addObserverForName:AVAudioSessionInterruptionNotification
				            object:[AVAudioSession sharedInstance]
				             queue:[NSOperationQueue mainQueue]
				        usingBlock:^(NSNotification* notification)
				{
					NSDictionary* userInfo = [notification userInfo];
					NSNumber*     typeValue = [userInfo objectForKey:AVAudioSessionInterruptionTypeKey];
					if (typeValue == nil) { return; }

					const NSUInteger type = [typeValue unsignedIntegerValue];
					if (type == AVAudioSessionInterruptionTypeBegan)
					{
						backend->OnInterruptionBegan();
					}
					else if (type == AVAudioSessionInterruptionTypeEnded)
					{
						NSNumber* optionValue = [userInfo objectForKey:AVAudioSessionInterruptionOptionKey];
						const bool shouldResume = (optionValue != nil)
						                        && (([optionValue unsignedIntegerValue]
						                             & AVAudioSessionInterruptionOptionShouldResume) != 0);
						backend->OnInterruptionEnded(shouldResume);
					}
				}];

			interruptionObserver_ = [observer retain];
		}


		void CoreAudioSoundBackend::UnregisterInterruptionObserver()
		{
			if (interruptionObserver_ == nullptr) { return; }

			id observer = static_cast<id>(interruptionObserver_);
			[[NSNotificationCenter defaultCenter] removeObserver:observer];
			[observer release];
			interruptionObserver_ = nullptr;
		}
#endif // AQ_PLATFORM_IOS
	}
}
#endif // AQ_PLATFORM_APPLE
