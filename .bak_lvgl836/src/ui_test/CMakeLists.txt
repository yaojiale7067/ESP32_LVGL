SET(SOURCES screens/ui_Screen1.c
    screens/ui_SerialTool.c
    screens/ui_dht11.c
    screens/ui_file.c
    screens/ui_wifi.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c)

add_library(ui ${SOURCES})
