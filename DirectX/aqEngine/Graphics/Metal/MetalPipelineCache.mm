#include "aq.h"
// Metal のパイプラインステートキャッシュ。他構成では本体をガードして空 TU にする。
#if defined(ENGINE_GRAPHICS_METAL)
#include "Graphics/Metal/MetalPipelineCache.h"
#include <cstdio>

// 本 TU は手動参照カウント(MRR)前提で書いている。CMake は -fobjc-arc を渡していない。
#if __has_feature(objc_arc)
#error "MetalPipelineCache.mm は ARC 非対応です(-fobjc-arc を外してください)"
#endif


namespace aq
{
	namespace graphics
	{
		namespace
		{
			/** FNV-1a(VulkanPipelineCache と同じ定数) */
			static constexpr size_t FNV_OFFSET_BASIS = 1469598103934665603ull;
			static constexpr size_t FNV_PRIME        = 1099511628211ull;

			/** StartupLog 1 行の上限(MetalShader.mm と同じ扱い) */
			static constexpr size_t LOG_LINE_MAX = 320;


			/** バイト列を FNV-1a で畳み込む */
			void HashBytes(size_t& hash, const void* data, const size_t size)
			{
				const uint8_t* bytes = static_cast<const uint8_t*>(data);
				for (size_t i = 0; i < size; ++i) {
					hash ^= bytes[i];
					hash *= FNV_PRIME;
				}
			}


			/** 値 1 つを FNV-1a で畳み込む(パディングを跨がないようメンバ単位で呼ぶ) */
			template <typename T>
			void HashValue(size_t& hash, const T& value)
			{
				HashBytes(hash, &value, sizeof(T));
			}


			/**
			 * 複数行のメッセージを 1 行ずつログへ流す。
			 *
			 * MTLRenderPipelineState の生成失敗は NSError の localizedDescription に
			 * 「頂点属性 n に対応するバッファが無い」等が**複数行**で載る。
			 * aq::StartupMark は 1 行で切るので、まとめて渡すと先頭しか残らない
			 * (MetalShader.mm の LogMultiLine と同じ理由・同じ作法)。
			 */
			void LogMultiLine(const char* prefix, const char* text)
			{
				if (text == nullptr) { return; }

				const std::string source(text);
				size_t begin = 0;
				while (begin <= source.size()) {
					size_t end = source.find('\n', begin);
					if (end == std::string::npos) { end = source.size(); }

					const size_t length = (end - begin < LOG_LINE_MAX) ? (end - begin) : LOG_LINE_MAX;
					if (length > 0) {
						char line[512];
						std::snprintf(line, sizeof(line), "%s%.*s", prefix, static_cast<int>(length), source.c_str() + begin);
						aq::StartupLog(line);
					}

					if (end == source.size()) { break; }
					begin = end + 1;
				}
			}


			/** const void* で持っている id<MTLFunction> を取り出す(MRR なのでブリッジ不要) */
			id<MTLFunction> ToFunction(const void* pointer)
			{
				return static_cast<id<MTLFunction>>(const_cast<void*>(pointer));
			}
		}


		/**
		 * PSO のキャッシュキー
		 */
		bool MetalPipelineKey::operator==(const MetalPipelineKey& other) const
		{
			// パディングを含めた memcmp にしないのは MetalPipelineKeyHash のコメントと同じ理由。
			if (vsFunction     != other.vsFunction)     { return false; }
			if (psFunction     != other.psFunction)     { return false; }
			if (topologyClass  != other.topologyClass)  { return false; }
			if (blendMode      != other.blendMode)      { return false; }
			if (colorCount     != other.colorCount)     { return false; }
			if (depthFormat    != other.depthFormat)    { return false; }
			if (vertexStride   != other.vertexStride)   { return false; }
			if (instanceStride != other.instanceStride) { return false; }

			// colorCount を超えた分は PSO に渡らないが、キーの作り手が消し忘れた
			// 残骸で別キー扱いになるのを避けたいので**全 8 本を比較する**
			// (使わない要素は既定値 Invalid のまま残っているのが正しい状態)。
			for (uint32_t i = 0; i < 8; ++i) {
				if (colorFormat[i] != other.colorFormat[i]) { return false; }
			}
			return true;
		}


