#!/bin/bash
# ESP-IDF Environment Setup
# Source this in every new terminal before building:
#   source ./setup_env.sh

source ~/.espressif/tools/activate_idf_v5.5.2.sh
export PATH="$IDF_PATH/tools:$PATH"

echo ""
echo "ESP-IDF v5.5.2 environment activated."
echo "Commands: idf.py build | idf.py flash monitor | idf.py menuconfig"
echo ""
