#pragma once
#include <cstdint>
#include <atomic>
#include <array>
#include <vector>
#include <memory>
#include "Sound/SoundTypes.h"
#include "Sound/SoundFwd.h"


namespace aq
{
	namespace sound
	{
		/**
		 * プラットフォーム非依存のソフトウェアミキサ(Mac移植設計 §5)
		 *
		 * 論理ボイスの集合を出力フォーマット(48kHz / float / 2ch 想定)へミックスする。
		 * CoreAudio / Oboe のような「PCM を要求してくるだけ」の出力 API の下で
		 * XAudio2 のソースボイス相当の意味論を成立させるための本体。
		 *
		 * スレッド境界(architecture.md §5 / Sound設計 §3.3 (d)(e)):
		 *  - 投入側(サウンドスレッド)からのボイス操作はすべて SPSC コマンドリングへ積み、
		 *    Render() の冒頭で drain して適用する。順序は投入順どおりに保たれる。
		 *  - PCM の投入はボイスごとの SPSC ブロックリング(コピー)で渡す。
		 *  - Render() はロック取得・メモリ確保・解放・IO を一切行わない。
		 *  - 出力スレッド側で参照が切れる RefSoundClip は回収リングへ退避し、
		 *    Update()(サウンドスレッド)が解放する。
		 *  - プロデューサはサウンドスレッド 1 本に限る(SPSC 前提。Sound設計 §3.3(e))。
		 *
		 * 本クラスは ISoundVoice / ISoundBackend を実装しない純粋な部品。
		 * P4 の CoreAudioSoundVoice / CoreAudioSoundBackend が薄いアダプタとして委譲する。
		 */
		class SoftwareMixer
		{
			/**
			 * 公開型
			 */
		public:
			/** ボイス識別子(= スロット番号)。無効値は INVALID_VOICE_ID */
			using VoiceId = uint32_t;


			/**
			 * 公開定数
			 */
		public:
			static constexpr VoiceId INVALID_VOICE_ID = 0xffffffffu;

			/** 同時に確保できる論理ボイス数(固定プール。実行時に確保・解放しない) */
			static constexpr uint32_t MAX_VOICE_COUNT = 64u;

			/** 出力チャンネル数の上限 */
			static constexpr uint32_t MAX_OUTPUT_CHANNEL_COUNT = 8u;

			/** 入力(ソース)チャンネル数の上限 */
			static constexpr uint32_t MAX_SOURCE_CHANNEL_COUNT = 8u;

			/** ボイス 1 本あたりの投入ブロック数(SubmitBuffer のキュー深度) */
			static constexpr uint32_t VOICE_BLOCK_COUNT = 16u;

			/** 出力行列の要素数(出力ch × 入力ch) */
			static constexpr uint32_t MATRIX_ELEMENT_COUNT = MAX_OUTPUT_CHANNEL_COUNT * MAX_SOURCE_CHANNEL_COUNT;


			/**
			 * 内部型
			 */
		private:
			/** スロットの占有状態。Acquiring の間は Render がそのスロットを見ない */
			enum class SlotState : uint8_t
			{
				Free,
				Acquiring,
				Active,
			};

			/** 再生状態(Sound設計 §3.3(b) の状態機械) */
			enum class PlayState : uint8_t
			{
				Idle,
				Playing,
				Paused,
			};

			/** サウンドスレッド → 出力スレッドのコマンド種別 */
			enum class CommandType : uint8_t
			{
				None,
				Start,
				Pause,
				Resume,
				Stop,
				SetVolume,
				SetFrequencyRatio,
				SetOutputMatrix,
				SubmitClipRegion,
			};


			/** SPSC コマンドリングの 1 要素。実行時確保を避けるため行列は固定長で持つ */
			struct Command
			{
				CommandType  type        = CommandType::None;
				VoiceId      voiceId     = INVALID_VOICE_ID;
				float        value       = 0.0f;
				uint32_t     flushIndex  = 0u;      // Stop 発行時点の writeIndex(それ以前の投入だけ破棄する)
				uint16_t     srcChannels = 0u;
				uint16_t     dstChannels = 0u;
				uint64_t     startFrame  = 0u;
				uint64_t     frameCount  = 0u;
				LoopRegion   loop;
				bool         endOfStream = false;
				RefSoundClip clip;
				float        matrix[MATRIX_ELEMENT_COUNT] = {};
			};


			/** SubmitBuffer のコピー先ブロック。data の resize はプロデューサ側だけが行う */
			struct Block
			{
				std::vector<uint8_t> data;
				uint32_t             byteSize    = 0u;
				bool                 endOfStream = false;
			};