		size_t MetalPipelineKeyHash::operator()(const MetalPipelineKey& key) const
		{
			size_t hash = FNV_OFFSET_BASIS;
			HashValue(hash, key.vsFunction);
			HashValue(hash, key.psFunction);
			HashValue(hash, key.topologyClass);
			HashValue(hash, key.blendMode);
			HashValue(hash, key.colorCount);
			HashValue(hash, key.depthFormat);
			HashValue(hash, key.vertexStride);
			HashValue(hash, key.instanceStride);
			for (uint32_t i = 0; i < 8; ++i) {
				HashValue(hash, key.colorFormat[i]);
			}
			return hash;
		}


		/************************************/




		/**
		 * MTLRenderPipelineState / MTLComputePipelineState のキャッシュ
		 */
		MetalPipelineCache::MetalPipelineCache(id<MTLDevice> device)
			: device_([device retain])
			, pipelineMap_()
			, computeMap_()
		{
		}


		MetalPipelineCache::~MetalPipelineCache()
		{
			Clear();
			[device_ release];
			device_ = nil;
		}


		id<MTLRenderPipelineState> MetalPipelineCache::GetOrCreate(const MetalPipelineKey& key, MTLVertexDescriptor* vertexDesc)
		{
			PipelineMap::const_iterator it = pipelineMap_.find(key);
			if (it != pipelineMap_.end()) { return it->second; }

			if (device_ == nil || key.vsFunction == nullptr) {
				aq::StartupLog("[MetalPipelineCache] VS が nil のため PSO を生成できません");
				return nil;
			}

			id<MTLRenderPipelineState> pipeline = nil;

			@autoreleasepool {
				MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];

				desc.vertexFunction   = ToFunction(key.vsFunction);
				desc.fragmentFunction = (key.psFunction != nullptr) ? ToFunction(key.psFunction) : nil;

				// List / Strip は PSO 側では潰れる(MetalCommon.h の ToMTLTopologyClass 参照)。
				desc.inputPrimitiveTopology = static_cast<MTLPrimitiveTopologyClass>(key.topologyClass);

				// カラーアタッチメント。ブレンドは全 RT へ同じモードを適用する
				// (Vulkan 側も rtCount 本すべてに同じ VkPipelineColorBlendAttachmentState を配っている)。
				const uint32_t colorCount = (key.colorCount < 8) ? key.colorCount : 8;
				for (uint32_t i = 0; i < colorCount; ++i) {
					desc.colorAttachments[i].pixelFormat = key.colorFormat[i];
					if (key.colorFormat[i] != MTLPixelFormatInvalid) {
						metal::ApplyBlendMode(desc.colorAttachments[i], static_cast<BlendMode>(key.blendMode));
					}
				}

				// 深度。depthFormat が Invalid なら深度アタッチメント無しのパス(深度テストは
				// MTLDepthStencilState 側で Disabled を選ぶこと。PSO は関知しない)。
				desc.depthAttachmentPixelFormat = key.depthFormat;

				// 頂点レイアウト。**stride だけはキー側の実測値で上書きする**。
				// MetalShader が持つ記述子は .spv のリフレクションで組んだ「パック済み前提」の
				// stride なので、実際に積んだ頂点バッファの stride と食い違うことがある
				// (VulkanPipelineCache が key.vertexStride を優先しているのと同じ扱い)。
				// MetalShader の記述子を直接書き換えると他の PSO へ波及するので copy を使う。
				MTLVertexDescriptor* effectiveDesc = nil;
				if (vertexDesc != nil) {
					effectiveDesc = [vertexDesc copy];
					if (key.vertexStride > 0) {
						effectiveDesc.layouts[metal::VERTEX_BUFFER_INDEX].stride = key.vertexStride;
					}
					if (key.instanceStride > 0) {
						effectiveDesc.layouts[metal::INSTANCE_BUFFER_INDEX].stride = key.instanceStride;
					}
					desc.vertexDescriptor = effectiveDesc;
				}

				NSError* error = nil;
				pipeline = [device_ newRenderPipelineStateWithDescriptor:desc error:&error];

				[effectiveDesc release];
				[desc release];

				if (pipeline == nil) {
					// **ここを出さないと原因が分からない**。Metal は「どの属性が悪いか」まで
					// 書いてくれるので、本文を必ず残す(MetalShader のコンパイル失敗時と同じ親切さ)。
					char msg[512];
					std::snprintf(msg, sizeof(msg),
						"[MetalPipelineCache] PSO の生成に失敗: colorCount=%u depth=%d vbStride=%u instStride=%u",
						static_cast<uint32_t>(key.colorCount), static_cast<int>(key.depthFormat),
						key.vertexStride, key.instanceStride);
					aq::StartupLog(msg);
					if (error != nil) {
						LogMultiLine("[MetalPipelineCache]   ", [[error localizedDescription] UTF8String]);
					}
					EngineAssertMsg(false, "Metal の PSO 生成に失敗しました");
				}
			}

