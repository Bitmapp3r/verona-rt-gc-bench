# Sanitizer options
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

# Helper function to apply sanitizer flags to a target
function(apply_sanitizer_flags target sanitizer_type)
  if(sanitizer_type STREQUAL "asan")
    target_compile_options(${target} PRIVATE -fsanitize=address)
    target_link_options(${target} PRIVATE -fsanitize=address)
  elseif(sanitizer_type STREQUAL "tsan")
    target_compile_options(${target} PRIVATE -fsanitize=thread)
    target_link_options(${target} PRIVATE -fsanitize=thread)
  endif()
endfunction()

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

# Build list of sanitizer variants to create
set(SANITIZER_VARIANTS "normal")  # Start with normal build
if(ENABLE_ASAN)
  list(APPEND SANITIZER_VARIANTS "asan")
endif()
if(ENABLE_TSAN)
  list(APPEND SANITIZER_VARIANTS "tsan")
endif()
message(STATUS "Sanitizer variants: ${SANITIZER_VARIANTS}")

foreach(TEST_CATEGORY ${TEST_CATEGORIES})
foreach(TEST_MODE "sys" "con")
  subdirlist(TESTS ${TESTDIR}/${TEST_CATEGORY}/${TEST})
  foreach(TEST ${TESTS})
    unset(SRC)
    aux_source_directory(${TESTDIR}/${TEST_CATEGORY}/${TEST} SRC)
    set(TESTNAME "${TEST_CATEGORY}-${TEST_MODE}-${TEST}")
    
    # Create variants for each sanitizer option
    foreach(SANITIZER_VARIANT ${SANITIZER_VARIANTS})
      # Keep track of all targets created for this test
      unset(TEST_TARGETS)
      
      # Determine the variant name and output prefix
      if("${SANITIZER_VARIANT}" STREQUAL "normal")
        set(VARIANT_SUFFIX "")
        set(OUTPUT_PREFIX "${CMAKE_BINARY_DIR}/test")
      else()
        set(VARIANT_SUFFIX "-${SANITIZER_VARIANT}")
        set(OUTPUT_PREFIX "${CMAKE_BINARY_DIR}/${SANITIZER_VARIANT}/test")
      endif()
      
      set(VARIANT_TESTNAME "${TESTNAME}${VARIANT_SUFFIX}")
      
      if (${TEST_CATEGORY} STREQUAL "benchmarks")
        # Create a base directory for this test
        set(TEST_BASE_DIR "${OUTPUT_PREFIX}/benchmarks/${TEST}")
        
        # Create library subdirectory
        set(LIB_OUTPUT_DIR "${TEST_BASE_DIR}/${TEST_MODE}-library")
        file(MAKE_DIRECTORY ${LIB_OUTPUT_DIR})
        
        # Create library
        add_library(${VARIANT_TESTNAME}-lib SHARED ${SRC})
        set_target_properties(${VARIANT_TESTNAME}-lib
        PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        LIBRARY_OUTPUT_DIRECTORY ${LIB_OUTPUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY ${LIB_OUTPUT_DIR}
        PREFIX ""
        OUTPUT_NAME ${VARIANT_TESTNAME}
        )
        list(APPEND TEST_TARGETS ${VARIANT_TESTNAME}-lib)
        
        # Create executable subdirectory
        set(EXE_OUTPUT_DIR "${TEST_BASE_DIR}/${TEST_MODE}-executable")
        file(MAKE_DIRECTORY ${EXE_OUTPUT_DIR})
        
        # Create executable
        add_executable(${VARIANT_TESTNAME}-bin ${SRC})
        set_target_properties(${VARIANT_TESTNAME}-bin
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${EXE_OUTPUT_DIR}
        OUTPUT_NAME ${VARIANT_TESTNAME}
        )
        list(APPEND TEST_TARGETS ${VARIANT_TESTNAME}-bin)
      else()
        # Create test executable
        add_executable(${VARIANT_TESTNAME} ${SRC})
        list(APPEND TEST_TARGETS ${VARIANT_TESTNAME})
        
        # Set output directory for test
        set_target_properties(${VARIANT_TESTNAME}
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_PREFIX}/${TEST_CATEGORY}"
        )
      endif()

      # Apply common properties to all targets
      foreach(target ${TEST_TARGETS})
        target_include_directories(${target} PRIVATE ${TESTDIR}/${TEST_CATEGORY}/${TEST} ${TESTDIR})
        add_dependencies(rt_tests ${target})
        target_link_libraries(${target} verona_rt)
        
        # Apply sanitizer flags if needed
        if(NOT "${SANITIZER_VARIANT}" STREQUAL "normal")
          apply_sanitizer_flags(${target} ${SANITIZER_VARIANT})
        endif()
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
    endforeach()  # SANITIZER_VARIANTS
  endforeach()
