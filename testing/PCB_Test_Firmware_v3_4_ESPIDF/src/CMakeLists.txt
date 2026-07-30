FILE(GLOB_RECURSE app_sources
	"${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/*.c")

idf_component_register(SRCS ${app_sources}
	INCLUDE_DIRS "." "hal" "ble"
	REQUIRES driver esp_adc esp_timer nvs_flash bt)
