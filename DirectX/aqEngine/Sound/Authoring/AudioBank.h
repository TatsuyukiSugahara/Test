#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include "AudioDefs.h"


namespace aq
{
	namespace util { class JsonValue; }

	namespace audio
	{
		// 1 つの AudioBank（JSON）からロードした定義を保持する（§13）。
		// フェーズ1: kinds / objects(Sound) / events(Play/Stop) を読み、
		// 非ストリーミング clip を事前ロードする。複数 Bank のマージは AudioDirector が行う。
		class AudioBank
		{
		// ── メンバ変数 ──
		private:
			std::string                            bankId_;
			std::unordered_map<NameId, KindDef>        kinds_;
			std::unordered_map<NameId, AttenuationDef> attenuations_;
			std::unordered_map<NameId, SoundObjectDef> objects_;
			std::unordered_map<NameId, EventDef>       events_;
			std::unordered_map<NameId, RtpcDef>        rtpc_;
			std::vector<RtpcBindingDef>                rtpcBindings_;
			std::vector<StateRuleDef>                  stateRules_;
			std::vector<DuckingDef>                    duckings_;
			std::unordered_map<NameId, std::string>    names_;   // デバッグ用 逆引き（CRC32 衝突検出にも使う）

		// ── メンバ関数 ──
		public:
			AudioBank() = default;

			// JSON ファイルをロードして定義を構築する。成功で true。
			bool LoadFromFile(const char* path);

			const std::string& GetBankId() const { return bankId_; }

			const KindDef*        FindKind(NameId id)        const;
			const AttenuationDef* FindAttenuation(NameId id) const;
			const SoundObjectDef* FindObject(NameId id)      const;
			const EventDef*       FindEvent(NameId id)       const;

			const std::unordered_map<NameId, EventDef>& GetEvents() const { return events_; }
			const RtpcDef*                     FindRtpc(NameId id) const;
			const std::vector<RtpcBindingDef>& GetRtpcBindings() const { return rtpcBindings_; }
			const std::vector<StateRuleDef>&   GetStateRules()   const { return stateRules_; }
			const std::vector<DuckingDef>&     GetDuckings()     const { return duckings_; }
			const std::unordered_map<NameId, SoundObjectDef>& GetObjects()  const { return objects_; }
			const std::unordered_map<NameId, RtpcDef>&        GetRtpcDefs() const { return rtpc_; }
			const std::unordered_map<NameId, std::string>& GetNames() const { return names_; }
			const std::string& NameOf(NameId id) const;

		private:
			// 名前を CRC32 化し、逆引き登録（衝突検出つき）。
			NameId Intern(const std::string& name);

			// SoundObject ノードを再帰パースして objects_ へ登録し、NameId を返す。
			NameId ParseObjectNode(const std::string& name, const util::JsonValue& def,
			                       NameId inheritedKind, const std::string& basePath);
			// 子エントリ（{"clip"}/{"ref"}/インライン）をパースして子の NameId を返す。
			NameId ParseChild(const std::string& parentName, int index, const util::JsonValue& child,
			                  NameId inheritedKind, const std::string& basePath);
			// 1 つの Action（event / stateRule 共通）をパースする。
			ActionDef ParseAction(const util::JsonValue& action);
		};
	}
}
