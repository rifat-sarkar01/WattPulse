cmake_minimum_required(VERSION 3.21)
project(WattPulse VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(WATTPULSE_BUILD_TESTS "Build unit tests" ON)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus)
  add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_link_options(/LTCG /OPT:REF /OPT:ICF)
endif()

add_subdirectory(third_party/sqlite)
add_library(wattpulse_core STATIC
  src/core/energy_integrator.cpp
  src/core/power_model.cpp
  src/core/estimator.cpp
  src/core/sampler.cpp
  src/sensors/battery.cpp
  src/sensors/emi.cpp
  src/sensors/nvml.cpp
  src/sensors/adlx.cpp
  src/sensors/demo.cpp
  src/sensors/factory.cpp
  src/storage/store.cpp
  src/app/settings.cpp
  src/app/platform_win.cpp
  src/app/log.cpp
  src/ui/format.cpp
)
target_include_directories(wattpulse_core PUBLIC src)
target_include_directories(wattpulse_core SYSTEM PUBLIC third_party/adlx/Include)
target_link_libraries(wattpulse_core PUBLIC sqlite3 pdh setupapi advapi32 shell32 ole32)

add_executable(wattpulse
  src/app/main.cpp
  src/ui/dashboard.cpp
  assets/wattpulse.rc
)
set_target_properties(wattpulse PROPERTIES WIN32_EXECUTABLE TRUE)
target_link_libraries(wattpulse PRIVATE wattpulse_core d2d1 dwrite dwmapi windowscodecs)

if(WATTPULSE_BUILD_TESTS)
  enable_testing()
  # Compiled straight into the test exe: MSVC static libs do not pull in main().
  add_executable(wattpulse_tests
    third_party/catch2/catch_amalgamated.cpp
    tests/test_integrator.cpp
    tests/test_power_model.cpp
    tests/test_estimator.cpp
    tests/test_storage.cpp
    tests/test_format.cpp
    tests/test_sensor_parse.cpp
    tests/test_sampler.cpp
  )
  target_include_directories(wattpulse_tests PRIVATE third_party/catch2)
  target_compile_definitions(wattpulse_tests PRIVATE CATCH_CONFIG_NO_POSIX_SIGNALS)
  target_link_libraries(wattpulse_tests PRIVATE wattpulse_core)
  add_test(NAME wattpulse_tests COMMAND wattpulse_tests)
endif()
