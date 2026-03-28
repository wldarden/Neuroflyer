if(NOT TARGET project_warnings)
    add_library(project_warnings INTERFACE)
    target_compile_options(project_warnings INTERFACE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:
            -Wall -Wextra -Wpedantic -Werror
        >
    )
endif()
