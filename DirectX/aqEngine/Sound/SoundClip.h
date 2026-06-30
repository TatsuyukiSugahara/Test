#pragma once
#include <cstdint>
#include <vector>
#include "Resource/Resource.h"
#include "SoundTypes.h"


namespace aq
{
	namespace sound
	{
		// SoundClip が data_ に保持するデコード済み実体。
		struct SoundClipData
		{
			SoundFormat          format;
			std::vector<uint8_t> pcm;        // デコード済み PCM（全展開）
			LoopRegion           defaultLoop; // 既定ループ（未設定ならループ無効）
		};


		// 常駐・不変・複数ソース共有のデコード済みクリップ（§5.1）。
		// 既存 ResourceManager の Load<SoundClip>() 非同期ロード/キャッシュに乗る。
		class SoundClip : public res::ResourceBase
		{
			engineResource(aq::sound::SoundClip);

		public:
			SoundClip() : res::ResourceBase() { data_ = new SoundClipData(); }
			virtual ~SoundClip()
			{
				delete static_cast<SoundClipData*>(data_);
				data_ = nullptr;
			}

			const SoundFormat& GetFormat() const { return Get()->format; }
			const uint8_t*     GetPcm()    const { return Get()->pcm.data(); }
			uint32_t           GetPcmByteSize() const { return static_cast<uint32_t>(Get()->pcm.size()); }

			uint64_t GetFrameCount() const
			{
				const uint32_t bpf = Get()->format.BytesPerFrame();
				return bpf == 0u ? 0u : static_cast<uint64_t>(Get()->pcm.size()) / bpf;
			}

			const LoopRegion& GetDefaultLoop() const { return Get()->defaultLoop; }

			// ローダから書き込むための非 const アクセス。
			SoundClipData* GetMutable() { return Get(); }

		private:
			inline SoundClipData* Get() const { return static_cast<SoundClipData*>(data_); }
		};
		using RefSoundClipResource = std::shared_ptr<SoundClip>;


		// SoundClip 読み込みローダ（§5.1）。ResourceManager::Reflection<SoundClip, SoundClipLoader>() で登録する。
		// 拡張子で全展開デコーダを選ぶ: .wav は WavDecoder、それ以外（mp3/aac/wma/m4a 等）は
		// Media Foundation（Windows）。
		class SoundClipLoader : public res::ResourceLoaderBase
		{
		public:
			SoundClipLoader() = default;
			~SoundClipLoader() = default;

			virtual res::ResourceBase* Create() override { return new SoundClip(); }

		private:
			bool Loading() override;
		};
	}
}
