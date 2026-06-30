#pragma once
#include <cstdint>
#include <vector>
#include "ISoundDecoder.h"

struct IMFSourceReader;


namespace aq
{
	namespace sound
	{
		// Media Foundation を使ったデコーダ（Windows ネイティブ）。MP3 / AAC / WMA /
		// M4A など OS が対応するフォーマットを PCM(16bit) へデコードする（§5）。
		// ストリーミング（ISoundDecoder）と全展開（DecodeFileFully）の両対応。
		// MF/COM の初期化は本クラスが自前で行う（ResourceManager のワーカースレッドでも安全）。
		class MFDecoder : public ISoundDecoder
		{
		// ── メンバ変数 ──
		private:
			IMFSourceReader*     reader_ = nullptr;   // 所有する（cpp で Release）
			SoundFormat          format_;
			uint64_t             totalFrames_   = 0;
			std::vector<uint8_t> pending_;        // 直近 MF サンプルの未消費バイト
			size_t               pendingOffset_  = 0;
			bool                 atEnd_          = false;
			bool                 mfStarted_      = false;
			bool                 comInitialized_ = false;

		// ── メンバ関数 ──
		public:
			MFDecoder() = default;
			~MFDecoder() override;

			bool               Open(const char* path) override;
			const SoundFormat& GetFormat() const override { return format_; }
			uint64_t           GetTotalFrames() const override { return totalFrames_; }
			uint32_t           ReadFrames(void* dst, uint32_t maxFrames) override;
			bool               Seek(uint64_t frame) override;
			bool               IsEnd() const override { return atEnd_ && pendingOffset_ >= pending_.size(); }

			// path を全展開デコードして PCM とフォーマットを取り出す（SoundClip 用）。
			static bool DecodeFileFully(const char* path, SoundFormat& outFormat, std::vector<uint8_t>& outPcm);

		private:
			void Close();
			// 次の MF サンプルを pending_ へ読み込む。EOS なら false。
			bool FillPending();
		};
	}
}
