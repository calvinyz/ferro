# --- MuJoCo ---------------------------------------------------------------
# Default: the vendored copy in third_party/mujoco/ (shared with rust_sidecar).
# Override for a system install:
#   cmake -B build -DMUJOCO_INCLUDE_DIR=/path/to/include -DMUJOCO_LIBRARY=/path/to/lib/libmujoco.so
set(FERRO_MUJOCO_VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/mujoco")

if(NOT MUJOCO_INCLUDE_DIR)
    set(MUJOCO_INCLUDE_DIR "${FERRO_MUJOCO_VENDOR_DIR}/include")
endif()

if(NOT MUJOCO_LIBRARY)
    file(GLOB FERRO_MUJOCO_LIB_CANDIDATES "${FERRO_MUJOCO_VENDOR_DIR}/lib/libmujoco.*")
    list(GET FERRO_MUJOCO_LIB_CANDIDATES 0 MUJOCO_LIBRARY)
endif()

add_executable(ferro_core src/main.cpp)

# --- Model & Policy paths (compile-time constants) -----------
# Can be overridden on command line:
#   cmake -B build -DINVERTED_PENDULUM_PATH=/path/to/inverted_pendulum.xml -DPOLICY_PATH=/path/to/policy.onnx
if(NOT INVERTED_PENDULUM_PATH)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/models/inverted_pendulum.xml")
        set(INVERTED_PENDULUM_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/models/inverted_pendulum.xml")
    endif()
endif()

if(NOT POLICY_PATH)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/policies/toy_policy.onnx")
        set(POLICY_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/policies/toy_policy.onnx")
    endif()
endif()

if(MUJOCO_INCLUDE_DIR AND MUJOCO_LIBRARY AND EXISTS "${MUJOCO_LIBRARY}")
    message(STATUS "MuJoCo found: ${MUJOCO_LIBRARY}")
    message(STATUS "Inverted Pendulum model: ${INVERTED_PENDULUM_PATH}")
    message(STATUS "Policy: ${POLICY_PATH}")

    target_include_directories(ferro_core PRIVATE "${MUJOCO_INCLUDE_DIR}")
    target_link_libraries(ferro_core PRIVATE "${MUJOCO_LIBRARY}")

    # Pass paths to C++ as compile-time constants
    target_compile_definitions(ferro_core PRIVATE
        INVERTED_PENDULUM_PATH="${INVERTED_PENDULUM_PATH}"
        POLICY_PATH="${POLICY_PATH}"
    )

    add_executable(mujoco_smoke_test src/mujoco_smoke_test.cpp)
    target_include_directories(mujoco_smoke_test PRIVATE "${MUJOCO_INCLUDE_DIR}")
    target_link_libraries(mujoco_smoke_test PRIVATE "${MUJOCO_LIBRARY}")

    get_filename_component(FERRO_MUJOCO_LIBDIR "${MUJOCO_LIBRARY}" DIRECTORY)
    set_target_properties(ferro_core mujoco_smoke_test PROPERTIES BUILD_RPATH "${FERRO_MUJOCO_LIBDIR}")
else()
    message(FATAL_ERROR "MuJoCo not found. Run scripts/fetch_mujoco.sh, or set "
                         "-DMUJOCO_INCLUDE_DIR and -DMUJOCO_LIBRARY for a system install.")
endif()

# --- ONNX Runtime ----------------------------------------------------------
# FERRO_ONNXRUNTIME_DIR can be overridden on the command line for a non-Homebrew
# install (e.g. a manual tarball extract on Linux/R11).
set(FERRO_ONNXRUNTIME_VENDORED OFF)
if(NOT FERRO_ONNXRUNTIME_DIR)
    # Vendored copy from scripts/fetch_onnxruntime.sh, else Homebrew on macOS.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/onnxruntime")
        set(FERRO_ONNXRUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/onnxruntime")
        set(FERRO_ONNXRUNTIME_VENDORED ON)
    elseif(APPLE AND EXISTS "/opt/homebrew/opt/onnxruntime")
        set(FERRO_ONNXRUNTIME_DIR "/opt/homebrew/opt/onnxruntime")
    endif()
endif()

if(FERRO_ONNXRUNTIME_DIR AND EXISTS "${FERRO_ONNXRUNTIME_DIR}")
    message(STATUS "ONNX Runtime: ${FERRO_ONNXRUNTIME_DIR}")

    if(FERRO_ONNXRUNTIME_VENDORED)
        # The upstream release ships a CMake config that points at include/onnxruntime
        # and lib64/, neither of which matches its own tarball, so declare the
        # imported target directly instead of trusting it.
        file(GLOB FERRO_ORT_LIB_CANDIDATES
             "${FERRO_ONNXRUNTIME_DIR}/lib/libonnxruntime.so"
             "${FERRO_ONNXRUNTIME_DIR}/lib/libonnxruntime.dylib")
        list(GET FERRO_ORT_LIB_CANDIDATES 0 FERRO_ORT_LIB)
        add_library(onnxruntime::onnxruntime SHARED IMPORTED)
        set_target_properties(onnxruntime::onnxruntime PROPERTIES
            IMPORTED_LOCATION "${FERRO_ORT_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${FERRO_ONNXRUNTIME_DIR}/include/onnxruntime")
    else()
        find_package(onnxruntime CONFIG REQUIRED PATHS "${FERRO_ONNXRUNTIME_DIR}/lib/cmake/onnxruntime" NO_DEFAULT_PATH)
    endif()

    # Link ONNX Runtime to ferro_core
    target_link_libraries(ferro_core PRIVATE onnxruntime::onnxruntime)

    add_executable(onnxruntime_smoke_test src/onnxruntime_smoke_test.cpp)
    target_link_libraries(onnxruntime_smoke_test PRIVATE onnxruntime::onnxruntime)

    set_property(TARGET ferro_core APPEND PROPERTY BUILD_RPATH "${FERRO_ONNXRUNTIME_DIR}/lib")
    set_property(TARGET onnxruntime_smoke_test APPEND PROPERTY BUILD_RPATH "${FERRO_ONNXRUNTIME_DIR}/lib")
else()
    message(FATAL_ERROR "ONNX Runtime not found. Install via Homebrew or set -DFERRA_ONNXRUNTIME_DIR=/path/to/onnxruntime")
endif()
