# サウンドエンジン 設計ドキュメント

対象: `aqEngine/Sound/`
前提: 既存の抽象レイヤ（Bridge パターン）の流儀に合わせ、サウンドを `ISoundBackend` / `ISoundVoice` の実装として追加する。Windows は XAudio2、Android は Oboe を **自前バックエンド**として実装する。
本書は **設計のみ**。実装コードは含まない（シグネチャ例は方針の明確化用）。
姉妹文書: [VulkanBackend設計.md](VulkanBackend設計.md)（同一構造の先行事例。Bridge + シングルトン + `Create<TImpl>()` をサウンドへ平行適用する）。

---

## 0. 設計の指針

- **抽象レイヤ（`ISoundBackend` / `ISoundVoice`）は API 非依存に保つ。** Game 側・ECS 側のコードは XAudio2 か Oboe かを一切知らない。バックエンド差し替えで呼び出し側は書き換えゼロをゴールにする（Vulkan 移行で抽象IF追加 0 本を達成した実績と同じ思想）。
- **3D 音響（距離減衰・パン・ドップラー）はコア側（`aq::sound`）で計算する。** X3DAudio は Windows 専用で Android に等価物が無いため、3D ロジックを 1 か所に集約し、バックエンドには「ボイス再生 + per-channel ゲイン + 周波数比」だけを渡す。プラットフォーム差はボイス出力だけに閉じる。既存 `Math/Vector` を流用する。
- **ミキシングの置き場所はバックエンドごとに異なる。**
  - XAudio2: ソースボイス → サブミックスボイス（Bus）→ マスタリングボイス。**ミックスは XAudio2 が行う。**
  - Oboe: 出力ストリームは 1 本。**自前のソフトウェアミキサ**でアクティブボイスをコールバック内で合算する。
  - この差は各バックエンド内部に閉じ込め、`ISoundVoice` の意味論（Submit/Start/Pause/Resume/Stop/SetOutputMatrix/SetFrequencyRatio、§3.1〜§3.4）で揃える。
- **ストリーミング（プッシュ型）ボイスを一級市民にする。** BGM ストリーム再生と、将来の**動画の音声トラック流し込み**を同じ仕組みで扱う（§11）。デバイス出力クロック（§3.4）と stream の media クロック（§3.5）を分けて露出し、A/V 同期の master clock にできるようにする。
- まず「2D ワンショット再生が鳴る」→「Bus/ストリーミング」→「3D」→「ECS 統合」→「Oboe 実機」と段階的に積む（§12 フェーズ計画）。

---

## 1. アーキテクチャ全体像

```
Game / ECS 側
   │  auto clip = ResourceManager::Load<SoundClip>(...);  // 事前ロード（§5.2）
   │  SoundEngine::Play(clip) / source.SetPosition(worldPos)
   ▼
SoundEngine (aq::sound, コア, プラットフォーム非依存, シングルトン)
   ├─ SoundListener        3D リスナー（= カメラ。位置/前方/上/速度）
   ├─ SoundSource          3D 発音体（位置/減衰/ドップラー/ループ/再生制御）
   ├─ SoundClip            デコード済み PCM リソース（SE は全展開）
   ├─ SoundStream          ストリーミング元（BGM / 動画音声トラック）
   ├─ SoundHandle          再生中インスタンスの軽量ハンドル
   ├─ SoundBus             Master / BGM / SE / Voice のボリュームグループ
   └─ Mixer3D              3D パラメータ → per-voice ゲイン/周波数比 を計算
   │
   ▼  impl_ : ISoundBackend   (Bridge — IGraphicsDeviceImpl と同じ立ち位置)
   ├─ XAudio2SoundBackend  (Windows)  ← XAudio2 デバイス / マスタリング / サブミックス
   │     └─ XAudio2SoundVoice : ISoundVoice  ← IXAudio2SourceVoice をラップ
   └─ OboeSoundBackend     (Android)  ← AAudio/OpenSL ストリーム + SoftwareMixer
         └─ OboeSoundVoice  : ISoundVoice  ← ミキサ内の論理ボイス
```

各バックエンドが所有する内部サブシステム（D3D12/Vulkan の対応物と同じ思想で 1:1 に並べる）。

| コア概念 | XAudio2 の対応物 | Oboe の対応物 | 役割 |
|---|---|---|---|
| `ISoundVoice` | `IXAudio2SourceVoice` | ミキサ内論理ボイス | 1 音源インスタンスの再生・ゲイン・ピッチ |
| `SoundBus` | `IXAudio2SubmixVoice` | ミキサ内バスゲイン | グループボリューム |
| Master | `IXAudio2MasteringVoice` | `oboe::AudioStream` | 最終出力 |
| ミキシング | XAudio2 内部 | `SoftwareMixer`（自前） | ボイス合算・リサンプル |
| 再生クロック | `GetState().SamplesPlayed` を出力レートへ換算 | ミキサ output frame カウンタ | A/V 同期 master clock（§3.4） |

---

## 2. クラス構成（コア `aq::sound`）

### 2.1 SoundEngine（シングルトン・Bridge）

GraphicsDevice と同じ `Create<TImpl>()` テンプレート・シングルトンにする。