			/** 論理ボイス 1 本 */
			struct Voice
			{
				/** 両スレッド共有 */
				std::atomic<uint8_t>  slotState{ static_cast<uint8_t>(SlotState::Free) };
				std::atomic<uint64_t> consumedFrames{ 0u };
				std::atomic<bool>     finished{ false };

				/** サウンドスレッドが立て、Render がスロットを片付けてから Free へ戻す */
				std::atomic<bool>     releaseRequested{ false };

				/** 投入リング(producer = サウンドスレッド / consumer = Render) */
				std::array<Block, VOICE_BLOCK_COUNT> blocks;
				std::atomic<uint32_t>                writeIndex{ 0u };
				std::atomic<uint32_t>                readIndex{ 0u };

				/** Acquire 時に確定し、Release まで不変 */
				SoundFormat format;
				SoundBusId  bus = SoundBusId::Master;

				/** サウンドスレッドのみが触る */
				bool submitClosed = false;

				/** 以下は Render スレッドのみが触る */
				PlayState playState      = PlayState::Idle;
				float     volume         = 1.0f;
				float     frequencyRatio = 1.0f;

				uint16_t matrixSrcChannels = 0u;
				uint16_t matrixDstChannels = 0u;
				float    matrix[MATRIX_ELEMENT_COUNT] = {};

				/** ゼロコピー再生中のクリップ領域 */
				RefSoundClip   clip;
				const uint8_t* clipPcm         = nullptr;
				uint64_t       clipPos         = 0u;
				uint64_t       clipRegionEnd   = 0u;
				LoopRegion     loop;
				uint32_t       loopDoneCount   = 0u;
				bool           clipActive      = false;
				bool           clipEndOfStream = false;

				/** 投入ブロックの消費位置 */
				uint32_t blockFrameOffset = 0u;

				/** 消費フレーム数の Render スレッド側カウンタ(1 フレームごとの atomic RMW を避ける) */
				uint64_t renderConsumedFrames = 0u;

				/** 線形リサンプルの内部状態(prevFrame と curFrame の間を phase で補間) */
				double phase  = 0.0;
				bool   primed = false;
				float  prevFrame[MAX_SOURCE_CHANNEL_COUNT] = {};
				float  curFrame[MAX_SOURCE_CHANNEL_COUNT]  = {};
			};


			/**
			 * 内部定数
			 */
		private:
			/** コマンドリングの深度 */
			static constexpr uint32_t COMMAND_QUEUE_CAPACITY = 256u;

			/** クリップ回収リングの深度 */
			static constexpr uint32_t RETIRE_QUEUE_CAPACITY = 128u;


			/**
			 * メンバ変数
			 */
		private:
			/** 出力フォーマット(インタリーブ float。channels <= MAX_OUTPUT_CHANNEL_COUNT) */
			SoundFormat outputFormat_;
			bool        initialized_ = false;

			/** 論理ボイスの固定プール */
			std::array<Voice, MAX_VOICE_COUNT> voices_;

			/** コマンドリング(SPSC) */
			std::array<Command, COMMAND_QUEUE_CAPACITY> commands_;
			std::atomic<uint32_t>                       commandWrite_{ 0u };
			std::atomic<uint32_t>                       commandRead_{ 0u };

			/** クリップ回収リング(producer = Render / consumer = Update) */
			std::array<RefSoundClip, RETIRE_QUEUE_CAPACITY> retired_;
			std::atomic<uint32_t>                           retireWrite_{ 0u };
			std::atomic<uint32_t>                           retireRead_{ 0u };

			/** バス音量とマスタ音量 */
			std::atomic<float> busVolume_[static_cast<size_t>(SoundBusId::Count)];
			std::atomic<float> masterVolume_{ 1.0f };

			/** 統計(出力スレッドが更新し、任意のスレッドが読む) */
			std::atomic<uint64_t> renderedFrames_{ 0u };
			std::atomic<uint64_t> underrunCount_{ 0u };
			std::atomic<uint64_t> retireOverflowCount_{ 0u };


			/**
			 * 生成・初期化
			 */
		public:
			SoftwareMixer();
			~SoftwareMixer();

			SoftwareMixer(const SoftwareMixer&)            = delete;
			SoftwareMixer& operator=(const SoftwareMixer&) = delete;

			/**
			 * 出力フォーマットを確定して使用可能にする
			 * @param outputFormat 32bit float・channels <= MAX_OUTPUT_CHANNEL_COUNT のみ受け付ける
			 * @return 成功で true
			 */
			bool Initialize(const SoundFormat& outputFormat);

			/** 全ボイスを解放して停止する。**出力スレッドを止めてから**呼ぶこと */
			void Finalize();

			inline bool               IsInitialized()   const { return initialized_; }
			inline const SoundFormat& GetOutputFormat() const { return outputFormat_; }


