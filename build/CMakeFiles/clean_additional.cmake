# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "RelWithDebInfo")
  file(REMOVE_RECURSE
  "CMakeFiles/lsp-audio-player_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/lsp-audio-player_autogen.dir/ParseCache.txt"
  "CMakeFiles/lspa_core_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/lspa_core_autogen.dir/ParseCache.txt"
  "CMakeFiles/lspa_plugins_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/lspa_plugins_autogen.dir/ParseCache.txt"
  "CMakeFiles/lspa_tests_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/lspa_tests_autogen.dir/ParseCache.txt"
  "CMakeFiles/lv2probe_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/lv2probe_autogen.dir/ParseCache.txt"
  "lsp-audio-player_autogen"
  "lspa_core_autogen"
  "lspa_plugins_autogen"
  "lspa_tests_autogen"
  "lv2probe_autogen"
  )
endif()
