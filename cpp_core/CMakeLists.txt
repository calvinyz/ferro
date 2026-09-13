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

    add_executable(ferro_eval src/eval.cpp)
    target_include_directories(ferro_eval PRIVATE "${MUJOCO_INCLUDE_DIR}")
    target_link_libraries(ferro_eval PRIVATE "${MUJOCO_LIBRARY}" onnxruntime::onnxruntime)
    target_compile_definitions(ferro_eval PRIVATE
        INVERTED_PENDULUM_PATH="${INVERTED_PENDULUM_PATH}"
        POLICY_PATH="${POLICY_PATH}"
    )

    set_property(TARGET ferro_core APPEND PROPERTY BUILD_RPATH "${FERRO_ONNXRUNTIME_DIR}/lib")
    set_property(TARGET onnxruntime_smoke_test APPEND PROPERTY BUILD_RPATH "${FERRO_ONNXRUNTIME_DIR}/lib")
    set_property(TARGET ferro_eval APPEND PROPERTY BUILD_RPATH "${FERRO_ONNXRUNTIME_DIR}/lib")
    if(APPLE)
        set_property(TARGET ferro_eval APPEND PROPERTY BUILD_RPATH "${FERRO_MUJOCO_LIBDIR}")
    endif()
else()
    message(FATAL_ERROR "ONNX Runtime not found. Install via Homebrew or set -DFERRA_ONNXRUNTIME_DIR=/path/to/onnxruntime")
endif()

# --- Rust sidecar ----------------------------------------------------------
# Builds the Rust crate as a staticlib and links it in. cargo is often not on
# PATH in a GUI-launched build, so search the usual install locations too.
find_program(CARGO_EXECUTABLE cargo
    HINTS "$ENV{HOME}/.cargo/bin" "/opt/homebrew/opt/rustup/bin")

if(CARGO_EXECUTABLE)
    set(FERRO_RUST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../rust_sidecar")

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(FERRO_CARGO_FLAGS "")
        set(FERRO_CARGO_PROFILE debug)
    else()
        set(FERRO_CARGO_FLAGS --release)
        set(FERRO_CARGO_PROFILE release)
    endif()

    set(FERRO_RUST_LIB "${FERRO_RUST_DIR}/target/${FERRO_CARGO_PROFILE}/libferro_sidecar.a")
    file(GLOB FERRO_RUST_SOURCES "${FERRO_RUST_DIR}/src/*.rs")

    # Finding cargo is not enough: the rustup proxy shells out to rustc, so its
    # own directory has to be on PATH or the build fails with "could not
    # execute process `rustc -vV`".
    get_filename_component(FERRO_CARGO_DIR "${CARGO_EXECUTABLE}" DIRECTORY)

    add_custom_command(
        OUTPUT "${FERRO_RUST_LIB}"
        COMMAND "${CMAKE_COMMAND}" -E env "PATH=${FERRO_CARGO_DIR}:$ENV{PATH}"
                "${CARGO_EXECUTABLE}" build ${FERRO_CARGO_FLAGS}
        WORKING_DIRECTORY "${FERRO_RUST_DIR}"
        DEPENDS ${FERRO_RUST_SOURCES} "${FERRO_RUST_DIR}/Cargo.toml"
        COMMENT "Building Rust sidecar staticlib"
        VERBATIM)
    add_custom_target(rust_sidecar_build DEPENDS "${FERRO_RUST_LIB}")

    # Native libs the staticlib needs, per `rustc --print native-static-libs`.
    # macOS reports only -lSystem -lc -lm, all implicit; Linux needs the rest.
    if(NOT APPLE AND UNIX)
        set(FERRO_RUST_SYS_LIBS gcc_s util rt pthread m dl)
    endif()

    find_package(Threads REQUIRED)

    # ferro_core runs the consumer thread, so it needs the staticlib too.
    add_dependencies(ferro_core rust_sidecar_build)
    target_link_libraries(ferro_core PRIVATE
        "${FERRO_RUST_LIB}" ${FERRO_RUST_SYS_LIBS} Threads::Threads)

    add_executable(sidecar_link_test src/sidecar_link_test.cpp)
    add_dependencies(sidecar_link_test rust_sidecar_build)
    target_link_libraries(sidecar_link_test PRIVATE "${FERRO_RUST_LIB}" ${FERRO_RUST_SYS_LIBS})

    add_executable(target_channel_test src/target_channel_test.cpp)
    target_link_libraries(target_channel_test PRIVATE Threads::Threads)

    add_executable(ring_stress_test src/ring_stress_test.cpp)
    add_dependencies(ring_stress_test rust_sidecar_build)
    target_link_libraries(ring_stress_test PRIVATE
        "${FERRO_RUST_LIB}" ${FERRO_RUST_SYS_LIBS} Threads::Threads)

    message(STATUS "Rust sidecar: ${CARGO_EXECUTABLE} (${FERRO_CARGO_PROFILE})")
else()
    message(WARNING "cargo not found, skipping Rust sidecar targets")
endif()