```cpp
namespace aq
{
	namespace sound
	{
		class SoundEngine
		{
		public:
			// バックエンドを与えて生成（aq::Create() のタイミングで呼ぶ）
			template <typename TImpl, typename... TArgs>
			static void Create(TArgs&&... args);
			static SoundEngine& Get();
			static void Release();

			bool Initialize();
			void Finalize();

			// 毎フレーム: 3D 更新・ストリーム供給・ボイス回収
			void Update(float deltaTime);

			// ワンショット再生（2D）。RefSoundClip(= shared_ptr<const SoundClip>) を渡す。
			// ボイスが再生終了まで clip を保持するため、呼び出し側の解放/ResourceManager の
			// unload 順に依存せず PCM 生存が保証される（§3.3a / High）。
			SoundHandle Play(RefSoundClip clip, SoundBusId bus = SoundBusId::SE);

			// 3D 発音体の生成（位置を持つ）。エンジンがプール所有し、世代付きハンドルを返す（§2.4）
			SoundSourceHandle CreateSource(RefSoundClip clip, SoundBusId bus = SoundBusId::SE);
			SoundSource*      Resolve(SoundSourceHandle handle);   // 無効化済みなら nullptr
			void              DestroySource(SoundSourceHandle handle);

			// ストリーミング元を生成（BGM/動画）。キャッシュ非共有の実体（§5.1/§5.2）
			std::unique_ptr<SoundStream> OpenStream(const char* path, SoundBusId bus = SoundBusId::BGM);

			SoundListener& GetListener();
			void           SetBusVolume(SoundBusId bus, float volume);
			void           SetMasterVolume(float volume);

		private:
			std::unique_ptr<ISoundBackend> impl_;
			// SoundListener listener_; SoundBus buses_[...]; SoundSource プール（世代管理）;
			// アクティブボイス回収 等
		};
	}
}
```

### 2.2 SoundSource（3D 発音体）
- `SetPosition / SetVelocity`、減衰モデル（`AttenuationModel`）、`minDistance / maxDistance`、`SetPitch`。
- clip は `CreateSource(clip)` 時に保持済み（`RefSoundClip`, §2.4）。再生制御は **3 状態を分離**する（§3.3b の `ISoundVoice` 契約に対応）:
  - `Play(LoopRegion loop = {})` … 先頭から再生開始。ループはここで確定（`SubmitClipRegion(..., loop)` 経由, §3.3c）。
  - `Pause() / Resume()` … 再生位置を保持して停止/再開（キューは破棄しない）。
  - `Stop()` … 停止し、投入済みキューを破棄して先頭へ巻き戻す（flush）。
- ループは**再生開始時に確定**する。再生中の変更 `SetLoopRegion()` は内部で `Stop`→再 submit を伴う（XAudio2 の loop は buffer 提出時固定のため, Medium #6）。ストリームのループは `SoundStream` の decoder seek で行い、ボイスは関与しない。
- **3D 位置を持つソースは mono 入力に限定**する（§4）。ステレオ素材は 2D 扱い（BGM/UI）とする。
- 内部に `ISoundVoice` を 1 本持つ。3D パラメータは `Mixer3D` がリスナーと突き合わせて出力行列/周波数比へ変換し、`ISoundVoice` へ反映する。

### 2.3 SoundListener（3D リスナー）
- 位置・前方ベクトル・上ベクトル・速度。通常はカメラに追従（§9 の `AudioListenerComponent`）。

### 2.4 ハンドルと共有参照（`SoundHandle` / `SoundSourceHandle` / `RefSoundClip`）
- `SoundHandle` / `SoundSourceHandle` … **世代付きインデックス**。実体（ボイス/ソース）はエンジンがプール所有し、再利用される。
  - `SoundHandle` … `Play()` の戻り値。再生中インスタンスを指す。`IsPlaying() / Stop() / SetVolume()`。再生終了後にアクセスしても世代不一致で安全に no-op。
  - `SoundSourceHandle` … `CreateSource()` の戻り値。3D ソースを指す。**ECS コンポーネントは生ポインタでなくこのハンドルを保持**し（Medium #6）、`SoundEngine::Resolve()` で都度解決する。コンポーネント破棄/シーン破棄時は `DestroySource()` を呼ぶ契約。回収済みハンドルの `Resolve` は nullptr。
- `RefSoundClip` … `using RefSoundClip = std::shared_ptr<const SoundClip>;`（既存 `RefMeshResource` 等と同じ流儀）。**clip は共有参照で寿命管理**し、ハンドル（世代）方式は使わない。`Play`/`CreateSource` に渡すとボイスが再生終了まで `shared_ptr` を保持し、`ResourceManager` の unload・`Finalize` 順・手動解放に依存せず PCM 生存を保証する（High）。`ResourceManager::Load<SoundClip>()` の戻り値 `shared_ptr<SoundClip>` をそのまま渡せる。

---

## 3. バックエンド抽象（Bridge implementor）

### 3.1 ISoundVoice
1 音源インスタンスの最小 IF。XAudio2 のソースボイスモデルをそのまま採用し、Oboe 側はミキサ内論理ボイスで同義を満たす。各メソッドの**実時間契約は §3.3〜§3.5 で確定**する（メモリ所有権・寿命・状態遷移・スレッド・クロック）。

