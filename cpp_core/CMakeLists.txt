add_executable(ferro_core src/main.cpp)

# --- MuJoCo ---------------------------------------------------------------
# MUJOCO_INCLUDE_DIR / MUJOCO_LIBRARY can be overridden on the command line
# (e.g. -DMUJOCO_LIBRARY=/path/to/libmujoco.so on Linux/R11).
if(APPLE AND NOT MUJOCO_INCLUDE_DIR)
    set(MUJOCO_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mujoco_include")
endif()
if(APPLE AND NOT MUJOCO_LIBRARY)
    set(MUJOCO_LIBRARY
        "/Applications/MuJoCo.app/Contents/Frameworks/mujoco.framework/Versions/A/libmujoco.3.12.0.dylib")
endif()

if(MUJOCO_INCLUDE_DIR AND MUJOCO_LIBRARY AND EXISTS "${MUJOCO_LIBRARY}")
    add_executable(mujoco_smoke_test src/mujoco_smoke_test.cpp)
    target_include_directories(mujoco_smoke_test PRIVATE "${MUJOCO_INCLUDE_DIR}")
    target_link_libraries(mujoco_smoke_test PRIVATE "${MUJOCO_LIBRARY}")
else()
    message(WARNING "MuJoCo not found (MUJOCO_INCLUDE_DIR/MUJOCO_LIBRARY unset) - skipping mujoco_smoke_test target")
endif()
