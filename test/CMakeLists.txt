if(MSVC)
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG")
endif()

if(VERONA_CI_BUILD)
if(MSVC)
  set (CON_CORES 2)
else()
  set (CON_CORES 2 3)
endif()
else()
include(ProcessorCount)
ProcessorCount(N)
set (CON_CORES 2 ${N})
endif()

# Look for lldb
find_program(LLDB NAMES lldb)
# For CI builds attempt to run inside LLDB to get more information
# for crashes.
if (VERONA_CI_BUILD AND (NOT LLDB STREQUAL LLDB-NOTFOUND))
  # Test if lldb actually works.  Windows gives a dll exception in CI
  # for the lldb run.
  execute_process(COMMAND ${LLDB} -v RESULT_VARIABLE LLDB_EXIT_CODE)
  if (${LLDB_EXIT_CODE} EQUAL 0)
    message(STATUS "Running runtime tests inside '${LLDB}'")
    set (TESTRUNNER ${LLDB} "-batch" "--one-line" "run" "--one-line-on-crash" "thread backtrace all" "--one-line-on-crash" "quit 1" "--")
  else()
    message(STATUS "Not using '${LLDB}' to run runtime tests failed with: " ${LLDB_EXIT_CODE})
  endif()
endif()

add_custom_target(rt_tests)