```cpp
// 投入結果（背圧制御。Medium #4）
enum class SubmitResult
{
	Accepted,       // 受理（キューに積んだ）
	WouldBlock,     // 内部リング/ブロック満杯。呼び出し側は後で再投入
	InvalidFormat,  // format 不一致
	Closed,         // 既に endOfStream 済み/停止中で受け付けない
};

class ISoundVoice
{
public:
	virtual ~ISoundVoice() = default;

	virtual bool Initialize(const SoundFormat& format) = 0;

	// PCM 供給（ストリーミング）。endOfStream で末尾通知。バックエンドが内部コピーするので
	// 呼び出し側は即解放してよい（§3.3a）。満杯時は WouldBlock を返す（§3.3 背圧）。
	virtual SubmitResult SubmitBuffer(const void* data, uint32_t byteSize, bool endOfStream) = 0;

	// 常駐クリップのゼロコピー再生。RefSoundClip を保持して clip 生存を保証する（§3.3a / High）。
	// ループは submit 時に確定（XAudio2 は buffer 提出時固定のため。Medium #6）。
	virtual SubmitResult SubmitClipRegion(RefSoundClip clip, uint64_t startFrame, uint64_t frameCount,
	                                      const LoopRegion& loop, bool endOfStream) = 0;

	// 再生状態（§3.3b の状態機械）
	virtual void Start()  = 0;   // 停止/一時停止 → 再生
	virtual void Pause()  = 0;   // 位置保持で停止（キュー維持）
	virtual void Resume() = 0;   // Pause からの再開
	virtual void Stop()   = 0;   // 停止し投入キューを破棄して巻き戻す（flush）

	// per-instance 全体音量（1.0 = 等倍）。3D 出力行列・バスゲインとは別。
	virtual void SetVolume(float volume) = 0;

	// ピッチ / ドップラー（1.0 = 等倍）。リサンプル比に反映される
	virtual void SetFrequencyRatio(float ratio) = 0;

	// 出力ルーティング行列（入力ch × 出力ch, row-major）。3D パン結果を Mixer3D が渡す。
	// 3D 音源は src=1（mono）で 1×dst 行列。2D ステレオ素材は src=2 の行列を渡す（§4）。
	virtual void SetOutputMatrix(uint32_t srcChannels, uint32_t dstChannels, const float* matrix) = 0;

	// このボイスが実際に再生し終えた「入力フレーム数」。media clock の anchor（§3.5）。
	// ＝ XAudio2 source voice の SamplesPlayed / Oboe ミキサのボイス別消費カウンタ。
	// SetFrequencyRatio で出力フレームとはズレるため、入力フレーム基準で返す。
	virtual uint64_t GetConsumedFrames() const = 0;

	// ストリーミングの背圧制御用。未消費の投入バッファ数（残量）
	virtual uint32_t GetQueuedBufferCount() const = 0;
};
```

> ループとクロックを **submit 時確定 / voice は入力消費フレームのみ露出** に寄せたのがポイント。デバイス全体の出力クロックは voice ではなく `ISoundBackend`（§3.4）に置き、media time への写像は `SoundStream`（§3.5）が担う。

### 3.2 ISoundBackend
デバイス初期化とボイス工場。`GraphicsDevice`/`IGraphicsDeviceImpl` の関係をそのまま縮小したもの。

```cpp
class ISoundBackend
{
public:
	virtual ~ISoundBackend() = default;

	virtual bool Initialize() = 0;
	virtual void Finalize()   = 0;

	// ボイス工場。呼び出し元は実装型を知らない
	virtual std::unique_ptr<ISoundVoice> CreateVoice(const SoundFormat& format, SoundBusId bus) = 0;

	virtual void SetBusVolume(SoundBusId bus, float volume) = 0;
	virtual void SetMasterVolume(float volume) = 0;

	// デバイス出力クロック（§3.4）。送出フレーム総数・出力レート・推定遅延・underrun。
	// A/V 同期はこれ（latency anchor）と voice の GetConsumedFrames を合わせて算出（§3.5）。
	virtual SoundClock GetOutputClock() const = 0;

	// バックエンドのポンプ（Oboe は基本コールバック駆動なので空実装でよい）
	virtual void Update() {}
};
```

### 3.3 リアルタイム契約（所有権・寿命・状態）

実装で最も事故りやすい部分を、抽象 IF の不変条件として固定する。

#### (a) PCM メモリ所有権（High）
- **`SubmitBuffer` は内部コピーが既定。** バックエンドは投入 PCM を自身が管理するリング/プールへコピーし、呼び出し側（デコーダの一時バッファ）は呼び出し直後に解放・再利用してよい。
  - XAudio2 の `SubmitSourceBuffer` は **データをコピーしない**ため、`XAudio2SoundVoice` は per-voice の確保済みブロックへコピーしてから `pAudioData` に渡し、`OnBufferEnd` でそのブロックを回収する（呼び出し側バッファは参照しない）。
  - Oboe 側はそもそもソフトウェアミキサのリングへコピーする。
  - 内部リング満杯時は `SubmitResult::WouldBlock` を返す（コピーしない）。呼び出し側＝`SoundStream` は次の `Update` で再投入する（背圧）。
- **常駐クリップのゼロコピーは `SubmitClipRegion` で、生存は `RefSoundClip` で保証する。** ボイスは渡された `shared_ptr<const SoundClip>` を**再生終了/回収まで保持**する。これにより `const&` では表現できなかった生存保証を API 上で表現し、`ResourceManager` の unload・`Finalize` 順・手動解放に依存しない（High）。`OnBufferEnd`/Draining 完了時に `shared_ptr` を release。
- ストリーミング（BGM/動画）は常に `SubmitBuffer`（コピー）を使う。コピー帯域はオーディオでは僅少で問題にならない。

