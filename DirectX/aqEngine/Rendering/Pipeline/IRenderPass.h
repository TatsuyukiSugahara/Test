#pragma once
#include <cstdint>
#include <vector>
#include "Rendering/Pipeline/PassResources.h"


namespace aq
{
	namespace rendering
	{
		struct RenderFrame;
		class RenderCommandList;


		/**
		 * パスの実行単位(設計書/レンダーパイプライン設計.md §1.1)
		 */
		enum class PassScope : uint8_t
		{
			Frame,   // フレームに 1 回(影・ポストプロセス・UI)
			View,    // ビューごと(分割画面ではビュー数だけ回る)
		};




		/**
		 * Build() に渡すビュー情報。
		 * Frame scope のパスには先頭ビュー相当(viewIndex=0 / 全画面矩形)が渡る。
		 */
		struct PassViewInfo
		{
			/** ビューの番号と総数 */
			uint32_t viewIndex = 0;
			uint32_t viewCount = 1;

			/** このビューのビューポート(ピクセル) */
			float x = 0.0f;
			float y = 0.0f;
			float w = 0.0f;
			float h = 0.0f;

			/** 全画面の寸法(ピクセル)。ポストプロセス等が RT 全体を扱うときに使う */
			float fullWidth  = 0.0f;
			float fullHeight = 0.0f;


			/** 先頭ビューか(GBuffer のクリアやパーティクルは先頭ビューだけが行う) */
			inline bool IsFirstView()  const { return viewIndex == 0; }
			/** 単一ビューか(Hi-Z やクラスタカリングは単一カメラ前提) */
			inline bool IsSingleView() const { return viewCount == 1; }
		};




		/**
		 * パスが読む / 書く掲示板キーの宣言。PipelineBuilder::Build() の検証に使う。
		 */
		class PassDeclaration
		{
		private:
			std::vector<PassResourceKey> reads_;
			std::vector<PassResourceKey> readsOptional_;
			std::vector<PassResourceKey> writes_;


		public:
			/** 必ず前段で書かれていなければならないキー */
			inline void Reads(const PassResourceKey key)         { reads_.push_back(key); }
			/** 無くても動くキー(例: Sky は Depth が無ければ深度なしで描く) */
			inline void ReadsOptional(const PassResourceKey key) { readsOptional_.push_back(key); }
			/** このパスが書く(登録する)キー */
			inline void Writes(const PassResourceKey key)        { writes_.push_back(key); }


		public:
			inline const std::vector<PassResourceKey>& GetReads()         const { return reads_; }
			inline const std::vector<PassResourceKey>& GetReadsOptional() const { return readsOptional_; }
			inline const std::vector<PassResourceKey>& GetWrites()        const { return writes_; }
		};




		/**
		 * レンダーパスの抽象。エンジンの標準パスもゲーム独自のパスも同じ型で、
		 * PipelineBuilder に Add / Insert してパイプラインを組む。
		 *
		 * 契約(設計書 §1.1):
		 *  1. 読むキーは DeclareResources で宣言する
		 *  2. 入った状態に戻して終わる(BlendMode=Opaque / DepthMode=ReadWrite)。RT のバインドは次のパスが自分で行う
		 *  3. Scene に描くパスは自分で SetRenderTargetWithDepthCommand(Scene, Depth) を積む
		 *     (Depth が無ければ SetRenderTargetCommand(Scene))。前のパスの終わり方に依存しない
		 *  4. Setup で作った RT は自分が所有し、デストラクタで解放する
		 *  5. Build はゲームスレッド、積んだコマンドの Execute はレンダースレッド
		 */
		class IRenderPass
		{
		public:
			virtual ~IRenderPass() = default;

			/** ログと Find<T> の表示に使う名前(静的文字列) */
			virtual const char* GetName() const = 0;

			/** 実行単位。既定はビューごと */
			virtual PassScope GetScope() const { return PassScope::View; }

			/** 機能要件。false のパスは Build() が除外してログに出す(例: Bloom は compute 必須) */
			virtual bool IsSupported() const { return true; }

			/** 読む / 書くキーの宣言 */
			virtual void DeclareResources(PassDeclaration& decl) const = 0;

			/**
			 * RT の生成と掲示板への登録。列の順に 1 回だけ呼ばれる。
			 * 失敗したら false(パイプライン全体の Build() が失敗する)
			 */
			virtual bool Setup(PassResources& res, const uint32_t width, const uint32_t height)
			{
				(void)res; (void)width; (void)height;
				return true;
			}

			/**
			 * コマンドの記録(ゲームスレッド)。
			 * frame は非 const(クラスタカリングがアイテムの useGpuCull を確定させるため)。
			 * Frame scope のパスには frames[0] と全画面の view が渡る。
			 */
			virtual void Build(RenderFrame& frame, const PassViewInfo& view,
			                   PassResources& res, RenderCommandList& outList) = 0;
		};
	}
}