			/**
			 * ボイス操作(サウンドスレッドから呼ぶ)
			 */
		public:
			/** 空きスロットを 1 本確保する。空きが無ければ INVALID_VOICE_ID */
			VoiceId AcquireVoice(const SoundFormat& format, const SoundBusId bus);

			/** スロットを返却する。実際の解放は Render が行う(コールバック実行中の破棄を避ける) */
			void ReleaseVoice(const VoiceId voiceId);

			/** PCM 投入(コピー)。満杯なら WouldBlock(Sound設計 §3.3(a)) */
			SubmitResult SubmitBuffer(const VoiceId voiceId, const void* data, const uint32_t byteSize, const bool endOfStream);

			/** 常駐クリップのゼロコピー再生。clip はボイスが保持して生存保証する */
			SubmitResult SubmitClipRegion(const VoiceId voiceId, RefSoundClip clip, const uint64_t startFrame,
			                              const uint64_t frameCount, const LoopRegion& loop, const bool endOfStream);

			void Start(const VoiceId voiceId);
			void Pause(const VoiceId voiceId);
			void Resume(const VoiceId voiceId);
			void Stop(const VoiceId voiceId);

			void SetVoiceVolume(const VoiceId voiceId, const float volume);
			void SetFrequencyRatio(const VoiceId voiceId, const float ratio);

			/**
			 * 出力ルーティング行列を設定する
			 * @param matrix XAudio2 の SetOutputMatrix と同じ並び。matrix[dstIndex * srcChannels + srcIndex]
			 */
			void SetOutputMatrix(const VoiceId voiceId, const uint32_t srcChannels, const uint32_t dstChannels, const float* matrix);

			/** そのボイスが鳴らし終えた入力フレーム数(media clock の anchor) */
			uint64_t GetConsumedFrames(const VoiceId voiceId) const;

			/** 未消費の投入ブロック数(背圧制御用) */
			uint32_t GetQueuedBufferCount(const VoiceId voiceId) const;

			/** endOfStream 投入後にキューが尽きたか */
			bool IsFinished(const VoiceId voiceId) const;


			/**
			 * バス / マスタ
			 */
		public:
			void SetBusVolume(const SoundBusId bus, const float volume);
			void SetMasterVolume(const float volume);

			float GetBusVolume(const SoundBusId bus) const;
			inline float GetMasterVolume() const { return masterVolume_.load(std::memory_order_relaxed); }


			/**
			 * ポンプ
			 */
		public:
			/** サウンドスレッド: 出力スレッドが手放したクリップ参照を解放する */
			void Update();

			/**
			 * 出力スレッド: frames フレーム分をインタリーブ float で書き出す
			 * @param out    outputFormat_.channels * frames 要素以上のバッファ
			 * @param frames 出力フレーム数
			 */
			void Render(float* out, const uint32_t frames);

			/** 出力済みフレーム総数(GetOutputClock の outputFrames に使う) */
			inline uint64_t GetRenderedFrames() const { return renderedFrames_.load(std::memory_order_relaxed); }

			/** 供給切れ回数 */
			inline uint64_t GetUnderrunCount() const { return underrunCount_.load(std::memory_order_relaxed); }

			/** 回収リング溢れ回数(0 でなければ Update() の呼び出し頻度が足りていない) */
			inline uint64_t GetRetireOverflowCount() const { return retireOverflowCount_.load(std::memory_order_relaxed); }


			/**
			 * 内部処理
			 */
		private:
			/** Active なスロットだけを返す。範囲外/未確保なら nullptr */
			Voice*       GetVoice(const VoiceId voiceId);
			const Voice* GetVoice(const VoiceId voiceId) const;

			/** コマンドリングの空きスロットを取る(プロデューサ専用)。満杯なら nullptr */
			Command* AcquireCommandSlot();
			/** AcquireCommandSlot で埋めたスロットを公開する */
			void     PublishCommand();

			/** Render 冒頭のコマンド適用 */
			void DrainCommands();
			void ApplyCommand(Command& command);

			/** 1 ボイスを out へ加算ミックスする */
			void MixVoice(Voice& voice, float* out, const uint32_t frames, const float gain);

			/** ソースから 1 フレーム取り出して dst(float × srcChannels)へ書く。供給切れなら false */
			bool FetchSourceFrame(Voice& voice, float* dst);

			/** 再生位置・リサンプル状態を初期化する(Render スレッド) */
			void ResetPlaybackState(Voice& voice);

			/** クリップ参照を回収リングへ退避する(Render スレッド) */
			void RetireClip(RefSoundClip&& clip);

			/** 回収リングを空にする(サウンドスレッド) */
			void DrainRetired();
		};
	}
}
