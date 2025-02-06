cmake_minimum_required(VERSION 3.16)


include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(EXTRA_COMPONENT_DIRS ./esp-idf-lib/components)
project(esp32-bme680-prometheus)
