# Unity test runner for the esptag crypto core (P1: crypto known-answer tests).
#
# Runs on the esp32s3 (the linux host target can't resolve mbedTLS's driver
# closure on IDF v6.0.1). The test drives Unity from app_main, so flashing and
# reading the serial console prints the pass/fail summary directly -- no
# interactive test menu. From this directory:
#   idf.py set-target esp32s3
#   idf.py -p <PORT> flash monitor      # or the serial-capture recipe in CLAUDE.md
cmake_minimum_required(VERSION 3.22)

# micro_ecc lives one level up, outside this project's tree.
set(EXTRA_COMPONENT_DIRS "../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esptag_crypto_test)
