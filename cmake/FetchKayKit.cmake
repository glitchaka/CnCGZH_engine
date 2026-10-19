cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED CNCGZH_SOURCE_DIR OR NOT DEFINED CNCGZH_BINARY_DIR)
    message(FATAL_ERROR "CNCGZH_SOURCE_DIR and CNCGZH_BINARY_DIR are required")
endif()

set(asset_root "${CNCGZH_BINARY_DIR}/assets/sample/kaykit-dungeon")
set(marker "${asset_root}/.cncgzh-kaykit-ready")
set(project_root "${CNCGZH_BINARY_DIR}/projects")

# Keep the sample project beside the build resources so Studio/Runtime work
# regardless of whether the executable is in Debug/Release underneath it.
file(REMOVE_RECURSE "${project_root}")
file(COPY "${CNCGZH_SOURCE_DIR}/projects/" DESTINATION "${project_root}")

if(EXISTS "${marker}")
    return()
endif()

set(cache_dir "${CNCGZH_BINARY_DIR}/_deps/kaykit-dungeon")
set(zip_file "${cache_dir}/kaykit-dungeon.zip")
set(unpack_dir "${cache_dir}/unpacked")
set(url "https://github.com/KayKit-Game-Assets/KayKit-Dungeon-Remastered-1.0/archive/b0ca9bd96a8072ab36a3a5464f00ed1e06a16d07.zip")

file(MAKE_DIRECTORY "${cache_dir}")

if(NOT EXISTS "${zip_file}")
    message(STATUS "Fetching bundled KayKit Dungeon Remastered assets...")
    file(DOWNLOAD
        "${url}"
        "${zip_file}"
        TLS_VERIFY ON
        SHOW_PROGRESS
        STATUS download_status
    )
    list(GET download_status 0 download_code)
    list(GET download_status 1 download_message)
    if(NOT download_code EQUAL 0)
        file(REMOVE "${zip_file}")
        message(FATAL_ERROR "Could not fetch KayKit assets: ${download_message}")
    endif()
endif()

file(REMOVE_RECURSE "${unpack_dir}")
file(MAKE_DIRECTORY "${unpack_dir}")
file(ARCHIVE_EXTRACT INPUT "${zip_file}" DESTINATION "${unpack_dir}")

file(GLOB extracted_roots LIST_DIRECTORIES true "${unpack_dir}/KayKit-Dungeon-Remastered-1.0-*")
list(LENGTH extracted_roots root_count)
if(NOT root_count EQUAL 1)
    message(FATAL_ERROR "Unexpected KayKit archive layout")
endif()
list(GET extracted_roots 0 extracted_root)

set(kaykit_assets "${extracted_root}/addons/kaykit_dungeon_remastered/Assets")
if(NOT EXISTS "${kaykit_assets}")
    message(FATAL_ERROR "KayKit Assets directory was not found after extraction")
endif()

file(REMOVE_RECURSE "${asset_root}")
file(MAKE_DIRECTORY "${asset_root}")
file(COPY "${kaykit_assets}/" DESTINATION "${asset_root}")
file(WRITE "${marker}" "KayKit Dungeon Remastered b0ca9bd96a8072ab36a3a5464f00ed1e06a16d07\n")

message(STATUS "KayKit assets ready at ${asset_root}")
