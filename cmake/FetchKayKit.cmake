cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED CNCGZH_SOURCE_DIR OR NOT DEFINED CNCGZH_BINARY_DIR)
    message(FATAL_ERROR "CNCGZH_SOURCE_DIR and CNCGZH_BINARY_DIR are required")
endif()

function(fetch_zip_pack target_dir marker_name url extracted_glob source_subdir marker_text)
    set(marker "${target_dir}/${marker_name}")
    if(EXISTS "${marker}")
        return()
    endif()

    string(MD5 cache_key "${url}")
    set(cache_dir "${CNCGZH_BINARY_DIR}/_deps/kaykit-${cache_key}")
    set(zip_file "${cache_dir}/pack.zip")
    set(unpack_dir "${cache_dir}/unpacked")
    file(MAKE_DIRECTORY "${cache_dir}")

    if(NOT EXISTS "${zip_file}")
        message(STATUS "Fetching KayKit sample assets...")
        file(DOWNLOAD "${url}" "${zip_file}" TLS_VERIFY ON SHOW_PROGRESS STATUS download_status)
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

    file(GLOB extracted_roots LIST_DIRECTORIES true "${unpack_dir}/${extracted_glob}")
    list(LENGTH extracted_roots root_count)
    if(NOT root_count EQUAL 1)
        message(FATAL_ERROR "Unexpected KayKit archive layout for ${url}")
    endif()
    list(GET extracted_roots 0 extracted_root)

    set(source_dir "${extracted_root}/${source_subdir}")
    if(NOT EXISTS "${source_dir}")
        message(FATAL_ERROR "KayKit source directory was not found: ${source_dir}")
    endif()

    file(REMOVE_RECURSE "${target_dir}")
    file(MAKE_DIRECTORY "${target_dir}")
    file(COPY "${source_dir}/" DESTINATION "${target_dir}")
    file(WRITE "${marker}" "${marker_text}\n")
endfunction()

# Dungeon sample pack used for environment/props.
fetch_zip_pack(
    "${CNCGZH_SOURCE_DIR}/assets/sample/kaykit-dungeon"
    ".cncgzh-kaykit-ready"
    "https://github.com/KayKit-Game-Assets/KayKit-Dungeon-Remastered-1.0/archive/b0ca9bd96a8072ab36a3a5464f00ed1e06a16d07.zip"
    "KayKit-Dungeon-Remastered-1.0-*"
    "addons/kaykit_dungeon_remastered/Assets"
    "KayKit Dungeon Remastered b0ca9bd96a8072ab36a3a5464f00ed1e06a16d07"
)

# Exact character-pack versions supplied for the engine test project. The mirror is
# pinned to one immutable commit and contains the original CC0 KayKit pack layout.
set(character_commit "af08a62d3669370ec4636ae6314b38cdcd5dd759")
set(character_url "https://github.com/GeorgeQLe/assets-kaykit-3d-characters/archive/${character_commit}.zip")
set(character_cache "${CNCGZH_BINARY_DIR}/_deps/kaykit-characters-${character_commit}")
set(character_zip "${character_cache}/characters.zip")
set(character_unpack "${character_cache}/unpacked")
set(character_root "${CNCGZH_SOURCE_DIR}/assets/sample/kaykit-characters")
set(character_marker "${character_root}/.cncgzh-characters-ready")

if(NOT EXISTS "${character_marker}" OR NOT EXISTS "${character_root}/character-animations-1.1")
    file(MAKE_DIRECTORY "${character_cache}")
    if(NOT EXISTS "${character_zip}")
        message(STATUS "Fetching KayKit Adventurers 2.0, Skeletons 1.1 and Character Animations 1.1...")
        file(DOWNLOAD "${character_url}" "${character_zip}" TLS_VERIFY ON SHOW_PROGRESS STATUS download_status)
        list(GET download_status 0 download_code)
        list(GET download_status 1 download_message)
        if(NOT download_code EQUAL 0)
            file(REMOVE "${character_zip}")
            message(FATAL_ERROR "Could not fetch KayKit character packs: ${download_message}")
        endif()
    endif()

    file(REMOVE_RECURSE "${character_unpack}")
    file(MAKE_DIRECTORY "${character_unpack}")
    file(ARCHIVE_EXTRACT INPUT "${character_zip}" DESTINATION "${character_unpack}")
    file(GLOB roots LIST_DIRECTORIES true "${character_unpack}/assets-kaykit-3d-characters-*")
    list(LENGTH roots root_count)
    if(NOT root_count EQUAL 1)
        message(FATAL_ERROR "Unexpected KayKit character archive layout")
    endif()
    list(GET roots 0 extracted_root)

    set(pack_root "${extracted_root}/assets/kaykit")
    if(NOT EXISTS "${pack_root}/adventurers-2.0" OR NOT EXISTS "${pack_root}/skeletons-1.1" OR NOT EXISTS "${pack_root}/character-animations-1.1")
        message(FATAL_ERROR "Required KayKit character or animation packs were not found in the character archive")
    endif()

    file(REMOVE_RECURSE "${character_root}")
    file(MAKE_DIRECTORY "${character_root}")
    file(COPY "${pack_root}/adventurers-2.0" DESTINATION "${character_root}")
    file(COPY "${pack_root}/skeletons-1.1" DESTINATION "${character_root}")
    file(COPY "${pack_root}/character-animations-1.1" DESTINATION "${character_root}")
    if(EXISTS "${extracted_root}/LICENSES/KayKit-CC0-License.txt")
        file(COPY "${extracted_root}/LICENSES/KayKit-CC0-License.txt" DESTINATION "${character_root}")
    endif()
    file(WRITE "${character_marker}" "KayKit Adventurers 2.0 + Skeletons 1.1 + Character Animations 1.1, source commit ${character_commit}\n")
endif()

message(STATUS "KayKit sample assets ready")
