#!/bin/sh
# Detect Pi model and configure display settings for dirtsim-ui.
# Creates /etc/dirtsim/display.conf with appropriate environment variables.

CONFIG_DIR="/etc/dirtsim"
CONFIG_FILE="${CONFIG_DIR}/display.conf"

# Create config directory if needed.
mkdir -p "${CONFIG_DIR}"

# Read Pi model from device tree.
MODEL=$(cat /proc/device-tree/model 2>/dev/null | tr -d '\0')

case "${MODEL}" in
    *"Pi 4"*|*"Pi4"*)
        # Pi4 with MPI4008 4-inch HDMI touchscreen.
        # Display is 480x800 portrait, rotated 90 degrees for landscape.
        cat > "${CONFIG_FILE}" << 'EOF'
# Pi4 with MPI4008 display.
LV_DISPLAY_ROTATION=90
LV_EVDEV_DEVICE=/dev/input/touchscreen0
EOF
        echo "Configured for Pi4 with MPI4008 display"
        ;;
    *"Pi 5"*|*"Pi5"*)
        # Pi5 with HyperPixel 4.0 DPI display.
        # Display is 480x800 portrait, rotated 270 degrees for landscape.
        cat > "${CONFIG_FILE}" << 'EOF'
# Pi5 with HyperPixel 4.0 display.
LV_DISPLAY_ROTATION=270
LV_EVDEV_DEVICE=/dev/input/by-path/platform-i2c@0-event
EOF
        echo "Configured for Pi5 with HyperPixel display"
        ;;
    *)
        # Unknown model - use Pi5 defaults.
        echo "Unknown model: ${MODEL} - using Pi5 defaults"
        cat > "${CONFIG_FILE}" << 'EOF'
# Unknown model - using Pi5 defaults.
LV_DISPLAY_ROTATION=270
LV_EVDEV_DEVICE=/dev/input/by-path/platform-i2c@0-event
EOF
        ;;
esac

chmod 644 "${CONFIG_FILE}"
