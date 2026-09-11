#pragma once
// Metal のパイプラインステートキャッシュ(設計書/MetalBackend設計.md §4)。
//
// **本ヘッダは Objective-C++ 専用**で、Graphics/Metal/ 配下の .mm からのみ include する
// (MetalCommon.h が __OBJC__ を検査している)。
#if defined(ENGINE_GRAPHICS_METAL)
#include "Graphics/Metal/MetalCommon.h"
#include <unordered_map>


namespace aq
{
	namespace graphics
	{
		/**
		 * PSO のキャッシュキー(設計書 §4.1)
		 *
		 * **DepthMode はキーに入れない**。Metal では深度/ステンシルは PSO ではなく
		 * MTLDepthStencilState という別オブジェクトで、エンコーダへ独立に設定する
		 * (MetalDepthStencilCache がその作り置き)。ビューポート/シザーも同じく PSO 外なので、
		 * Vulkan の VkPipeline より組み合わせ爆発が小さい。
		 *
		 * **vsFunction / psFunction を const void* で持つのは、id<MTLFunction> の
		 * ポインタ同値でキャッシュを引くため**。MTLFunction の寿命は MetalShader が握っており、
		 * キャッシュ側は retain しない(シェーダが先に死ぬ場面では Clear() を呼ぶこと)。
		 */
		struct MetalPipelineKey
		{
			/** シェーダ関数(id<MTLFunction>)。深度のみのパスでは psFunction は nullptr */
			const void* vsFunction = nullptr;
			const void* psFunction = nullptr;

			/** ラスタライズ/出力マージ状態 */
			uint8_t topologyClass = 0;   // MTLPrimitiveTopologyClass
			uint8_t blendMode     = 0;   // BlendMode
			uint8_t colorCount    = 0;   // 有効なカラーアタッチメント数

			/** アタッチメントのフォーマット。深度が無いパスでは depthFormat は Invalid */
			MTLPixelFormat colorFormat[8] = {};
			MTLPixelFormat depthFormat    = MTLPixelFormatInvalid;

			/** 頂点ストリームの stride(リフレクション値ではなく実際にバインドする VB の値) */
			uint32_t vertexStride   = 0;
			uint32_t instanceStride = 0;


			bool operator==(const MetalPipelineKey& other) const;
		};




		/**
		 * MetalPipelineKey のハッシュ
		 *
		 * VulkanPipelineCache と同じ FNV-1a だが、**構造体全体の memcmp / memhash ではなく
		 * メンバごとに畳み込む**。MetalPipelineKey は const void* と uint8_t が混在していて
		 * パディングが入るため、丸ごとバイト列として見ると「等しいのにハッシュが違う」
		 * キーが生まれうる(unordered_map が同じキーを二重に持つ)。
		 */
		struct MetalPipelineKeyHash
		{
			size_t operator()(const MetalPipelineKey& key) const;
		};




		/**
		 * MTLRenderPipelineState / MTLComputePipelineState のキャッシュ
		 *
		 * Draw の flush 時に遅延生成する(設計書 §4.2)。Metal の PSO 生成は実測で
		 * 数 ms かかることがあるため、初回フレームのヒッチが問題になったら
		 * P6 で事前生成を検討する。
		 *
		 * 参照カウントは MRR(ARC ではない)。生成した PSO は +1 で保持し、
		 * Clear() / デストラクタで release する。
		 */
		class MetalPipelineCache
		{
		private:
			using PipelineMap = std::unordered_map<MetalPipelineKey, id<MTLRenderPipelineState>, MetalPipelineKeyHash>;
			using ComputeMap  = std::unordered_map<const void*, id<MTLComputePipelineState>>;

			/** Metal オブジェクト */
			id<MTLDevice> device_;

			/** キャッシュ本体 */
			PipelineMap pipelineMap_;
			ComputeMap  computeMap_;


		public:
			explicit MetalPipelineCache(id<MTLDevice> device);
			~MetalPipelineCache();

			MetalPipelineCache(const MetalPipelineCache&)            = delete;
			MetalPipelineCache& operator=(const MetalPipelineCache&) = delete;


		public:
			/**
			 * 無ければ作ってキャッシュする
			 *
			 * @param key        PSO キー
			 * @param vertexDesc VS の MTLVertexDescriptor(MetalShader が持つもの。頂点入力が無ければ nil)
			 * @return 生成済みの PSO。失敗したら nil(エラーの本文はログへ出す)
			 */
			id<MTLRenderPipelineState> GetOrCreate(const MetalPipelineKey& key, MTLVertexDescriptor* vertexDesc);

			/**
			 * compute 用(P4)。無ければ作ってキャッシュする。
			 *
			 * 描画側と違い**キーは MTLFunction のポインタだけ**。Metal の compute PSO は
			 * 関数 1 本で決まり、アタッチメントもブレンドも頂点レイアウトも関与しないため。
			 *
			 * なお **threadsPerThreadgroup はここから取れない**。
			 * MTLComputePipelineState の threadExecutionWidth / maxTotalThreadsPerThreadgroup は
			 * ハードウェアの都合の値で、HLSL の [numthreads(...)] とは無関係
			 * (MetalRenderContextImpl が .spv のリフレクションで別途求める)。
			 *
			 * @param cs 対象の MTLFunction
			 * @return 生成済みの compute PSO。失敗したら nil(エラーの本文はログへ出す)
			 */
			id<MTLComputePipelineState> GetOrCreateCompute(id<MTLFunction> cs);

			/** 保持している PSO をすべて捨てる(シェーダを作り直す前に呼ぶこと) */
			void Clear();
		};
	}
}
#endif // ENGINE_GRAPHICS_METAL
