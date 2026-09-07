# --- MuJoCo ---------------------------------------------------------------
# Default: the vendored copy from scripts/fetch_mujoco.sh (run it first if
# this errors out). Override for a system install:
#   cmake -B build -DMUJOCO_INCLUDE_DIR=/path/to/include -DMUJOCO_LIBRARY=/path/to/lib/libmujoco.so
set(FERRO_MUJOCO_VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mujoco")

if(NOT MUJOCO_INCLUDE_DIR)
    set(MUJOCO_INCLUDE_DIR "${FERRO_MUJOCO_VENDOR_DIR}/include")
endif()

if(NOT MUJOCO_LIBRARY)
    file(GLOB FERRO_MUJOCO_LIB_CANDIDATES "${FERRO_MUJOCO_VENDOR_DIR}/lib/libmujoco.*")
    list(GET FERRO_MUJOCO_LIB_CANDIDATES 0 MUJOCO_LIBRARY)
endif()

add_executable(ferro_core src/main.cpp)

if(MUJOCO_INCLUDE_DIR AND MUJOCO_LIBRARY AND EXISTS "${MUJOCO_LIBRARY}")
    message(STATUS "MuJoCo found: ${MUJOCO_LIBRARY}")
    target_include_directories(ferro_core PRIVATE "${MUJOCO_INCLUDE_DIR}")
    target_link_libraries(ferro_core PRIVATE "${MUJOCO_LIBRARY}")

    add_executable(mujoco_smoke_test src/mujoco_smoke_test.cpp)
    target_include_directories(mujoco_smoke_test PRIVATE "${MUJOCO_INCLUDE_DIR}")
    target_link_libraries(mujoco_smoke_test PRIVATE "${MUJOCO_LIBRARY}")

    if(APPLE)
        get_filename_component(FERRO_MUJOCO_LIBDIR "${MUJOCO_LIBRARY}" DIRECTORY)
        set_target_properties(ferro_core mujoco_smoke_test PROPERTIES BUILD_RPATH "${FERRO_MUJOCO_LIBDIR}")
    endif()
else()
    message(FATAL_ERROR "MuJoCo not found. Run scripts/fetch_mujoco.sh, or set "
                         "-DMUJOCO_INCLUDE_DIR and -DMUJOCO_LIBRARY for a system install.")
endif()

# --- ONNX Runtime ----------------------------------------------------------
# ONNXRUNTIME_ROOT can be overridden on the command line for a non-Homebrew
# install (e.g. a manual tarball extract on Linux/R11).
if(APPLE AND NOT FERRO_ONNXRUNTIME_DIR)
    set(FERRO_ONNXRUNTIME_DIR "/opt/homebrew/opt/onnxruntime")
endif()

if(FERRO_ONNXRUNTIME_DIR AND EXISTS "${FERRO_ONNXRUNTIME_DIR}")
    find_package(onnxruntime CONFIG REQUIRED PATHS "${FERRO_ONNXRUNTIME_DIR}/lib/cmake/onnxruntime" NO_DEFAULT_PATH)
    add_executable(onnxruntime_smoke_test src/onnxruntime_smoke_test.cpp)
    target_link_libraries(onnxruntime_smoke_test PRIVATE onnxruntime::onnxruntime)
else()
    message(WARNING "ONNX Runtime not found (FERRO_ONNXRUNTIME_DIR unset) - skipping onnxruntime_smoke_test target")
endif()
