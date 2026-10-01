#pragma once


namespace aq
{
	namespace link
	{
		class LinkService;


		/**
		 * エンジン標準の命令を登録する。
		 * ping / runtime.info / runtime.commands / level.load / level.reloadAll と、評価用の debug.* 命令
		 * @param service 登録先
		 */
		void RegisterEngineCommands(LinkService& service);
	}
}