			// 失敗も記憶する。毎 Draw で作り直してログが溢れるのを防ぐため(nil が返り続ける)。
			pipelineMap_[key] = pipeline;
			return pipeline;
		}


		id<MTLComputePipelineState> MetalPipelineCache::GetOrCreateCompute(id<MTLFunction> cs)
		{
			if (cs == nil) {
				aq::StartupLog("[MetalPipelineCache] CS が nil のため compute PSO を生成できません");
				return nil;
			}

			const void* functionKey = static_cast<const void*>(cs);

			ComputeMap::const_iterator it = computeMap_.find(functionKey);
			if (it != computeMap_.end()) { return it->second; }

			if (device_ == nil) {
				aq::StartupLog("[MetalPipelineCache] MTLDevice が無いため compute PSO を生成できません");
				return nil;
			}

			id<MTLComputePipelineState> pipeline = nil;

			@autoreleasepool {
				// 描画側と違い compute は記述子を組む必要がない(Metal の PSO は関数 1 本で決まる)。
				NSError* error = nil;
				pipeline = [device_ newComputePipelineStateWithFunction:cs error:&error];

				if (pipeline == nil) {
					// **ここを出さないと原因が分からない**。どの関数で落ちたかが要るのでシェーダ名も添える
					// (MetalShader のコンパイル失敗時 / 描画 PSO 失敗時と同じ親切さ)。
					char msg[512];
					std::snprintf(msg, sizeof(msg), "[MetalPipelineCache] compute PSO の生成に失敗: %s",
						([cs name] != nil) ? [[cs name] UTF8String] : "(名前不明)");
					aq::StartupLog(msg);
					if (error != nil) {
						LogMultiLine("[MetalPipelineCache]   ", [[error localizedDescription] UTF8String]);
					}
					EngineAssertMsg(false, "Metal の compute PSO 生成に失敗しました");
				}
			}

			// 失敗も記憶する。毎 Dispatch で作り直してログが溢れるのを防ぐため(nil が返り続ける)。
			computeMap_[functionKey] = pipeline;
			return pipeline;
		}


		void MetalPipelineCache::Clear()
		{
			// MTLFunction は MetalShader の持ち物なので触らない。ここで解放するのは
			// newXxxPipelineStateWithXxx: で +1 された PSO だけ。
			for (PipelineMap::value_type& entry : pipelineMap_) {
				[entry.second release];
			}
			pipelineMap_.clear();

			for (ComputeMap::value_type& entry : computeMap_) {
				[entry.second release];
			}
			computeMap_.clear();
		}
	}
}
#endif // ENGINE_GRAPHICS_METAL
