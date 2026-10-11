# Optional sanitizer support: cmake -DTAPEDECK_SANITIZE=address|thread|undefined
function(tapedeck_set_sanitizer target)
    if(TAPEDECK_SANITIZE STREQUAL "")
        return()
    endif()
    set(flags "-fsanitize=${TAPEDECK_SANITIZE}" -fno-omit-frame-pointer -g)
    target_compile_options(${target} PUBLIC ${flags})
    target_link_options(${target} PUBLIC ${flags})
endfunction()
