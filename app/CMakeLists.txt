cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(cyw55500_sdio_test)

# Step 1: Basic SDIO test - just main.c
target_sources(app PRIVATE
    src/main.c
)