#### (b) 再生状態の状態機械
ボイスは次の状態を持ち、メソッドの意味を一意にする。

```
Idle ──Start──▶ Playing ──Pause──▶ Paused ──Resume──▶ Playing
  ▲                │  ▲                                   │
  └──── Stop ──────┘  └──────────── Stop ─────────────────┘
                 （Stop = 停止 + 投入キュー破棄 + 先頭へ巻き戻し）
```
- `Pause/Resume` は再生位置とキューを保持（一時停止）。`Stop` は flush（キュー破棄）。両者を**明確に分離**する。
- ストリーミングで `endOfStream` 投入後にキューが尽きると Idle（自然終了）へ遷移し、`SoundHandle` は以後 no-op。

#### (c) ループ区間（submit 時確定 / Medium #6）
```cpp
struct LoopRegion
{
	uint64_t startFrame  = 0;   // ループ先頭（フレーム）
	uint64_t frameCount  = 0;   // 0 = ループ無効
	uint32_t loopCount   = 0;   // 0 = 無限
};
```
- **ループは `SubmitClipRegion(..., loop, ...)` の引数で渡し、submit 時に確定する。** 独立した `SetLoopRegion` は持たない（XAudio2 の `LoopBegin/LoopLength/LoopCount` は `XAUDIO2_BUFFER` 提出時に固定で、提出後の変更は既存 buffer に反映されないため。Medium #6）。
- 常駐クリップ: バックエンドが region を `XAUDIO2_BUFFER` のループ指定で反映。
- **再生中にループを変えるには `Stop`（flush）→ 新しい `LoopRegion` で再 submit** する。`SoundSource::SetLoopRegion` はこの「停止→再 submit」をラップした高レベル便宜 API として提供する。
- ストリーム: ループは voice ではなく `SoundStream` が担当。末尾到達時に **decoder を `startFrame` へ seek** して供給継続する。バックエンドはループを知らなくてよい。

#### (d) Oboe コールバック用ボイス寿命モデル（High）
`dataCallback`（オーディオスレッド）が触れるボイスを、ゲーム/サウンドスレッドが**破棄しない**ことを構造で保証する。

- **固定サイズの voice プール**（`OboeSoundVoice[N]`）をミキサが所有。ベクタ再確保・delete を実行時にしない。
- サウンドスレッド → オーディオスレッドの操作は **SPSC コマンドキュー**（`Start/Stop/SetMatrix/SetFreq/Submit/Free`）で渡し、`dataCallback` 冒頭で drain して適用する。コールバック内でロック・確保・IO をしない。
- 各スロットは状態 `Free/Acquired/Playing/Draining` を持つ。**回収はオーディオスレッド側**で `Draining` 完了後に `Free` へ戻し、空き通知をオーディオ→サウンドのキュー（または atomic 状態）で返す。サウンドスレッドは `Free` スロットだけを再取得する。
- これにより「callback 実行中に voice が消える/動く」状況が起きない（epoch/世代は `SoundHandle` 側で別途検証）。
- XAudio2 backend は SourceVoice 自体がスレッドセーフなため、この機構は **Oboe backend 内部に閉じる**（抽象 IF は不変）。

#### (e) スレッド集約ポリシー（Medium #5）
- **`ISoundBackend` / `ISoundVoice` への呼び出しは「サウンドスレッド」単一に集約する**（＝ Oboe コマンドキューは SPSC でよい）。`SubmitBuffer`/`SubmitClipRegion`/状態遷移/行列更新はすべてこのスレッドから行う。
- **デコーダ/供給スレッド（BGM デコード・動画 `VideoPlayer` の音声デコード）は voice に直接触れない。** 自前のロックフリーリングへ PCM を書くだけで、`SoundEngine::Update()`（サウンドスレッド）がそのリングを drain して `SubmitBuffer` する。
  - つまり「decoder thread → `SoundStream` のリング → （サウンドスレッドが）voice」の一方向。MPSC は不要。
  - この一段クッションにより、デコードの遅延・スパイクがオーディオ/ゲームスレッドへ波及しない。
- どうしても複数プロデューサから直接 push したい将来要件が出たら、その時に該当 voice のリングだけ MPSC 化する（抽象 IF は不変）。現設計は **SPSC 前提**を明示する。

### 3.4 デバイス出力クロック契約（A/V 同期の latency anchor）

`ISoundBackend::GetOutputClock()` が返す **デバイス全体**のクロック。源泉は「出力デバイスへ送出されたフレーム数」と「推定遅延」。voice 個別の media time ではない（それは §3.5）。

