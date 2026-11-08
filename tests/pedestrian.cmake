# Walkers on the near sidewalk of a short view (Joe's 7½ St SW setup, 2026-11-07): does VST keep them from producing
# wrong-way speeds, and still time the cars around them?
#
# In a 15-20 ft view a person on the near sidewalk stands taller in the picture than the travel lane, so when a car comes
# the other way their blobs touch.  A walker coming in at the left edge just as a legal car came in at the right was
# logged as an 18 mph wrong-way car: the car "hid" the walker's front bumper, the tracker filled it in with its entry
# look-ahead, and the walker's track ran across the zone on that guess (VehicleDynamics::hiddenFrontBumper(); the
# tracker-level sweep is tests/pedestrian_merge_test.cpp).
#
# walker_clip (tests/walker_clip.cpp) draws each scene; VST runs on it with the config below, a 20 ft view at 64 px/ft
# with subFrameTiming on, NE (right to left) the legal way, and minLoggedSpeed 10 so a walker's own 3 mph track is counted
# but not listed, as in a real setup.  Each scene must give exactly the rows listed (direction:speed, each within 3 mph):
#   scene                   required          before the fix (v2.0.0)
#   walker_then_car         (no wrong-way)    SW 18, called out WRONG WAY
#   walker_crossing_car     NE:25             nothing (the walker's track kept the right end closed to new cars)
#   walker_right_then_car   SW:25             SW 27  (a real wrong-way car next to a walker: must stay)
#   wrong_way_car           SW:25             SW 25
#   legal_car               NE:25             NE 25
# and every SW row, and only those, must be called out WRONG WAY on VST's output.
#
# Invoked by ctest with -DVST=<exe> -DCLIP=<walker_clip exe> -DOUT=<work dir>.
cmake_policy(VERSION 3.16)

file(MAKE_DIRECTORY ${OUT})
set(CFG ${OUT}/pedestrian_20ft.cfg)
# Calibration: 25 mph is 78.2 px a frame at 64 px/ft and 30 fps, so the 1023 px zone takes 13.08 frames; the keys are whole
# numbers, so they hold the frames at 2.5 mph (131), the same calibration.  Margins as tests/short_view.cmake's 20 ft view.
file(WRITE ${CFG} "# Written by tests/pedestrian.cmake: walker_clip's synthetic 20 ft view.
L2RDirection = \"SW\"
R2LDirection = \"NE\"
LegalDirection = NE
obstruction = [0,0]
AnalysisBoxLeft = 0
AnalysisBoxTop = 65
AnalysisBoxWidth = 1279
AnalysisBoxHeight = 622
speedLineLeft = 128
speedLineRight = 1151
R2LStreetY = 455
L2RStreetY = 455
nextHeight = 278
maxL2RDistOnEntry = 417
maxR2LDistOnEntry = 417
entryLookBack = 1000
SLOP = 83
largeVehicleArea = 100
CalibrationSpeed = 2.5
CalibrationFramesL2R = 131
CalibrationFramesR2L = 131
CalibrationFPS = 30
subFrameTiming = 1
minLoggedSpeed = 10
")

set(failures "")
foreach(case
    "walker_then_car="
    "walker_crossing_car=NE:25"
    "walker_right_then_car=SW:25"
    "wrong_way_car=SW:25"
    "legal_car=NE:25")
  string(REPLACE "=" ";" parts "${case}")
  list(GET parts 0 scene)
  list(LENGTH parts n)
  set(want "")
  if(n GREATER 1)
    list(GET parts 1 want)
  endif()

  set(AVI ${OUT}/${scene}.avi)
  execute_process(COMMAND ${CLIP} ${AVI} ${scene} RESULT_VARIABLE rc ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "walker_clip couldn't draw ${scene} (${rc}):\n${err}")
  endif()
  set(CSV ${OUT}/stats_${scene}.csv)
  execute_process(
    COMMAND ${VST} --headless --input ${AVI} --config ${CFG} --stats ${CSV}
    RESULT_VARIABLE rc OUTPUT_VARIABLE log ERROR_VARIABLE log)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "VideoSpeedTracker exited with ${rc} on ${scene}:\n${log}")
  endif()

  # The rows, as direction:speed.
  file(STRINGS ${CSV} lines)
  list(REMOVE_AT lines 0)
  set(got "")
  set(wrong_way_rows 0)
  foreach(line IN LISTS lines)
    string(REPLACE "\r" "" line "${line}")
    string(REPLACE ", " ";" fields "${line}")
    list(GET fields 3 dir)
    list(GET fields 12 speed)
    list(APPEND got "${dir}:${speed}")
    if(dir STREQUAL "SW")
      math(EXPR wrong_way_rows "${wrong_way_rows} + 1")
    endif()
  endforeach()
  message(STATUS "${scene}: ${got}")

  # Same rows as wanted, in order, each speed within 3 mph.
  list(LENGTH got n_got)
  list(LENGTH want n_want)
  set(ok TRUE)
  if(NOT n_got EQUAL n_want)
    set(ok FALSE)
  elseif(n_want GREATER 0)
    math(EXPR last "${n_want} - 1")
    foreach(i RANGE ${last})
      list(GET got ${i} g)
      list(GET want ${i} w)
      string(REPLACE ":" ";" g "${g}")
      string(REPLACE ":" ";" w "${w}")
      list(GET g 0 gdir)
      list(GET g 1 gspeed)
      list(GET w 0 wdir)
      list(GET w 1 wspeed)
      math(EXPR off "${gspeed} - ${wspeed}")
      if(NOT gdir STREQUAL wdir OR off GREATER 3 OR off LESS -3)
        set(ok FALSE)
      endif()
    endforeach()
  endif()
  if(NOT ok)
    string(APPEND failures "  ${scene}: got [${got}], want [${want}]\n")
  endif()

  # Called out WRONG WAY exactly once per SW row.
  string(REGEX MATCHALL "WRONG WAY" callouts "${log}")
  list(LENGTH callouts n_callouts)
  if(NOT n_callouts EQUAL wrong_way_rows)
    string(APPEND failures "  ${scene}: ${n_callouts} WRONG WAY callouts for ${wrong_way_rows} SW rows\n")
  endif()
endforeach()

if(failures)
  message(FATAL_ERROR "Walker scenes went wrong:\n${failures}")
endif()
message(STATUS "Walker scenes: no wrong-way speed from a walker, cars around walkers timed")
