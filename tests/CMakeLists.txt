cmake_minimum_required(VERSION 3.16)
project(app_hub_core_tests LANGUAGES CXX)

enable_testing()

add_executable(app_hub_core_tests
    core_tests.cpp
    ../components/app_core/app_manager.cpp
    ../components/app_core/app_registry.cpp
    ../components/app_core/display_policy.cpp
    ../components/app_core/schedule.cpp
    ../components/app_core/settings_model.cpp
)

target_include_directories(app_hub_core_tests PRIVATE
    ../components/app_core/include
)
target_compile_features(app_hub_core_tests PRIVATE cxx_std_17)
target_compile_options(app_hub_core_tests PRIVATE -Wall -Wextra -Werror -pedantic)

add_test(NAME app_hub_core_tests COMMAND app_hub_core_tests)