```cpp
struct SoundClock
{
	uint64_t outputFrames;     // 出力レートで送出済みのフレーム総数（デバイス基準）
	uint32_t sampleRate;       // 出力サンプルレート
	double   latencySeconds;   // 推定デバイス出力遅延（送出済み≠発音済みの差）
	uint64_t underrunCount;    // 累積アンダーラン回数（ズレ検出用）
};
```
- **XAudio2**: mastering voice の `GetState().SamplesPlayed` は **device output frame counter として信頼できない**（High）。ここでは `latencySeconds` の推定（`GetPerformanceData()` の遅延・バッファ周期）と `underrunCount`（`GlitchesSinceEngineStarted`）を主に提供し、`outputFrames` は best-effort（送出済みの目安）に留める。**A/V 同期の権威ある時刻は §3.5 の stream 側 media clock** とし、本クロックは latency 補正用 anchor として使う。
- **Oboe**: ミキサの出力フレームカウンタ ＋ `AudioStream::getTimestamp()`（提示時刻）で `latencySeconds` を埋める。`underrunCount` は `getXRunCount()`。outputFrames はミキサが厳密に持てる。

### 3.5 ストリーム media クロック契約（A/V 同期の権威）

動画同期に必要なのは「いま聞こえている音声がメディアの何秒目か」。これは voice/device ではなく **stream 固有**なので `SoundStream` が提供する。

```cpp
struct MediaClock
{
	double presentedMediaSeconds;  // 現在発音中の音声の media 時刻（PTS 基準）
	bool   valid;                  // 供給途切れ/未開始なら false
};
// SoundStream::GetPlaybackClock() -> MediaClock
```
- 算出: `presentedMediaSeconds ≒ baseMediaPTS + voice.GetConsumedFrames() / inputSampleRate - backend.GetOutputClock().latencySeconds`。
  - `baseMediaPTS` … この stream に最初に流した PCM の media PTS（動画なら音声トラックの開始 PTS）。
  - `GetConsumedFrames()` … その voice が実際に鳴らし終えた**入力フレーム数**（§3.1）。ピッチ非使用の動画音声では入力＝出力レート対応で素直。
  - `latencySeconds` … §3.4 の anchor で「送出済み」と「実際に発音済み」の差を補正。
- **動画プレイヤ（§11）はこの `MediaClock` を master clock** にして映像フレーム提示をスケジュールする。`SoundClock::underrunCount` 増加や `valid==false` 時は再同期する。

---

## 4. 3D 音響モデル（コアで計算 = `Mixer3D`）

X3DAudio 等のプラットフォーム API には依存せず、コアで以下を計算する。プラットフォーム間で**音の聞こえ方を一致**させるのが狙い。

1. **距離減衰**: リスナー↔ソース距離 `d` と `minDistance / maxDistance`、`AttenuationModel`（Linear / Inverse / Exponential）から距離ゲインを算出。
2. **パンニング**: ソース方向をリスナー座標系へ変換し、**1×dst の出力行列**（mono 入力 → 各出力チャンネルのゲイン）を構成して `SetOutputMatrix(1, dstChannels, matrix)` で渡す。出力は当面ステレオ（dst=2）、5.1 は将来。
3. **ドップラー**: リスナー速度・ソース速度・音速から周波数比を算出し `SetFrequencyRatio` で渡す。
4. **バス/マスター**との合成ゲインを行列に掛け込む。

**入力チャンネルの規約（High #3）**: 3D 位置を持つ音源は **mono 入力に限定**する（パンを行列で与えるため）。ステレオ/マルチチャンネル素材（BGM・UI・環境音）は 2D 扱いとし、`SetOutputMatrix(srcChannels, dstChannels, ...)` に恒等または明示の行列を渡して直接ルーティングする。`Mixer3D` はソースが mono であることを前提化（生成時に検証）。

`SoundFormat`・`AttenuationModel`・`LoopRegion`・`SoundClock` 等は `SoundTypes.h` に定義する。座標・ベクトルは既存 `aqEngine/Math/Vector.h` を流用する。

---

## 5. リソースとデコード

| 種別 | クラス | 方針 |
|---|---|---|
| 短音（SE） | `SoundClip` | 起動時/ロード時に全 PCM をデコードして常駐。複数ソースで共有・不変 |
| 長音（BGM） | `SoundStream` | ファイルから逐次デコードし、`Update` でリングへ供給。常駐させない |
| 動画音声 | `SoundStream`（プッシュ） | デコーダ（§11）が `SubmitBuffer` で push。エンジンはデコードしない |

- デコーダは `Sound/Decoder/` に置く。まず **WAV（`WavDecoder`）**、次に **Ogg Vorbis（stb_vorbis をベンダリング）** を想定。フォーマット追加は `ISoundDecoder` 越しに行い、コアは中身を知らない。
- リソースは既存の Resource 管理（`aqEngine/Resource/`）／`Memory` アロケータと整合させる。デコード済み PCM は専用アロケータ上に確保する方針（後で検討）。

### 5.1 責務分担（誰が `SoundClip` を作るか）

「ファイル読み込み」「デコード」「再生」を明確に分離する。**`SoundEngine` は自分でファイルを読まない**。

| レイヤ | 責務 |
|---|---|
| `ResourceManager` + `SoundClipLoader`（`WavLoader`/`OggLoader`） | ファイル IO、decoder 選択、PCM デコード、`SoundClip` 生成・**キャッシュ**（既存 `Load<T>()` の非同期＋バンク機構に乗る） |
| `SoundClip` | デコード済み PCM ＋ `SoundFormat` ＋ 既定 `LoopRegion` を保持。**不変・複数ソース共有**。`res::ResourceBase` 派生で非同期ロード/世代キャッシュに乗る |
| `SoundStream` | decoder ＋ ring buffer ＋ streaming 状態を保持。**インスタンス毎の状態を持つためキャッシュには載せない**。`SoundEngine::OpenStream()` が生成 |
| `SoundEngine` | 再生インスタンス管理（`SoundHandle`/`SoundSource`）、3D、bus、backend voice 生成。**素材は受け取るだけ** |
| `ISoundBackend` | XAudio2 / Oboe への出力だけ |

