string(LENGTH "${CURRENT_BUILDTREES_DIR}" buildtrees_path_length)
if(buildtrees_path_length GREATER 32 AND CMAKE_HOST_WIN32)
    set(FALLBACK_BUILDTREES_DIR "C:/qt-vcpkg" CACHE PATH "Fallback build directory if current is too long.")

    message(WARNING "${PORT}'s buildsystem uses very long paths and may fail on your system.\n" "Build tree is ${FALLBACK_BUILDTREES_DIR} now.")
    set(CURRENT_BUILDTREES_DIR ${FALLBACK_BUILDTREES_DIR})
endif()

execute_process(
    COMMAND cmd /c "echo %ProgramFiles(x86)%"
    OUTPUT_VARIABLE PROGRAMFILES_X86
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(VSWHERE_EXE "${PROGRAMFILES_X86}/Microsoft Visual Studio/Installer/vswhere.exe")

execute_process(
    COMMAND "${VSWHERE_EXE}" -latest -products * -property installationPath
    OUTPUT_VARIABLE VS_PATH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

file(TO_CMAKE_PATH "${VS_PATH}" VS_PATH)
message(STATUS "VS_PATH: ${VS_PATH}")

set(DEV_CMD "${VS_PATH}/Common7/Tools/VsDevCmd.bat")
message(STATUS "DEV_CMD: ${DEV_CMD}")

vcpkg_download_distfile(ARCHIVE
    URLS "https://hex-rays.com/hubfs/freefile/qt-5.15.2-full-IDA83.tar.bz2"
    FILENAME "qt-5.15.2-full-IDA83.tar.bz2"
    SHA512 0f752da52216fc527464c667733e13204eae5975215045706a8288ede7ea1243d2ff13410541c4b5abefcb95d1b6d2366ad1f18087399ecf21229387bec7de2f
)

vcpkg_extract_source_archive(
    SOURCE_PATH
    ARCHIVE "${ARCHIVE}"
)

file(RENAME "${SOURCE_PATH}/build/build.py" "${SOURCE_PATH}/build/build_orig.py")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/build.py" DESTINATION "${SOURCE_PATH}/build")

vcpkg_find_acquire_program(PYTHON3)
get_filename_component(PYTHON_PATH "${PYTHON3}" DIRECTORY)
vcpkg_add_to_path("${PYTHON_PATH}")

if(VCPKG_TARGET_IS_WINDOWS)
    vcpkg_find_acquire_program(PERL)
    get_filename_component(PERL_PATH "${PERL}" DIRECTORY)
    vcpkg_add_to_path("${PERL_PATH}")

    vcpkg_find_acquire_program(JOM)
    get_filename_component(JOM_PATH "${JOM}" DIRECTORY)
    vcpkg_add_to_path("${JOM_PATH}")
endif()

file(WRITE "${SOURCE_PATH}/build/build.bat"
    "@echo off\n"
    "call \"${DEV_CMD}\"\n"
    "python build.py -j ${VCPKG_CONCURRENCY} -v\n")

vcpkg_execute_build_process(
    COMMAND cmd /c build.bat
    WORKING_DIRECTORY "${SOURCE_PATH}/build"
    LOGNAME  qt-ida-build
)

file(REMOVE_RECURSE "${CURRENT_BUILDTREES_DIR}")