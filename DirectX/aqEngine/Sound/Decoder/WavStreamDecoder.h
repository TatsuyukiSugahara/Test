#pragma once
#include <cstdio>
#include "ISoundDecoder.h"


namespace aq
{
	namespace sound
	{
		// WAV を逐次読みするストリーミングデコーダ（§5）。data チャンクを
		// 先頭から少しずつ読み出す。圧縮 WAV は非対応（WavDecoder と同じ範囲）。
		class WavStreamDecoder : public ISoundDecoder
		{
		// ── メンバ変数 ──
		private:
			FILE*       file_ = nullptr;
			SoundFormat format_;
			long        dataOffset_ = 0;   // data チャンク本体のファイル先頭オフセット
			uint32_t    dataBytes_  = 0;   // data チャンクのバイト数
			uint32_t    readBytes_  = 0;   // 読み出し済みバイト数

		// ── メンバ関数 ──
		public:
			WavStreamDecoder() = default;
			~WavStreamDecoder() override;

			bool               Open(const char* path) override;
			const SoundFormat& GetFormat() const override { return format_; }
			uint64_t           GetTotalFrames() const override;
			uint32_t           ReadFrames(void* dst, uint32_t maxFrames) override;
			bool               Seek(uint64_t frame) override;
			bool               IsEnd() const override { return readBytes_ >= dataBytes_; }

		private:
			void Close();
		};
	}
}
