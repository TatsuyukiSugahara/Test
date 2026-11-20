# ----------------------------------------------------------------------------
#  ReflectionTest — リフレクション(Reflect と型消去)の検査ツール
#
#  全登録コンポーネントの serializePtr / deserializePtr を既定値の実体で試し
#  (T1 往復 / T2 疎な JSON)、既存アセットの書き出し直しをダンプする(T3)。
#  Debug 構成では画面無しの ImGui で編集イベント(T4)と属性(T5)も試す。
#  T6 編集ノードの往復(Debug のみ)と T7 数値の書き出し(E4a)も行う。
#  設計書 エディタ全体設計.md 6.4 節「5. 検査ツール」(E6a / E6b)、7.8 節「9. 検査ツールの追加」(E4a)。
#
#  CMake 専用。vcxproj は用意していない(使い方は README.md)。
# ----------------------------------------------------------------------------
add_executable(ReflectionTest "${CMAKE_CURRENT_SOURCE_DIR}/Main.cpp")

# ComponentRegistry とコンポーネントの実体はすべて aqEngine にあるので、aqEngine ごとリンクする。
target_link_libraries(ReflectionTest PRIVATE aqEngine)

target_include_directories(ReflectionTest PRIVATE
	"${CMAKE_SOURCE_DIR}/aqEngine"
	"${CMAKE_SOURCE_DIR}")

# T6 / T7 の試験用データ(TestData/)の場所。実行時のカレントディレクトリに依らず読めるように、ソースの位置を埋め込む。
target_compile_definitions(ReflectionTest PRIVATE
	REFLECTION_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/TestData")

# AQ_PLATFORM_* は aqEngine が PUBLIC で伝播させるので再指定は不要。
aq_apply_common_compile_options(ReflectionTest)

set_target_properties(ReflectionTest PROPERTIES FOLDER "Tools")
