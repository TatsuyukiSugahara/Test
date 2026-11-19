# ----------------------------------------------------------------------------
#  ReflectionTest — リフレクション(Reflect と型消去)の検査ツール
#
#  全登録コンポーネントの serializePtr / deserializePtr を既定値の実体で試し
#  (T1 往復 / T2 疎な JSON)、既存アセットの書き出し直しをダンプする(T3)。
#  設計書 エディタ全体設計.md 6.4 節「5. 検査ツール」(E6a)。
#
#  CMake 専用。vcxproj は用意していない(使い方は README.md)。
# ----------------------------------------------------------------------------
add_executable(ReflectionTest "${CMAKE_CURRENT_SOURCE_DIR}/Main.cpp")

# ComponentRegistry とコンポーネントの実体はすべて aqEngine にあるので、aqEngine ごとリンクする。
target_link_libraries(ReflectionTest PRIVATE aqEngine)

target_include_directories(ReflectionTest PRIVATE
	"${CMAKE_SOURCE_DIR}/aqEngine"
	"${CMAKE_SOURCE_DIR}")

# AQ_PLATFORM_* は aqEngine が PUBLIC で伝播させるので再指定は不要。
aq_apply_common_compile_options(ReflectionTest)

set_target_properties(ReflectionTest PROPERTIES FOLDER "Tools")