endforeach()
endforeach()

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

# Wrap all hardcoded test definitions in sanitizer variants loop
foreach(SANITIZER_VARIANT ${SANITIZER_VARIANTS})
  # Determine the variant suffix
  if("${SANITIZER_VARIANT}" STREQUAL "normal")
    set(VARIANT_SUFFIX "")
  else()
    set(VARIANT_SUFFIX "-${SANITIZER_VARIANT}")
  endif()

  # Try to avoid testing fairness of OS.
  set_tests_properties(runtime/func-con-fair_variance${VARIANT_SUFFIX} PROPERTIES PROCESSORS 7)

  set(TESTNAME "runtime/func-sys-hashmap${VARIANT_SUFFIX}")
  add_test(${TESTNAME} ${TESTRUNNER} func-sys-hashmap${VARIANT_SUFFIX} --seed_count ${CHUNK})

  # cowngc{1,4} are basically variations on the same test.
  # The other cowngc tests are more complicated.
  foreach(VARIANT 1 4)
  foreach(CORES 2 3 4)
    foreach(FORWARD 5)
      foreach(RING 0 1 2 5)
        foreach(SEED RANGE 1 ${TOP_SEED})
          SET (TESTNAME "runtime/func-sys-cowngc${VARIANT}_${CORES}_${FORWARD}_${RING}_${SEED}${VARIANT_SUFFIX}")
          add_test(${TESTNAME} ${TESTRUNNER} func-sys-cowngc${VARIANT}${VARIANT_SUFFIX} --cores ${CORES} --seed_count ${CHUNK} --forward ${FORWARD} --ring ${RING})
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
          SET (TESTNAME "runtime/func-con-cowngc${VARIANT}_${CORES}_${FORWARD}_${RING}_${SEED}${VARIANT_SUFFIX}")
          add_test(${TESTNAME} ${TESTRUNNER} func-con-cowngc${VARIANT}${VARIANT_SUFFIX} --cores ${CORES} --seed_count 10 --forward ${FORWARD} --ring ${RING})
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
            SET (TESTNAME "runtime/func-sys-diningphilosophers_${CORES}_${PHILOSOPHERS}_${HUNGER}_${FORKS}_${SEED}${VARIANT_SUFFIX}")
            add_test(${TESTNAME} ${TESTRUNNER} func-sys-diningphilosophers${VARIANT_SUFFIX} --cores ${CORES} --seed_count ${CHUNK} --philosophers ${PHILOSOPHERS} --hunger ${HUNGER} --forks ${FORKS})
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
            SET (TESTNAME "runtime/func-con-diningphilosophers_${CORES}_${PHILOSOPHERS}_${HUNGER}_${FORKS}_${SEED}${VARIANT_SUFFIX}")
            add_test(${TESTNAME} ${TESTRUNNER} ${TESTRUNNER} func-con-diningphilosophers${VARIANT_SUFFIX} --cores ${CORES} --seed_count 10 --philosophers ${PHILOSOPHERS} --hunger ${HUNGER} --forks ${FORKS})
            set_tests_properties(${TESTNAME} PROPERTIES PROCESSORS ${CORES})
          endforeach()
        endif()
      endforeach()
    endforeach()
  endforeach()
  endforeach()

  foreach(CORES 2 3 4)
  foreach(SEED RANGE 1 ${TOP_SEED})
    SET (TESTNAME "runtime/func-sys-noticeboard_${CORES}_${SEED}${VARIANT_SUFFIX}")
    add_test(${TESTNAME} ${TESTRUNNER} func-sys-noticeboard${VARIANT_SUFFIX} --cores ${CORES} --seed_count ${CHUNK})
  endforeach()
  endforeach()

  foreach(CORES 2 3 4)
  foreach(SEED RANGE 1 ${TOP_SEED})
    SET (TESTNAME "runtime/func-sys-noticeboard_weak_memory_${CORES}_${SEED}${VARIANT_SUFFIX}")
    add_test(${TESTNAME} ${TESTRUNNER} func-sys-noticeboard_weak_memory${VARIANT_SUFFIX} --cores ${CORES} --seed_count ${CHUNK})
  endforeach()
  endforeach()

  foreach(CORES 2 3 4)
  foreach(SEED RANGE 1 ${TOP_SEED})
    SET (TESTNAME "runtime/func-sys-steal_${CORES}_${SEED}${VARIANT_SUFFIX}")
    add_test(${TESTNAME} ${TESTRUNNER} func-sys-steal${VARIANT_SUFFIX} --cores ${CORES} --seed_count ${CHUNK})
  endforeach()
  endforeach()

  foreach(CORES 2 20)
  foreach(SEED RANGE 1 ${TOP_SEED})
    SET (TESTNAME "runtime/func-sys-cown_weak_ref_${CORES}_${SEED}${VARIANT_SUFFIX}")
    add_test(${TESTNAME} ${TESTRUNNER} func-sys-cown_weak_ref${VARIANT_SUFFIX} --cores ${CORES} --seed_count ${CHUNK})
  endforeach()
  endforeach()

  foreach(CORES 2 3)
  foreach(SEED RANGE 1 ${TOP_SEED})
    SET (TESTNAME "runtime/func-sys-ext_ref_freeze_${CORES}_${SEED}${VARIANT_SUFFIX}")
    add_test(${TESTNAME} ${TESTRUNNER} func-sys-ext_ref_freeze${VARIANT_SUFFIX} --cores ${CORES} --seed_count ${CHUNK})
  endforeach()
  endforeach()

  foreach(CORES 2 3 4)
  foreach(SEED RANGE 1 ${TOP_SEED})
    SET (TESTNAME "runtime/func-sys-runtimepause_${CORES}_${SEED}${VARIANT_SUFFIX}")
    add_test(${TESTNAME} ${TESTRUNNER} func-sys-runtimepause${VARIANT_SUFFIX} --cores ${CORES} --seed_count ${CHUNK})
  endforeach()
  endforeach()

  foreach(CORES 2 3 4)
  foreach(SEED RANGE 1 ${TOP_SEED})
    SET (TESTNAME "runtime/func-sys-fair${CORES}_${SEED}${VARIANT_SUFFIX}")
    add_test(${TESTNAME} ${TESTRUNNER} func-sys-fair${VARIANT_SUFFIX} --cores ${CORES} --seed_count ${CHUNK})
  endforeach()
  endforeach()

  foreach(CORES 1 2 3 4)
  foreach(SEED RANGE 1 ${TOP_SEED})
    SET (TESTNAME "runtime/func-sys-notify${CORES}_${SEED}${VARIANT_SUFFIX}")
    add_test(${TESTNAME} ${TESTRUNNER} func-sys-notify${VARIANT_SUFFIX} --cores ${CORES} --seed_count ${CHUNK})
  endforeach()
  endforeach()

  foreach(CORES 1 2 3 4)
  foreach(SEED RANGE 1 ${TOP_SEED})
    SET (TESTNAME "runtime/func-sys-backpressure_${CORES}_${SEED}${VARIANT_SUFFIX}")
    add_test(${TESTNAME} ${TESTRUNNER} func-sys-backpressure${VARIANT_SUFFIX} --cores ${CORES} --seed_count ${CHUNK})
  endforeach()
  endforeach()

  foreach(CORES 1 2 3 4)
  foreach(SEED RANGE 1 2)
    SET (TESTNAME "runtime/func-sys-runtimepause_${CORES}_${SEED}${VARIANT_SUFFIX}")
    add_test(${TESTNAME} ${TESTRUNNER} func-sys-runtimepause${VARIANT_SUFFIX} --cores ${CORES} --seed_count 10)
  endforeach()
  endforeach()

  foreach(CORES 1 2 3 4)
  foreach(SEED RANGE 1 2)
    SET (TESTNAME "runtime/func-con-runtimepause_${CORES}_${SEED}${VARIANT_SUFFIX}")
    add_test(${TESTNAME} ${TESTRUNNER} func-con-runtimepause${VARIANT_SUFFIX} --cores ${CORES} --seed_count 10)
  endforeach()
  endforeach()

  foreach(CORES 1 2 3 4)
  foreach(SEED RANGE 1 2)
    MATH(EXPR SEEDLOWER "${SEED} * 10")
    SET (TESTNAME "runtime/func-sys-promise_${CORES}_${SEEDLOWER}${VARIANT_SUFFIX}")
    add_test(${TESTNAME} ${TESTRUNNER} func-sys-promise${VARIANT_SUFFIX} --cores ${CORES} --seed ${SEEDLOWER} --seed_count 10)
  endforeach()
  endforeach()

  foreach(CORES 1 2 3 4)
  foreach(SEED RANGE 1 5)
    MATH(EXPR SEEDLOWER "${SEED} * 1000")
    SET (TESTNAME "runtime/func-sys-external_event_source_${CORES}_${SEED}${VARIANT_SUFFIX}")
    add_test(${TESTNAME} ${TESTRUNNER} func-sys-external_event_source${VARIANT_SUFFIX} --cores ${CORES} --seed_count 1000 --seed ${SEEDLOWER})
  endforeach()
  endforeach()

  foreach(CORES 1 2 3 4)
  foreach(SEED RANGE 1 5)
    MATH(EXPR SEEDLOWER "${SEED} * 1")
    SET (TESTNAME "runtime/func-con-external_event_source_${CORES}_${SEED}${VARIANT_SUFFIX}")
    add_test(${TESTNAME} ${TESTRUNNER} func-con-external_event_source${VARIANT_SUFFIX} --cores ${CORES} --seed_count 1 --seed ${SEEDLOWER})
  endforeach()
  endforeach()

  foreach(CORES 1 2 3 4)
  foreach(SEED RANGE 1 2)
    MATH(EXPR SEEDLOWER "${SEED} * 10")
    SET (TESTNAME "runtime/func-sys-early_release_${CORES}_${SEED}${VARIANT_SUFFIX}")
    add_test(${TESTNAME} ${TESTRUNNER} func-sys-early_release${VARIANT_SUFFIX} --cores ${CORES} --seed ${SEEDLOWER} --seed_count 10)
  endforeach()
  endforeach()

  foreach(CORES 1 2 3 4)
  foreach(SEED RANGE 1 2)
    MATH(EXPR SEEDLOWER "${SEED} * 10")
    SET (TESTNAME "runtime/perf-sys-hashtable_${CORES}_${SEED}${VARIANT_SUFFIX}")
    add_test(${TESTNAME} ${TESTRUNNER} perf-sys-hashtable${VARIANT_SUFFIX} --cores ${CORES} --seed ${SEEDLOWER} --seed_count 10)
  endforeach()
  endforeach()

endforeach()  # SANITIZER_VARIANTS