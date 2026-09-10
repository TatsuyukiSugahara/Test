#pragma once
// macOS 専用。他構成では中身を空にして、既存ビルドに一切影響させない。
#if defined(AQ_PLATFORM_MAC)
#include <cstdint>
#include <vector>
#include "ISoundDecoder.h"


namespace aq
{
	namespace sound
	{
		/**
		 * AudioToolbox の `ExtAudioFile` を使ったデコーダ(設計書/Mac移植設計.md §5)。
		 *
		 * Windows の `MFDecoder` に対応する位置づけで、mp3 / aac / m4a / caf など
		 * **OS が対応する形式**を PCM(16bit 符号付き整数)へデコードする。
		 * ストリーミング(`ISoundDecoder`)と全展開(`DecodeFileFully`)の両対応も同じ。
		 *
		 * 出力を 16bit 整数に揃えているのは `MFDecoder` と合わせるため。
		 * `ExtAudioFile` の「クライアントフォーマット」機能が変換を担うので、
		 * こちらでリサンプルやビット深度変換を書く必要はない。
		 *
		 * スレッド: `ResourceManager` のワーカースレッドから使われる。`ExtAudioFile` は
		 * インスタンスごとに独立しているので、1 インスタンスを 1 スレッドで使う限り安全。
		 */
		class ExtAudioFileDecoder : public ISoundDecoder
		{
		// ── メンバ変数 ──
		private:
			/** ExtAudioFileRef。CoreAudio 型をヘッダへ出さないため void* で持つ */
			void*       file_        = nullptr;
			SoundFormat format_;
			uint64_t    totalFrames_ = 0u;
			uint64_t    readFrames_  = 0u;
			bool        atEnd_       = false;

		// ── メンバ関数 ──
		public:
			ExtAudioFileDecoder() = default;
			~ExtAudioFileDecoder() override;

			bool               Open(const char* path) override;
			const SoundFormat& GetFormat()      const override { return format_; }
			uint64_t           GetTotalFrames() const override { return totalFrames_; }
			uint32_t           ReadFrames(void* dst, uint32_t maxFrames) override;
			bool               Seek(uint64_t frame) override;
			bool               IsEnd()          const override { return atEnd_; }

		// ── static ──
		public:
			/** path を全展開デコードして PCM とフォーマットを取り出す(`SoundClip` 用) */
			static bool DecodeFileFully(const char* path, SoundFormat& outFormat, std::vector<uint8_t>& outPcm);

		private:
			void Close();
		};
	}
}
#endif // AQ_PLATFORM_MAC
