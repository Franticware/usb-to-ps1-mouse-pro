set(target_name usb-to-ps1-mouse-pro)
add_executable(${target_name})

pico_enable_stdio_usb(${target_name} 1)

target_sources(${target_name} PRIVATE
 main.c
 parsemouse.c
 ws2812.c
 )

# print memory usage, enable all warnings
target_link_options(${target_name} PRIVATE -Xlinker --print-memory-usage)
target_compile_options(${target_name} PRIVATE -Wall -Wextra)

target_link_libraries(${target_name} PRIVATE pico_stdlib pico_multicore pico_pio_usb)
pico_add_extra_outputs(${target_name})

########

set(target_name_kb usb-keyboard-to-ps1-controller)
add_executable(${target_name_kb})

pico_enable_stdio_usb(${target_name_kb} 1)

target_sources(${target_name_kb} PRIVATE
 main_kb.c
 ws2812.c
 )

# print memory usage, enable all warnings
target_link_options(${target_name_kb} PRIVATE -Xlinker --print-memory-usage)
target_compile_options(${target_name_kb} PRIVATE -Wall -Wextra)

target_link_libraries(${target_name_kb} PRIVATE pico_stdlib pico_multicore pico_pio_usb)
pico_add_extra_outputs(${target_name_kb})