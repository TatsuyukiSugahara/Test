# ----------------------------------------------------------------------------
#  MixerTest — SoftwareMixer の単体テスト相当ツール
#
#  WAV を読ませてミックス結果を WAV に書く。--selftest は
#  「2 ボイス(片方ピッチ 1.5、出力行列で左右反転)」の結果を解析解と
#  突き合わせて自動判定する(設計書 Mac移植設計.md §9 の P1 評価項目)。
#
#  CMake 専用。vcxproj は用意していない(使い方は README.md)。
# ----------------------------------------------------------------------------
add_executable(MixerTest "${CMAKE_CURRENT_SOURCE_DIR}/Main.cpp")

# SoftwareMixer.cpp / WavDecoder.cpp は aq.h(PCH)を include するため、
# ソースを直接足さず aqEngine ごとリンクする(README §2 の推奨構成)。
target_link_libraries(MixerTest PRIVATE aqEngine)

target_include_directories(MixerTest PRIVATE
	"${CMAKE_SOURCE_DIR}/aqEngine"
	"${CMAKE_SOURCE_DIR}")

# AQ_PLATFORM_* は aqEngine が PUBLIC で伝播させるので再指定は不要。
aq_apply_common_compile_options(MixerTest)

set_target_properties(MixerTest PROPERTIES FOLDER "Tools")
