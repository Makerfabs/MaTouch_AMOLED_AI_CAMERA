set(ESP_DL_DIR "${CMAKE_CURRENT_LIST_DIR}/../esp-dl")

if(NOT EXISTS "${ESP_DL_DIR}/include/model_zoo/cat_face_detect_mn03.hpp")
    message(FATAL_ERROR "ESP-DL not found. Put ESP-DL 1.1.0 at ${ESP_DL_DIR}")
endif()

idf_component_register(SRCS "main.c"
                           "drivers/co5300_driver.c"
                           "drivers/camera_driver.c"
                           "app/camera_lcd_app.c"
                           "app/ai_engine.cpp"
                           "app/detection_result.c"
                    INCLUDE_DIRS "." "drivers" "app"
                                 "${ESP_DL_DIR}/include"
                                 "${ESP_DL_DIR}/include/tool"
                                 "${ESP_DL_DIR}/include/nn"
                                 "${ESP_DL_DIR}/include/layer"
                                 "${ESP_DL_DIR}/include/image"
                                 "${ESP_DL_DIR}/include/typedef"
                                 "${ESP_DL_DIR}/include/detect"
                                 "${ESP_DL_DIR}/include/model_zoo"
                    REQUIRES esp_timer)

idf_build_get_property(target IDF_TARGET)

target_link_libraries(${COMPONENT_LIB}
    "-L ${ESP_DL_DIR}/lib/${target}"
    libcat_face_detect.a
    libdl.a
)

