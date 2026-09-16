#pragma once
#include <cstdint>
#include <vector>
#include "Rendering/RenderTargetHandle.h"


namespace aq
{
	namespace rendering
	{
		/**
		 * 掲示板のキー。エンジン定義は PassResourceKeys に、ゲーム独自は
		 * MakePassKey("Game.Xxx") で作る(接頭辞でエンジンのキーと衝突しない)。
		 */
		using PassResourceKey = uint32_t;


		/**
		 * キー文字列から PassResourceKey を作る(FNV-1a 32bit、constexpr)。
		 * aqHash32 とは別物。掲示板のキー同士で一致すればよいので独立させてある。
		 */
		constexpr PassResourceKey MakePassKey(const char* str)
		{
			uint32_t hash = 2166136261u;
			for (; *str != '\0'; ++str) {
				hash ^= static_cast<uint32_t>(static_cast<unsigned char>(*str));
				hash *= 16777619u;
			}
			return hash;
		}


		/**
		 * エンジンが定義するキー(設計書/レンダーパイプライン設計.md §1.2)
		 */
		namespace PassResourceKeys
		{
			/** シーン RT(メイン RT かオフスクリーン RT)。パイプライン生成時に外から与える */
			constexpr PassResourceKey Scene        = MakePassKey("aq.Scene");
			/** シーンの深度を持つ RT(= GBuffer0)。GBufferPass が書く */
			constexpr PassResourceKey Depth        = MakePassKey("aq.Depth");
			/** G-Buffer 各面。GBufferPass が書く */
			constexpr PassResourceKey GBuffer0     = MakePassKey("aq.GBuffer0");
			constexpr PassResourceKey GBuffer1     = MakePassKey("aq.GBuffer1");
			constexpr PassResourceKey GBuffer2     = MakePassKey("aq.GBuffer2");
			constexpr PassResourceKey GBuffer3     = MakePassKey("aq.GBuffer3");
			/** GBuffer2 の別名(ワールド座標)。ポストプロセスとゲームパスが読む */
			constexpr PassResourceKey WorldPos     = MakePassKey("aq.WorldPos");
			/** Hi-Z ピラミッドの読み戻し面。HiZPass が書く */
			constexpr PassResourceKey HiZ          = MakePassKey("aq.HiZ");
			/** ポストプロセスの入出力(ピンポン)。P2 で各ポストパスが使う */
			constexpr PassResourceKey PostInput    = MakePassKey("aq.PostInput");
			constexpr PassResourceKey PostOutput   = MakePassKey("aq.PostOutput");
			/** Bloom の結果(Tonemap の第 2 入力)。P2 */
			constexpr PassResourceKey BloomTexture = MakePassKey("aq.BloomTexture");
			/** 最終出力。RenderThread::Submit の displayRT に渡す。最後に書いたパスの RT が採用される */
			constexpr PassResourceKey Output       = MakePassKey("aq.Output");
		}




		/**
		 * パス間で RT ハンドルを受け渡す掲示板。
		 * RT ハンドルだけを扱う(シャドウマップの IDepthMap は今どおり FrameContext 経由)。
		 * 数が少ないので線形探索。Setup の順に登録され、Build 中に読まれる。
		 */
		class PassResources
		{
		private:
			struct Entry
			{
				PassResourceKey    key;
				RenderTargetHandle handle;
			};

			std::vector<Entry> entries_;


		public:
			/** 登録。同じキーがあれば上書き */
			void Set(const PassResourceKey key, const RenderTargetHandle handle);

			/** 取得。無ければ INVALID なハンドル */
			RenderTargetHandle Get(const PassResourceKey key) const;

			/** 登録済みか */
			bool Has(const PassResourceKey key) const;

			/** 全消去 */
			inline void Clear() { entries_.clear(); }
		};
	}
}
