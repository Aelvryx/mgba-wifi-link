if(BUILD_PERF)
	set(PERF_SRC ${CMAKE_CURRENT_SOURCE_DIR}/perf-main.c)
	if(UNIX AND NOT APPLE)
		list(APPEND PERF_LIB rt)
	endif()

	add_executable(${BINARY_NAME}-perf ${PERF_SRC})
	target_link_libraries(${BINARY_NAME}-perf ${BINARY_NAME} ${PERF_LIB} ${OS_LIB})
	set_target_properties(${BINARY_NAME}-perf PROPERTIES COMPILE_DEFINITIONS "${OS_DEFINES};${FEATURE_DEFINES};${FUNCTION_DEFINES}" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
	install(TARGETS ${BINARY_NAME}-perf DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT ${BINARY_NAME}-perf)
	install(FILES "${PROJECT_SOURCE_DIR}/tools/perf.py" DESTINATION "${LIBDIR}/${BINARY_NAME}" COMPONENT ${BINARY_NAME}-perf)
endif()

if(BUILD_TEST)
	add_executable(${BINARY_NAME}-fuzz ${CMAKE_CURRENT_SOURCE_DIR}/fuzz-main.c)
	target_link_libraries(${BINARY_NAME}-fuzz ${BINARY_NAME})
	set_target_properties(${BINARY_NAME}-fuzz PROPERTIES COMPILE_DEFINITIONS "${OS_DEFINES};${FEATURE_DEFINES};${FUNCTION_DEFINES}")
	add_executable(tbl-fuzz ${CMAKE_CURRENT_SOURCE_DIR}/tbl-fuzz-main.c)
	target_link_libraries(tbl-fuzz ${BINARY_NAME})
	set_target_properties(tbl-fuzz PROPERTIES COMPILE_DEFINITIONS "${OS_DEFINES};${FEATURE_DEFINES};${FUNCTION_DEFINES}")
	install(TARGETS ${BINARY_NAME}-fuzz tbl-fuzz DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT ${BINARY_NAME}-test)
endif()

if(BUILD_SUITE)
	enable_testing()
	include_directories(AFTER ${CMOCKA_INCLUDE_DIRS})
	link_directories(${CMOCKA_LIBRARY_DIRS})

	foreach(TEST IN LISTS TEST_SRC)
		string(REPLACE "${PROJECT_SOURCE_DIR}/src/" "" TEST_NAME "${TEST}")
		string(REPLACE "/" "-" TEST_NAME "${TEST_NAME}")
		string(REPLACE "-test" "" TEST_NAME "${TEST_NAME}")
		string(REPLACE ".c" "" TEST_NAME "${TEST_NAME}")
		add_executable(test-${TEST_NAME} ${TEST})
		target_link_libraries(test-${TEST_NAME} ${BINARY_NAME} ${PLATFORM_LIBRARY} cmocka)
		set_target_properties(test-${TEST_NAME} PROPERTIES COMPILE_DEFINITIONS "${OS_DEFINES};${FEATURE_DEFINES};${FUNCTION_DEFINES}")
		if(TEST_NAME STREQUAL "gba-netplay-integration")
			target_compile_definitions(test-${TEST_NAME} PRIVATE
				GBA_LINK_TEST_ROM_PATH="${PROJECT_SOURCE_DIR}/tools/gba-link-test-rom/fixtures/gba-link-test.gba")
		endif()
		if(TEST_NAME STREQUAL "gba-replicated-pair-scheduler" OR
		   TEST_NAME STREQUAL "gba-replicated-pair")
			target_compile_definitions(test-${TEST_NAME} PRIVATE
				GBA_LINK_CONTINUOUS_ROM_PATH="${PROJECT_SOURCE_DIR}/tools/gba-link-test-rom/fixtures/gba-link-continuous.gba")
		endif()
		add_test(${TEST_NAME} test-${TEST_NAME})
	endforeach()

	if(BUILD_LIBRETRO AND M_CORE_GBA)
		add_executable(test-libretro-replicated-pair-frontend
			${CMAKE_CURRENT_SOURCE_DIR}/libretro-replicated-pair-frontend.c
			${PROJECT_SOURCE_DIR}/src/platform/libretro/replicated-pair-frontend.c)
		target_include_directories(test-libretro-replicated-pair-frontend PRIVATE
			${PROJECT_SOURCE_DIR}/src/platform/libretro)
		target_link_libraries(test-libretro-replicated-pair-frontend
			${BINARY_NAME} ${PLATFORM_LIBRARY} cmocka)
		set_target_properties(test-libretro-replicated-pair-frontend PROPERTIES
			COMPILE_DEFINITIONS "${OS_DEFINES};${FEATURE_DEFINES};${FUNCTION_DEFINES};GBA_LINK_CONTINUOUS_ROM_PATH=\"${PROJECT_SOURCE_DIR}/tools/gba-link-test-rom/fixtures/gba-link-continuous.gba\"")
		add_test(libretro-replicated-pair-frontend test-libretro-replicated-pair-frontend)

		add_executable(test-libretro-gba-wifi-link
			${CMAKE_CURRENT_SOURCE_DIR}/libretro-gba-wifi-link.c
			${PROJECT_SOURCE_DIR}/src/platform/libretro/gba-wifi-link.c)
		target_include_directories(test-libretro-gba-wifi-link PRIVATE
			${PROJECT_SOURCE_DIR}/src/platform/libretro)
		target_link_libraries(test-libretro-gba-wifi-link
			${BINARY_NAME} ${PLATFORM_LIBRARY} cmocka)
		set_target_properties(test-libretro-gba-wifi-link PROPERTIES
			COMPILE_DEFINITIONS "${OS_DEFINES};${FEATURE_DEFINES};${FUNCTION_DEFINES};M_LIBRETRO_GBA_WIFI_LINK_TEST")
		add_test(libretro-gba-wifi-link test-libretro-gba-wifi-link)

		foreach(REPLAY_ROLE IN ITEMS Host Client)
			add_library(test-libretro-gba-wifi-link-replay-${REPLAY_ROLE} OBJECT
				${PROJECT_SOURCE_DIR}/src/platform/libretro/gba-wifi-link.c)
			target_include_directories(test-libretro-gba-wifi-link-replay-${REPLAY_ROLE} PRIVATE
				${PROJECT_SOURCE_DIR}/src/platform/libretro)
			set_target_properties(test-libretro-gba-wifi-link-replay-${REPLAY_ROLE} PROPERTIES
				COMPILE_DEFINITIONS "${OS_DEFINES};${FEATURE_DEFINES};${FUNCTION_DEFINES};M_LIBRETRO_GBA_WIFI_LINK_TEST;M_LIBRETRO_GBA_WIFI_LINK_INSTANCE_PREFIX=mLibretroGBAWifiLinkReplay${REPLAY_ROLE}")
		endforeach()
		add_executable(test-libretro-gba-wifi-link-replay
			${CMAKE_CURRENT_SOURCE_DIR}/libretro-gba-wifi-link-replay.c
			$<TARGET_OBJECTS:test-libretro-gba-wifi-link-replay-Host>
			$<TARGET_OBJECTS:test-libretro-gba-wifi-link-replay-Client>)
		target_include_directories(test-libretro-gba-wifi-link-replay PRIVATE
			${PROJECT_SOURCE_DIR}/src/platform/libretro)
		target_link_libraries(test-libretro-gba-wifi-link-replay
			${BINARY_NAME} ${PLATFORM_LIBRARY} cmocka)
		set_target_properties(test-libretro-gba-wifi-link-replay PROPERTIES
			COMPILE_DEFINITIONS "${OS_DEFINES};${FEATURE_DEFINES};${FUNCTION_DEFINES};GBA_LINK_CONTINUOUS_ROM_PATH=\"${PROJECT_SOURCE_DIR}/tools/gba-link-test-rom/fixtures/gba-link-continuous.gba\"")
		add_test(libretro-gba-wifi-link-replay test-libretro-gba-wifi-link-replay)
	endif()
endif()

if(BUILD_CINEMA)
	enable_testing()
	add_executable(${BINARY_NAME}-cinema ${CMAKE_CURRENT_SOURCE_DIR}/cinema-main.c)
	target_link_libraries(${BINARY_NAME}-cinema ${BINARY_NAME} ${PLATFORM_LIBRARY})
	set_target_properties(${BINARY_NAME}-cinema PROPERTIES COMPILE_DEFINITIONS "${OS_DEFINES};${FEATURE_DEFINES};${FUNCTION_DEFINES}")
	if(NOT MSVC)
		set_target_properties(${BINARY_NAME}-cinema PROPERTIES COMPILE_FLAGS "-Wno-format-truncation")
	endif()
	add_test(cinema ${BINARY_NAME}-cinema -v)
	install(TARGETS ${BINARY_NAME}-cinema DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT ${BINARY_NAME}-test)
endif()
