idf_component_register(SRCS
                        "main.c"
                        "wifi/connect.c"
                        "wifi/wifi.c"
                        "mqtt/mqtt.c"
                        "sling/sling_sinter.c"
                        "sinter/sinter_task.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES
                        mqtt/mqtt_client_cert.pem mqtt/mqtt_client_key.pem mqtt/mqtt_server_cert.pem)

# Build static library, do not build test executables
option(BUILD_SHARED_LIBS OFF)
option(BUILD_TESTING OFF)

# Unfortunately the library performs install and export. Would
# have been nice if devs made that an option like BUILD_SHARED_LIBS
# and BUILD_TESTING. Override install() and export() to do nothing
# instead.
function(install)
endfunction()

function(export)
endfunction()

# Removes the specified compile flag from the specified target.
#   _target     - The target to remove the compile flag from
#   _flag       - The compile flag to remove
macro(remove_flag_from_target _target _flag)
    get_target_property(_target_cxx_flags ${_target} COMPILE_OPTIONS)
    if(_target_cxx_flags)
        list(REMOVE_ITEM _target_cxx_flags ${_flag})
        set_target_properties(${_target} PROPERTIES COMPILE_OPTIONS "${_target_cxx_flags}")
    endif()
endmacro()

# import libsinter, and remove the -pedantic flag
# TODO feels pretty hacky...
add_subdirectory(lib/sinter/vm libsinter)
remove_flag_from_target(sinter -pedantic)

# link sinter
target_link_libraries(${COMPONENT_LIB} PUBLIC sinter)
