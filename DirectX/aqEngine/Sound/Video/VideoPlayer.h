#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include "Sound/SoundTypes.h"


namespace aq
{
	namespace sound { class SoundStream; class MFDecoder; }

	namespace video
	{
		// 動画/メディアの再生（音声側。§11）。Media Foundation で**音声トラック**をデコードし、
		// SoundEngine のプッシュ駆動ストリームへ供給する。A/V 同期用に media クロックを露出する。
		//
		// ※ 映像フレームのデコード/表示は別サブシステム（レンダ統合）として未実装。
		//    本クラスは設計 §11 の「音声側 = プッシュ型ストリーム + media clock」を担う。
		//    .wav/.mp3/.aac/.m4a/.mp4 等 MF が読める形式が対象。
		class VideoPlayer
		{
		public:
			VideoPlayer();
			~VideoPlayer();

			VideoPlayer(const VideoPlayer&) = delete;
			VideoPlayer& operator=(const VideoPlayer&) = delete;

			// メディアを開いて音声再生を開始する。成功で true。
			bool Open(const char* path, sound::SoundBusId bus = sound::SoundBusId::BGM);
			// 毎フレーム: デコードした音声 PCM をプッシュ供給する。
			void Update(float deltaTime);
			void Stop();

			bool IsPlaying() const { return playing_; }
			// A/V 同期の master clock（いま聞こえている音声の media 時刻）。
			sound::MediaClock GetClock() const;

		private:
			std::unique_ptr<sound::MFDecoder>   audio_;
			std::unique_ptr<sound::SoundStream> stream_;
			sound::SoundFormat                  format_;
			std::vector<uint8_t>                scratch_;
			std::vector<uint8_t>                pending_;   // 背圧で押し戻された未供給分
			bool                                playing_ = false;
		};
	}
}
