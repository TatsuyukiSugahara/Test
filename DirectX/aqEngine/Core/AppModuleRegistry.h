#pragma once
#include "IAppModule.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>


namespace aq
{
	/**
	 * 起動できるモジュールの名前とファクトリの対応表。
	 * エントリが Engine::Initialize より前に登録し、Engine が起動引数 -app= の名前で生成する。
	 */
	class AppModuleRegistry
	{
	public:
		using Factory = std::function<std::unique_ptr<IAppModule>()>;


	private:
		struct Entry
		{
			std::string name;
			Factory     factory;
		};


	private:
		/** 登録順を保つ(名前を省略したときは先頭を起動する) */
		std::vector<Entry> entries_;


	public:
		/**
		 * モジュールを登録する。同じ名前の 2 回目以降はログに出して無視する
		 * @param name 起動引数 -app= と照合する名前(IAppModule::GetName と揃える)
		 * @param factory モジュールを生成する関数
		 */
		void Register(const char* name, Factory factory);

		/**
		 * 名前でモジュールを生成する
		 * @param name 空文字列なら登録順の先頭
		 * @return 見つからない・生成に失敗したときは nullptr
		 */
		std::unique_ptr<IAppModule> Create(const std::string& name) const;

		/** 登録済みの名前の一覧(登録順) */
		std::vector<std::string> GetNames() const;

		/** 何も登録されていないか */
		inline bool IsEmpty() const { return entries_.empty(); }
	};
}
