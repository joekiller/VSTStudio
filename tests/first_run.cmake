# First-run checks for the per-user config (src/Wintel64/Common/UserPaths.h) and the bundled sample: install the build,
# run the installed VST with an empty home and no --config, and require that it seeds its
# config from the installed template, creates the data folders, and analyzes the sample.
# Invoked by ctest with -DBUILD=<build dir> -DSRC=<repo root> -DOUT=<work dir>.
file(REMOVE_RECURSE ${OUT})
execute_process(COMMAND ${CMAKE_COMMAND} --install ${BUILD} --prefix ${OUT}/prefix
  RESULT_VARIABLE rc OUTPUT_QUIET)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "cmake --install failed (${rc})")
endif()
find_program(VST NAMES VideoSpeedTracker PATHS ${OUT}/prefix/bin ${OUT}/prefix NO_DEFAULT_PATH REQUIRED)

# macOS: installed programs load their dylibs (VST's own FFmpeg build) through @executable_path/../Frameworks, the
# app bundle's layout (CMAKE_INSTALL_RPATH), which the release job fills when it stages VSTStudio.app. Do the same
# here: copy every dylib found on the build-tree executable's LC_RPATH dirs into Frameworks beside the installed
# executable's folder. A build against Homebrew's absolute install names finds nothing to copy and needs none.
if(APPLE)
  find_program(BUILT NAMES VideoSpeedTracker PATHS ${BUILD} ${BUILD}/bin NO_DEFAULT_PATH REQUIRED)
  execute_process(COMMAND otool -l ${BUILT} OUTPUT_VARIABLE loads)
  string(REGEX MATCHALL "path [^\n]+ \\(offset" rpaths "${loads}")
  get_filename_component(frameworks "${VST}/../../Frameworks" ABSOLUTE)
  file(MAKE_DIRECTORY ${frameworks})
  foreach(r ${rpaths})
    string(REGEX REPLACE "^path (.+) \\(offset$" "\\1" dir "${r}")
    file(GLOB dylibs "${dir}/*.dylib")
    if(dylibs)
      file(COPY ${dylibs} DESTINATION ${frameworks} FOLLOW_SYMLINK_CHAIN)
    endif()
  endforeach()
endif()

file(MAKE_DIRECTORY ${OUT}/home ${OUT}/cwd)
execute_process(
  COMMAND ${CMAKE_COMMAND} -E env HOME=${OUT}/home --unset=XDG_CONFIG_HOME --unset=XDG_DATA_HOME
          ${VST} --headless --input ${SRC}/sampleData/IPCam/20160130/manual_20160130172251.avi --stats ${OUT}/s.csv
  WORKING_DIRECTORY ${OUT}/cwd
  RESULT_VARIABLE rc OUTPUT_VARIABLE log ERROR_VARIABLE log)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "First run without --config failed (${rc}):\n${log}")
endif()

if(APPLE)
  set(config ${OUT}/home/Library/Application\ Support/VSTStudio/VST.cfg)
  set(data ${OUT}/home/Library/Application\ Support/VSTStudio/Data)
else()
  set(config ${OUT}/home/.config/vststudio/VST.cfg)
  set(data ${OUT}/home/.local/share/vststudio)
endif()
foreach(p "${config}" "${data}/IPCam" "${data}/HiLites/forPosting" "${data}/Stats" "${data}/Trace")
  if(NOT EXISTS "${p}")
    message(FATAL_ERROR "First run didn't create ${p}:\n${log}")
  endif()
endforeach()
file(STRINGS "${config}" prefix_line REGEX "^dataPathPrefix")
string(FIND "${prefix_line}" "dataPathPrefix = ${data}" at)
if(NOT at EQUAL 0)
  message(FATAL_ERROR "Seeded config doesn't point at ${data}: ${prefix_line}")
endif()

# --demo needs nothing but the install: it finds the bundled sample and the template it's calibrated with,
# and writes its csv to the per-user Stats folder.  Run from a fresh home again so nothing above helps it.
file(REMOVE_RECURSE ${OUT}/home2)
file(MAKE_DIRECTORY ${OUT}/home2)
execute_process(
  COMMAND ${CMAKE_COMMAND} -E env HOME=${OUT}/home2 --unset=XDG_CONFIG_HOME --unset=XDG_DATA_HOME
          ${VST} --demo --headless
  WORKING_DIRECTORY ${OUT}/cwd
  RESULT_VARIABLE rc OUTPUT_VARIABLE log ERROR_VARIABLE log)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "--demo failed (${rc}):\n${log}")
