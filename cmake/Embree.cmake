option(AKARI_USE_EMBREE "Use Embree as ray intersection backend" ON)
option(AKARI_BUILD_EMBREE "Compile embree from source" OFF)

if(AKARI_USE_EMBREE)
    if(AKARI_BUILD_EMBREE)
        set(EMBREE_ISPC_SUPPORT           OFF CACHE BOOL " " FORCE)
        set(EMBREE_TUTORIALS              OFF CACHE BOOL " " FORCE)
        set(EMBREE_FILTER_FUNCTION        OFF CACHE BOOL " " FORCE)
        set(EMBREE_IGNORE_CMAKE_CXX_FLAGS OFF CACHE BOOL " " FORCE)
        set(EMBREE_GEOMETRY_QUAD          OFF CACHE BOOL " " FORCE)
        set(EMBREE_GEOMETRY_CURVE         OFF CACHE BOOL " " FORCE)
        set(EMBREE_GEOMETRY_GRID          OFF CACHE BOOL " " FORCE)
        set(EMBREE_GEOMETRY_SUBDIVISION   OFF CACHE BOOL " " FORCE)
        set(EMBREE_GEOMETRY_INSTANCE      OFF CACHE BOOL " " FORCE)
        set(EMBREE_GEOMETRY_USER          OFF CACHE BOOL " " FORCE)
        set(EMBREE_MAX_ISA NONE CACHE STRING "" FORCE)
        set(EMBREE_ISA_AVX2 ON CACHE BOOL "" FORCE)
        set(EMBREE_GEOMETRY_QUAD OFF CACHE BOOL "" FORCE)
        set(EMBREE_GEOMETRY_CURVE OFF CACHE BOOL "" FORCE)
        set(EMBREE_GEOMETRY_USER OFF CACHE BOOL "" FORCE)
        set(EMBREE_GEOMETRY_POINT OFF CACHE BOOL "" FORCE)
        set(EMBREE_TASKING_SYSTEM INTERNAL CACHE STRING "" FORCE)
        include_directories(external/embree-3.8.0/include)
        add_subdirectory(external/embree-3.8.0)
        add_compile_definitions(AKARI_USE_EMBREE)
    else()
        IF(WIN32)
            set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} "C:/Program Files/Intel/Embree3 x64")
        ELSE()
            set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} "/usr/lib64/cmake/")
        ENDIF()
        find_package(embree 3.8)
        IF(NOT embree_FOUND)
            message("Embree 3.8 not found; Falling back to custom BVH backend")
        ELSE()
            message("Embree found " ${EMBREE_INCLUDE_DIRS})
            add_compile_definitions(AKARI_USE_EMBREE)
            include_directories(${EMBREE_INCLUDE_DIRS})
        ENDIF()
    endif()

endif()