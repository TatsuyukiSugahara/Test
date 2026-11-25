# ----------------------------------------------------------------------------
#  ReflectionTest — リフレクション(Reflect と型消去)の検査ツール
#
#  全登録コンポーネントの serializePtr / deserializePtr を既定値の実体で試し
#  (T1 往復 / T2 疎な JSON)、既存アセットの書き出し直しをダンプする(T3)。
#  Debug 構成では画面無しの ImGui で編集イベント(T4)と属性(T5)も試す。
#  T6 編集ノードの往復(Debug のみ)と T7 数値の書き出し(E4a)も行う。
#  T13 Undo / Redo(E8a。Debug のみ)は Editor のコマンドと履歴の .cpp を直接コンパイルして試す。
#  T14 ログの通知(E8b。全構成)は Editor の ConsoleLog.cpp を直接コンパイルして試す。
#  T15 (a) アセットの一覧(E9a。全構成)は Editor の AssetCatalog.cpp を直接コンパイルして試す。
#  設計書 エディタ全体設計.md 6.4 節「5. 検査ツール」(E6a / E6b)、7.8 節「9. 検査ツールの追加」(E4a)。
#
#  CMake 専用。vcxproj は用意していない(使い方は README.md)。
# ----------------------------------------------------------------------------
add_executable(ReflectionTest "${CMAKE_CURRENT_SOURCE_DIR}/Main.cpp")

# T13 Undo / Redo は Editor のコマンドと履歴をそのままコンパイルして使う。
# LevelEditModel が AQ_DEBUG_IMGUI(Debug)だけにあるので、Debug のときだけソースに足す。
target_sources(ReflectionTest PRIVATE
	"$<$<CONFIG:Debug>:${CMAKE_SOURCE_DIR}/Editor/Commands/NodeCommands.cpp>"
	"$<$<CONFIG:Debug>:${CMAKE_SOURCE_DIR}/Editor/Commands/ComponentCommands.cpp>"
	"$<$<CONFIG:Debug>:${CMAKE_SOURCE_DIR}/Editor/CommandHistory.cpp>")

# T14 ログの通知は Editor の ConsoleLog をそのままコンパイルして使う。ImGui にも LevelEditModel にも依らないので全構成で足す。
target_sources(ReflectionTest PRIVATE
	"${CMAKE_SOURCE_DIR}/Editor/ConsoleLog.cpp")

# T15 (a) アセットの一覧は Editor の AssetCatalog をそのままコンパイルして使う。ImGui に依らないので全構成で足す。
target_sources(ReflectionTest PRIVATE
	"${CMAKE_SOURCE_DIR}/Editor/AssetCatalog.cpp")

# ComponentRegistry とコンポーネントの実体はすべて aqEngine にあるので、aqEngine ごとリンクする。
target_link_libraries(ReflectionTest PRIVATE aqEngine)

target_include_directories(ReflectionTest PRIVATE
	"${CMAKE_SOURCE_DIR}/aqEngine"
	"${CMAKE_SOURCE_DIR}"
	"${CMAKE_SOURCE_DIR}/Editor")

# T6 / T7 の試験用データ(TestData/)の場所。実行時のカレントディレクトリに依らず読めるように、ソースの位置を埋め込む。
target_compile_definitions(ReflectionTest PRIVATE
	REFLECTION_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/TestData")

# AQ_PLATFORM_* は aqEngine が PUBLIC で伝播させるので再指定は不要。
aq_apply_common_compile_options(ReflectionTest)

set_target_properties(ReflectionTest PROPERTIES FOLDER "Tools")