- `SoundClip` は既存の `res::ResourceBase` を継承し、`WavLoader : res::ResourceLoaderBase` を `ResourceManager::Reflection<SoundClip, WavLoader>()` で登録する（メッシュ/テクスチャと同じ流儀）。これで `Load<SoundClip>()` の非同期ロード・パス正規化・重複排除・`IsCompleted()` がそのまま使える。

### 5.2 ロード/再生フローの明文化（事前ロード統一）

**曖昧さ排除のため「事前ロード済みリソースを渡す」モデルに統一する。** ワンショット API は文字列パスを受け取らない（再生時 IO/デコードでスパイクやブロックを招くため）。

SE（短音）:
```cpp
// ロード（非同期。完了は IsCompleted() で確認、または事前にまとめてロード）
RefSoundClip clip = ResourceManager::Get().Load<SoundClip>("Assets/Sound/explosion.wav");
// 再生（shared_ptr をそのまま渡す。ボイスが再生終了まで保持＝生存保証。3D なら CreateSource）
SoundEngine::Get().Play(clip, SoundBusId::SE);
```
- `Play(RefSoundClip)` のみを提供し、`Play(const char*)` は**提供しない**。どうしても即時ロード再生が要る場面は、呼び出し側で「Load → 完了待ち or プリロード」を明示する。
- `clip` が未完了（`IsLoading()`）のまま `Play` した場合は no-op＋警告（黙って無音にしない）。プリロード運用を推奨。

BGM / 動画（ストリーム）:
```cpp
auto bgm = SoundEngine::Get().OpenStream("Assets/Sound/bgm.ogg");  // 新規ストリーム生成
bgm->Play(LoopRegion{ /*全体ループ*/ });   // ループは再生開始時に確定（§3.3c）
```
- `SoundStream` はキャッシュ共有しない実体。`OpenStream` が decoder を選択して生成し、`SoundEngine::Update()` がリング供給を回す（§3.3a のコピー投入）。
- 動画音声は §11 の `VideoPlayer` が `OpenStream`（プッシュ型）相当を取得し、自前デコード PCM を `SubmitBuffer` で流す。

---

## 6. バス / ミキシング

- 論理バス `SoundBusId { Master, BGM, SE, Voice }` を用意し、グループ単位の音量・ミュートを制御する。
- XAudio2: 各バス = `IXAudio2SubmixVoice`。ソースボイスを対応サブミックスへルーティング、サブミックス→マスタリング。
- Oboe: `SoftwareMixer` が `bus → gain` テーブルを持ち、ボイス合算時に乗算。
- 将来のエフェクト（リバーブ等）はバス単位で挿す前提で口だけ用意（実装は後）。

---

## 7. XAudio2 バックエンド（Windows）

- `XAudio2SoundBackend::Initialize`: `XAudio2Create` → `CreateMasteringVoice` → バスぶんの `CreateSubmixVoice`。
- `CreateVoice`: `CreateSourceVoice`（`SoundFormat` → `WAVEFORMATEX`）。指定バスのサブミックスへ `XAUDIO2_SEND_DESCRIPTOR` で送る。
- `XAudio2SoundVoice`:
  - `SubmitBuffer` → per-voice 確保済みブロックへ**コピー**してから `SubmitSourceBuffer`（`XAUDIO2_BUFFER`）。空きブロックが無ければ `WouldBlock` を返す。`endOfStream` で `Flags = XAUDIO2_END_OF_STREAM`。`OnBufferEnd`（`IXAudio2VoiceCallback`）でブロック回収（§3.3a）。
  - `SubmitClipRegion` → 常駐クリップ PCM を**ゼロコピー**で `pAudioData` 指定し、引数の `RefSoundClip` を**ボイスが保持**（`OnBufferEnd` で release, §3.3a）。`loop` を `XAUDIO2_BUFFER` の `LoopBegin/LoopLength/LoopCount` に設定（§3.3c）。
  - `Pause` → `Stop(0)`（位置保持）/ `Resume` → `Start(0)` / `Stop` → `Stop(0)` + `FlushSourceBuffers`（§3.3b）。
  - `SetFrequencyRatio` → `IXAudio2SourceVoice::SetFrequencyRatio`。
  - `SetOutputMatrix` → `IXAudio2SourceVoice::SetOutputMatrix`（src×dst をそのまま渡す, §3.1）。
  - `GetConsumedFrames` → この source voice の `GetState().SamplesPlayed`（入力フレーム基準。media clock の anchor, §3.5）。
  - `GetQueuedBufferCount` → `GetState().BuffersQueued`。
- `XAudio2SoundBackend::GetOutputClock`（§3.4）→ **mastering voice の `SamplesPlayed` は device clock として使わない**（High）。`GetPerformanceData()` から遅延/グリッチを取り `latencySeconds`・`underrunCount` を埋め、`outputFrames` は best-effort。権威ある A/V 時刻は §3.5 の stream 側で `GetConsumedFrames` から算出する。
- COM 初期化・デバイス喪失（`OnCriticalError`）時の再初期化を考慮。

