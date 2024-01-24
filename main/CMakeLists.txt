
idf_component_register(SRCS "utils.cpp" "Script.cpp" "Signal.cpp" "indication.cpp" "TmpNet.cpp" "MeshNet.cpp" "FirmwareUpdate.cpp" "work/work.cpp" "thread_ctl.cpp" "uart.cpp" "http.cpp" "main.cpp" "can.cpp" "mesh.cpp" 
"twai_filter.cpp" "dtp_message.cpp"
                       INCLUDE_DIRS ".")

# target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++17)
 