set(TESTDIR ${CMAKE_CURRENT_SOURCE_DIR})
subdirlist(TEST_CATEGORIES ${TESTDIR})
foreach(TEST_CATEGORY ${TEST_CATEGORIES})
foreach(TEST_MODE "sys" "con")
  subdirlist(TESTS ${TESTDIR}/${TEST_CATEGORY})
  foreach(TEST ${TESTS})
    unset(SRC)
    aux_source_directory(${TESTDIR}/${TEST_CATEGORY}/${TEST} SRC)
    set(TESTNAME "${TEST_CATEGORY}-${TEST_MODE}-${TEST}")
    
    # Keep track of all targets created for this test
    unset(TEST_TARGETS)
    
    if (${TEST_CATEGORY} STREQUAL "benchmarks")
    # Create a base directory for this test
    set(TEST_BASE_DIR "${CMAKE_BINARY_DIR}/test/benchmarks/${TEST}")
    
    # Create library subdirectory
    set(LIB_OUTPUT_DIR "${TEST_BASE_DIR}/${TEST_MODE}-library")
    file(MAKE_DIRECTORY ${LIB_OUTPUT_DIR})
    
    # Create library
    add_library(${TESTNAME}-lib SHARED ${SRC})
    set_target_properties(${TESTNAME}-lib
    PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    LIBRARY_OUTPUT_DIRECTORY ${LIB_OUTPUT_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${LIB_OUTPUT_DIR}
    PREFIX ""
    OUTPUT_NAME ${TESTNAME}
    )
    list(APPEND TEST_TARGETS ${TESTNAME}-lib)
    
    # Create executable subdirectory
    set(EXE_OUTPUT_DIR "${TEST_BASE_DIR}/${TEST_MODE}-executable")
    file(MAKE_DIRECTORY ${EXE_OUTPUT_DIR})
    
    # Create executable
    add_executable(${TESTNAME}-bin ${SRC})
    set_target_properties(${TESTNAME}-bin
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${EXE_OUTPUT_DIR}
    OUTPUT_NAME ${TESTNAME}
    )
    list(APPEND TEST_TARGETS ${TESTNAME}-bin)
    else()
        add_executable(${TESTNAME} ${SRC})
        list(APPEND TEST_TARGETS ${TESTNAME})
    endif()

    # Apply common properties to all targets
    foreach(target ${TEST_TARGETS})
        target_include_directories(${target} PRIVATE ${TESTDIR}/${TEST_CATEGORY}/${TEST} ${TESTDIR})
        add_dependencies(rt_tests ${target})
        target_link_libraries(${target} verona_rt)
    endforeach()
    
    if (${TEST_MODE} STREQUAL "sys")
      foreach(target ${TEST_TARGETS})
        target_compile_definitions(${target} PRIVATE USE_SYSTEMATIC_TESTING)
      endforeach()
    else()
      foreach(target ${TEST_TARGETS})
        add_test("runtime/${target}" ${TESTRUNNER} ${target})
        if (VERONA_CI_BUILD)
          target_compile_definitions(${target} PRIVATE USE_FLIGHT_RECORDER)
        endif ()
      endforeach()
    endif ()
    
    if (${TEST} STREQUAL "runtimepause")
      foreach(target ${TEST_TARGETS})
        if (VERONA_CI_BUILD)
          target_compile_definitions(${target} PRIVATE USE_FLIGHT_RECORDER)
        endif ()
      endforeach()
    endif ()
  endforeach()
endforeach()
endforeach()

# Try to avoid testing fairness of OS.
set_tests_properties(runtime/func-con-fair_variance PROPERTIES PROCESSORS 7)

if (VERONA_EXPENSIVE_SYSTEMATIC_TESTING)
MATH(EXPR CHUNK "500")
else ()
MATH(EXPR CHUNK "100")
endif ()

if (VERONA_CI_BUILD)
set (TOP_SEED 3)
else ()
set (TOP_SEED 20)
endif ()

add_test(${TESTNAME} ${TESTRUNNER} func-sys-hashmap --seed_count ${CHUNK})

# cowngc{1,4} are basically variations on the same test.
# The other cowngc tests are more complicated.
foreach(VARIANT 1 4)
foreach(CORES 2 3 4)
  foreach(FORWARD 5)
    foreach(RING 0 1 2 5)
      foreach(SEED RANGE 1 ${TOP_SEED})
        SET (TESTNAME "runtime/func-sys-cowngc${VARIANT}_${CORES}_${FORWARD}_${RING}_${SEED}")
        add_test(${TESTNAME} ${TESTRUNNER} func-sys-cowngc${VARIANT} --cores ${CORES} --seed_count ${CHUNK} --forward ${FORWARD} --ring ${RING})
      endforeach()
    endforeach()
  endforeach()
endforeach()
endforeach()

foreach(VARIANT 1 4)
foreach(CORES ${CON_CORES})
  foreach(FORWARD 10)
    foreach(RING 0 1 2 10)
      foreach(SEED RANGE 1 2)
        SET (TESTNAME "runtime/func-con-cowngc${VARIANT}_${CORES}_${FORWARD}_${RING}_${SEED}")
        add_test(${TESTNAME} ${TESTRUNNER} func-con-cowngc${VARIANT} --cores ${CORES} --seed_count 10 --forward ${FORWARD} --ring ${RING})
        set_tests_properties(${TESTNAME} PROPERTIES PROCESSORS ${CORES})
      endforeach()
    endforeach()
  endforeach()
endforeach()
endforeach()

foreach(CORES 2 3)
foreach(PHILOSOPHERS 2 3 4)
  foreach(HUNGER 1 2 3 5)
    foreach(FORKS 2 3 5)
      if (${FORKS} LESS_EQUAL ${PHILOSOPHERS})
        foreach(SEED RANGE 1 ${TOP_SEED})
          SET (TESTNAME "runtime/func-sys-diningphilosophers_${CORES}_${PHILOSOPHERS}_${HUNGER}_${FORKS}_${SEED}")
          add_test(${TESTNAME} ${TESTRUNNER} func-sys-diningphilosophers --cores ${CORES} --seed_count ${CHUNK} --philosophers ${PHILOSOPHERS} --hunger ${HUNGER} --forks ${FORKS})
        endforeach()
      endif()
    endforeach()
  endforeach()
endforeach()
endforeach()

foreach(CORES ${CON_CORES})
foreach(PHILOSOPHERS 2 3 4)
  foreach(HUNGER 1 2 3 4)
    foreach(FORKS 2 3 5)
      if (${FORKS} LESS_EQUAL ${PHILOSOPHERS})
        foreach(SEED RANGE 1 2)
          SET (TESTNAME "runtime/func-con-diningphilosophers_${CORES}_${PHILOSOPHERS}_${HUNGER}_${FORKS}_${SEED}")
          add_test(${TESTNAME} ${TESTRUNNER} ${TESTRUNNER} func-con-diningphilosophers --cores ${CORES} --seed_count 10 --philosophers ${PHILOSOPHERS} --hunger ${HUNGER} --forks ${FORKS})
          set_tests_properties(${TESTNAME} PROPERTIES PROCESSORS ${CORES})
        endforeach()
      endif()
    endforeach()
  endforeach()
endforeach()
endforeach()

foreach(CORES 2 3 4)
foreach(SEED RANGE 1 ${TOP_SEED})
  SET (TESTNAME "runtime/func-sys-noticeboard_${CORES}_${SEED}")
  add_test(${TESTNAME} ${TESTRUNNER} func-sys-noticeboard --cores ${CORES} --seed_count ${CHUNK})
endforeach()
endforeach()

foreach(CORES 2 3 4)
foreach(SEED RANGE 1 ${TOP_SEED})
  SET (TESTNAME "runtime/func-sys-noticeboard_weak_memory_${CORES}_${SEED}")
  add_test(${TESTNAME} ${TESTRUNNER} func-sys-noticeboard_weak_memory --cores ${CORES} --seed_count ${CHUNK})
endforeach()
endforeach()

foreach(CORES 2 3 4)
foreach(SEED RANGE 1 ${TOP_SEED})
  SET (TESTNAME "runtime/func-sys-steal_${CORES}_${SEED}")
  add_test(${TESTNAME} ${TESTRUNNER} func-sys-steal --cores ${CORES} --seed_count ${CHUNK})
endforeach()
endforeach()

foreach(CORES 2 20)
foreach(SEED RANGE 1 ${TOP_SEED})
  SET (TESTNAME "runtime/func-sys-cown_weak_ref_${CORES}_${SEED}")
  add_test(${TESTNAME} ${TESTRUNNER} func-sys-cown_weak_ref --cores ${CORES} --seed_count ${CHUNK})
endforeach()
endforeach()

foreach(CORES 2 3)
foreach(SEED RANGE 1 ${TOP_SEED})
  SET (TESTNAME "runtime/func-sys-ext_ref_freeze_${CORES}_${SEED}")
  add_test(${TESTNAME} ${TESTRUNNER} func-sys-ext_ref_freeze --cores ${CORES} --seed_count ${CHUNK})
endforeach()
endforeach()

foreach(CORES 2 3 4)
foreach(SEED RANGE 1 ${TOP_SEED})
  SET (TESTNAME "runtime/func-sys-runtimepause_${CORES}_${SEED}")
  add_test(${TESTNAME} ${TESTRUNNER} func-sys-runtimepause --cores ${CORES} --seed_count ${CHUNK})
endforeach()
endforeach()

foreach(CORES 2 3 4)
foreach(SEED RANGE 1 ${TOP_SEED})
  SET (TESTNAME "runtime/func-sys-fair${CORES}_${SEED}")
  add_test(${TESTNAME} ${TESTRUNNER} func-sys-fair --cores ${CORES} --seed_count ${CHUNK})
endforeach()
endforeach()

foreach(CORES 1 2 3 4)
foreach(SEED RANGE 1 ${TOP_SEED})
  SET (TESTNAME "runtime/func-sys-notify${CORES}_${SEED}")
  add_test(${TESTNAME} ${TESTRUNNER} func-sys-notify --cores ${CORES} --seed_count ${CHUNK})
endforeach()
endforeach()

foreach(CORES 1 2 3 4)
foreach(SEED RANGE 1 ${TOP_SEED})
  SET (TESTNAME "runtime/func-sys-backpressure_${CORES}_${SEED}")
  add_test(${TESTNAME} ${TESTRUNNER} func-sys-backpressure --cores ${CORES} --seed_count ${CHUNK})
endforeach()
endforeach()

foreach(CORES 1 2 3 4)
foreach(SEED RANGE 1 2)
  SET (TESTNAME "runtime/func-sys-runtimepause_${CORES}_${SEED}")
  add_test(${TESTNAME} ${TESTRUNNER} func-sys-runtimepause --cores ${CORES} --seed_count 10)
endforeach()
endforeach()

foreach(CORES 1 2 3 4)
foreach(SEED RANGE 1 2)
  SET (TESTNAME "runtime/func-con-runtimepause_${CORES}_${SEED}")
  add_test(${TESTNAME} ${TESTRUNNER} func-con-runtimepause --cores ${CORES} --seed_count 10)
endforeach()
endforeach()

foreach(CORES 1 2 3 4)
foreach(SEED RANGE 1 2)
  MATH(EXPR SEEDLOWER "${SEED} * 10")
  SET (TESTNAME "runtime/func-sys-promise_${CORES}_${SEEDLOWER}")
  add_test(${TESTNAME} ${TESTRUNNER} func-sys-promise --cores ${CORES} --seed ${SEEDLOWER} --seed_count 10)
endforeach()
endforeach()

foreach(CORES 1 2 3 4)
foreach(SEED RANGE 1 5)
  MATH(EXPR SEEDLOWER "${SEED} * 1000")
  SET (TESTNAME "runtime/func-sys-external_event_source_${CORES}_${SEED}")
  add_test(${TESTNAME} ${TESTRUNNER} func-sys-external_event_source --cores ${CORES} --seed_count 1000 --seed ${SEEDLOWER})
endforeach()
endforeach()

foreach(CORES 1 2 3 4)
foreach(SEED RANGE 1 5)
  MATH(EXPR SEEDLOWER "${SEED} * 1")
  SET (TESTNAME "runtime/func-con-external_event_source_${CORES}_${SEED}")
  add_test(${TESTNAME} ${TESTRUNNER} func-con-external_event_source --cores ${CORES} --seed_count 1 --seed ${SEEDLOWER})
endforeach()
endforeach()

foreach(CORES 1 2 3 4)
foreach(SEED RANGE 1 2)
  MATH(EXPR SEEDLOWER "${SEED} * 10")
  SET (TESTNAME "runtime/func-sys-early_release_${CORES}_${SEED}")
  add_test(${TESTNAME} ${TESTRUNNER} func-sys-early_release --cores ${CORES} --seed ${SEEDLOWER} --seed_count 10)
endforeach()
endforeach()

foreach(CORES 1 2 3 4)
foreach(SEED RANGE 1 2)
  MATH(EXPR SEEDLOWER "${SEED} * 10")
  SET (TESTNAME "runtime/perf-sys-hashtable_${CORES}_${SEED}")
  add_test(${TESTNAME} ${TESTRUNNER} perf-sys-hashtable --cores ${CORES} --seed ${SEEDLOWER} --seed_count 10)
endforeach()
endforeach()