---

## 8. Oboe バックエンド（Android）

- `OboeSoundBackend::Initialize`: `oboe::AudioStreamBuilder` で出力ストリーム1本（PerformanceMode::LowLatency、Float、デバイス推奨サンプルレート/バッファ）。AAudio 優先、フォールバック OpenSL ES（Oboe が吸収）。
- 出力は1本なので **`SoftwareMixer` を自前実装**：
  - **固定サイズ voice プール** + **SPSC コマンドキュー**でサウンド/オーディオスレッド間の寿命を管理（§3.3d/e）。`dataCallback` 冒頭でコマンドを drain してから合算する。コマンド投入はサウンドスレッド単一（§3.3e）。
  - `dataCallback` 内で `Playing` な `OboeSoundVoice` を走査し、各ボイスの PCM を **リサンプル**（クリップのサンプルレート → デバイスレート）して出力行列・バスゲインを掛けて加算。
  - `SetFrequencyRatio` はリサンプル比に反映（ドップラー/ピッチ）。`SetOutputMatrix` は合算時の係数。
  - 投入バッファはロックフリーのリング（サウンドスレッド → オーディオスレッド, SPSC）で扱う。満杯時 `SubmitBuffer` は `WouldBlock`。回収はオーディオスレッド側で `Draining→Free`（§3.3d）。
  - `OboeSoundVoice::GetConsumedFrames` はミキサのボイス別消費フレームカウンタ（§3.5 anchor）。
  - `OboeSoundBackend::GetOutputClock`（§3.4）はミキサの出力フレームカウンタ + `AudioStream::getTimestamp()`／`getXRunCount()`。
- ライフサイクル（`onErrorAfterClose` での再構築、アプリのバックグラウンド遷移での pause）を考慮。
- ビルド: Android 側は別ターゲット（NDK）。Oboe は AAR/ソース取り込み。**Windows ビルドには一切リンクしない**（`SoundBackend.h` の `#define` で分岐）。

---

## 9. ECS 統合

既存の Component/System（`TransformComponentSystem` 等）に乗せる。3D 音は自然にコンポーネント化できる。

- `AudioSourceComponent` … `SoundClip`（または `SoundStream`）参照 + 3D パラメータ（減衰/ループ/バス/自動再生）。**実体は生ポインタでなく `SoundSourceHandle` を保持**（§2.4, Medium #6）。コンポーネント破棄/シーン破棄時に `SoundEngine::DestroySource(handle)` を呼ぶのが所有契約。
- `AudioListenerComponent` … カメラエンティティに付与。Transform をリスナーへ反映。
- `SoundSystem` … 毎フレーム、各 `AudioSourceComponent` の handle を `Resolve()` し（無効なら skip）、Transform を `SoundSource` 位置へ反映、`AudioListenerComponent` を `SoundListener` に反映、最後に `SoundEngine::Update(dt)` を駆動。
- ImGui デバッグ（既存 `ImGuiFieldVisitor` / DebugUI）で再生中ボイス・バス音量・3D パラメータを可視化できるよう口を用意。

---

## 10. Engine 配線とバックエンド選択

- **バックエンド選択は `Sound/SoundBackend.h` の `#define`** で行う（`PhysicsBackend.h` と同じ流儀）。

```cpp
// SoundBackend.h（イメージ）
#if defined(_WIN32)
#  define SOUND_BACKEND_XAUDIO2
#elif defined(__ANDROID__)
#  define SOUND_BACKEND_OBOE
#endif

#if defined(SOUND_BACKEND_XAUDIO2)
#  include "XAudio2/XAudio2SoundBackend.h"
namespace aq { namespace sound { using DefaultSoundBackend = XAudio2SoundBackend; } }
#elif defined(SOUND_BACKEND_OBOE)
#  include "Oboe/OboeSoundBackend.h"
namespace aq { namespace sound { using DefaultSoundBackend = OboeSoundBackend; } }
#else
#  error "サウンドバックエンドが未選択です。SoundBackend.h を確認してください。"
#endif
```

- `aq::Create()`（または `Engine::Initialize`）で `SoundEngine::Create<DefaultSoundBackend>()` → `Initialize()`。
- `Engine::Update()` 内で `SoundEngine::Get().Update(deltaTime)`（既存 `gameTimer_.GetDeltaTime()` を渡す）。
- `Engine::Finalize()` で `SoundEngine::Get().Finalize()` → `Release()`。
- `Engine.vcxproj` / `.filters` に `Sound/` 配下を追加。XAudio2 はランタイム同梱（`xaudio2_9`）。

---

## 11. 動画連携（将来）

動画は独立サブシステム `VideoPlayer`（音声エンジンの責務外）。音声側は **プッシュ型ストリーム + stream media clock**（§3.5）を提供するだけで後付けできる。

```
VideoPlayer
  ├─ Demuxer       コンテナ分離(mp4/webm)   … FFmpeg(デスクトップ) / MediaExtractor(Android)
  ├─ VideoDecoder  H.264/VP9 → GPUテクスチャ … HW デコード優先
  ├─ AudioDecoder  AAC/Opus → PCM
  └─ A/V Sync      stream media clock を master として映像フレーム提示を同期
        │ 音声PCMを SubmitBuffer で push（サウンドスレッド経由, §3.3e）
        ▼
     SoundEngine の SoundStream（プッシュ型）→ GetPlaybackClock() を A/V 同期に使う
```