endif()
if(APPLE)
  set(demo_csv ${OUT}/home2/Library/Application\ Support/VSTStudio/Data/Stats/stats_demo.csv)
else()
  set(demo_csv ${OUT}/home2/.local/share/vststudio/Stats/stats_demo.csv)
endif()
file(STRINGS "${demo_csv}" demo_rows)
list(LENGTH demo_rows n)
if(n LESS 50)
  message(FATAL_ERROR "--demo logged only ${n} csv lines in ${demo_csv}:\n${log}")
endif()

# A bare run from the install folder (where Windows users unzip and start it) reads the template there; with its
# blank dataPathPrefix that is the per-user data folder, whose IPCam is empty, so VST says so and offers the sample.
# Answer "n" so it exits before opening any window.
get_filename_component(template_dir "${VST}" DIRECTORY)
if(NOT EXISTS "${template_dir}/VST.cfg")
  set(template_dir ${OUT}/prefix/share/vststudio)
endif()
file(WRITE ${OUT}/answer-no.txt "n\n")
execute_process(
  COMMAND ${CMAKE_COMMAND} -E env HOME=${OUT}/home2 --unset=XDG_CONFIG_HOME --unset=XDG_DATA_HOME ${VST}
  WORKING_DIRECTORY ${template_dir}
  INPUT_FILE ${OUT}/answer-no.txt
  RESULT_VARIABLE rc OUTPUT_VARIABLE log ERROR_VARIABLE log)
if(NOT rc EQUAL 0 OR NOT log MATCHES "No video folders found in [^\n]*IPCam" OR NOT log MATCHES "bundled 2016 Locust Avenue sample video")
  message(FATAL_ERROR "Bare run from ${template_dir} didn't offer the sample (${rc}):\n${log}")
endif()
if(log MATCHES "locustdata")
  message(FATAL_ERROR "Bare run still points at the 2016 data folder:\n${log}")
endif()

# Upgrading from v1 (named VideoSpeedTracker): a home with only the v1 config and data folders keeps using them as they
# are (UserPaths.cpp adopt()), so the absolute dataPathPrefix the v1 run seeded still points at its data, and no
# new-name folder appears.  The first run above already made such a pair under the new names; rename them to the v1
# names to stand in for an install made by v1.12.
file(REMOVE_RECURSE ${OUT}/home3)
if(APPLE)
  set(v1_config_dir "${OUT}/home3/Library/Application Support/VideoSpeedTracker")
  set(new_config_dir "${OUT}/home3/Library/Application Support/VSTStudio")
  set(v1_data_dir "${v1_config_dir}/Data")
  set(new_data_dir "${new_config_dir}/Data")
else()
  set(v1_config_dir "${OUT}/home3/.config/videospeedtracker")
  set(new_config_dir "${OUT}/home3/.config/vststudio")
  set(v1_data_dir "${OUT}/home3/.local/share/videospeedtracker")
  set(new_data_dir "${OUT}/home3/.local/share/vststudio")
endif()
file(MAKE_DIRECTORY "${v1_config_dir}" "${v1_data_dir}/IPCam" "${v1_data_dir}/Stats")
file(READ "${config}" seeded)
string(REPLACE "${data}" "${v1_data_dir}" seeded "${seeded}")
file(WRITE "${v1_config_dir}/VST.cfg" "${seeded}")
file(WRITE "${v1_data_dir}/Stats/kept.csv" "a v1 result\n")
execute_process(
  COMMAND ${CMAKE_COMMAND} -E env HOME=${OUT}/home3 --unset=XDG_CONFIG_HOME --unset=XDG_DATA_HOME
          ${VST} --headless --input ${SRC}/sampleData/IPCam/20160130/manual_20160130172251.avi --stats ${OUT}/s3.csv
  WORKING_DIRECTORY ${OUT}/cwd
  RESULT_VARIABLE rc OUTPUT_VARIABLE log ERROR_VARIABLE log)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "Run with only v1 folders failed (${rc}):\n${log}")
endif()
string(FIND "${log}" "Using config ${v1_config_dir}/VST.cfg" at)
if(at EQUAL -1)
  message(FATAL_ERROR "Run with only v1 folders didn't use ${v1_config_dir}/VST.cfg:\n${log}")
endif()
if(EXISTS "${new_config_dir}" OR EXISTS "${new_data_dir}")
  message(FATAL_ERROR "Run with only v1 folders made new-name folders beside them:\n${log}")
endif()
if(NOT EXISTS "${v1_data_dir}/Stats/kept.csv")
  message(FATAL_ERROR "Run with only v1 folders lost ${v1_data_dir}/Stats/kept.csv")
endif()