- 音声エンジン側に必要なのは §3 の `SubmitBuffer`（プッシュ）と `SoundStream::GetPlaybackClock()`（§3.5 media clock）だけ。**最初からこの2点を備えるため後付けゼロ**。
- スレッドは §3.3e に従う: 動画の音声デコーダは `SoundStream` のリングへ書くだけで、`SubmitBuffer` はサウンドスレッドが行う。
- 動画デコード本体はバックエンド選択に依存しない別実装（FFmpeg / MediaCodec）。本設計の音声バックエンド選定とは独立。
- 音声ミドルウェア（FMOD/Wwise）も動画は持たないため、動画 = 別サブシステムはどのルートでも共通。

---

## 12. フェーズ計画

1. **基盤 + 2D ワンショット**: `SoundTypes` / `ISoundBackend` / `ISoundVoice` / `SoundEngine` / `SoundClip` / `SoundLoader`(§5.1) / `WavDecoder` / `XAudio2*`。WAV を `Play()` で 1 発鳴らす。
2. **バス + ストリーミング**: `SoundBus`、`SoundStream`（BGM ループ）、背圧制御。Ogg デコーダ追加。
3. **プラットフォーム非依存 DSP の早期テスト（Low #7）**: `SoftwareMixer` / リサンプラ / ロックフリーリング / SPSC コマンドキューを**先に実装し、Windows 上で単体テスト**する（Oboe を待たない）。XAudio2 backend に「ソフトウェアミキサ経由」のデバッグ経路を用意し、Android で初めて動かす状況を避ける。
4. **3D**: `SoundListener` / `SoundSource` / `Mixer3D`（距離減衰・パン・出力行列・ドップラー）。
5. **ECS 統合**: `AudioSourceComponent` / `AudioListenerComponent` / `SoundSystem`、ImGui デバッグ。
6. **Oboe バックエンド**: フェーズ3の DSP を `OboeSoundBackend` に載せる + `oboe::AudioStream` 配線、Android 実機で 3D 再生確認。
7.（将来）**動画**: `VideoPlayer`（FFmpeg/MediaCodec）→ `SoundStream` へ push、A/V 同期（§3.4 の `SoundClock` を master）。

---

## 13. スレッド / メモリの考慮

- **オーディオスレッドはブロックしない**：XAudio2 コールバック・Oboe `dataCallback` 内でロック/確保/IO をしない。供給はロックフリーリング（リング枯渇時は無音 or 直近フレーム保持）。
- **3D 更新はゲームスレッド**（`SoundEngine::Update`）で行い、結果のゲイン/周波数比だけをアトミックに渡す。レンダリングの直列/非同期パイプライン（`RenderConfig.h`）とは独立。
- デコード済み PCM・ストリームリングは専用アロケータ上に確保し、`MemoryTracker` で可視化する方針（詳細は実装時）。
- `SoundHandle` は世代付きで dangling を防止。再生終了ボイスは `Update` でプールへ回収・再利用。

---

## 14. 実装ステータス（Windows 完結時点）

実装済み（XAudio2 バックエンドでビルド通過・動作する状態）:

| 機能 | 実装 | 主なファイル |
|---|---|---|
| 型/IF/ハンドル | ✅ | `SoundTypes.h` `ISoundVoice.h` `ISoundBackend.h` `SoundHandle.h` |
| XAudio2 バックエンド | ✅ | `XAudio2/XAudio2SoundBackend.*` `XAudio2/XAudio2SoundVoice.*` |
| クリップ（常駐）+ ローダ | ✅ | `SoundClip.*`（`SoundClipLoader` が拡張子で分岐） |
| 2D ワンショット再生 | ✅ | `SoundEngine::Play(RefSoundClip)` |
| バス/マスター音量 | ✅ | submix ボイス（BGM/SE/Voice） |
| ストリーミング（BGM/ループ/背圧） | ✅ | `SoundStream.*` `Decoder/WavStreamDecoder.*` |
| 3D（距離/パン/ドップラー） | ✅ | `Mixer3D.*` `SoundSource.*` `SoundListener.h` |
| ECS 統合 | ✅ | `Component/AudioSourceComponent.*` `Component/AudioListenerComponent.h` `Component/SoundSystem.*` |
| 圧縮フォーマット（MP3/AAC/WMA/M4A） | ✅ | `Decoder/MFDecoder.*`（Media Foundation。clip 全展開＋streaming 両対応） |

未実装（将来）:
- **Phase 6: Oboe バックエンド（Android）** ＋ Phase 3 の共有ソフトウェアミキサ/リサンプラ/SPSC（§3.3d/e, §8）。`MFDecoder` は Windows 専用なので Android では別デコーダ（MediaCodec 等）に差し替える。
- **Phase 7: 動画連携**（`VideoPlayer` → `SoundStream` push、§11）。
- Ogg（stb_vorbis）。MF で mp3/aac/wma/m4a を賄うため Windows では優先度低。

メモ: `MFDecoder` は COM/MF を自前初期化（`ResourceManager` のワーカースレッドでも安全）。リンクは `mfplat/mfreadwrite/mfuuid/propsys`（OS 標準、`#pragma comment` 済み）。
