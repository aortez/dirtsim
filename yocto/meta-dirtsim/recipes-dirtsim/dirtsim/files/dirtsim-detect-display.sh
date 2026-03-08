#!/bin/sh
# Detect connected display hardware and configure dirtsim-ui.
# Creates /etc/dirtsim/display.conf with appropriate environment variables.
#
# Detects by scanning DRM connectors for a connected display, then
# configures the DRM device, rotation, and touchscreen input accordingly.

CONFIG_DIR="/etc/dirtsim"
CONFIG_FILE="${CONFIG_DIR}/display.conf"

mkdir -p "${CONFIG_DIR}"

# Find the first connected DRM connector (skip Writeback).
CONNECTED_CONNECTOR=""
DRM_CARD=""
for status_file in /sys/class/drm/card*-*/status; do
    connector=$(dirname "$status_file" | xargs basename)
    case "$connector" in *Writeback*) continue ;; esac
    if [ "$(cat "$status_file")" = "connected" ]; then
        CONNECTED_CONNECTOR="$connector"
        DRM_CARD=$(echo "$connector" | sed 's/-.*//')
        break
    fi
done

if [ -z "$CONNECTED_CONNECTOR" ]; then
    echo "No connected display found, using defaults"
    cat > "${CONFIG_FILE}" << 'EOF'
# No connected display detected. Using DPI defaults.
LV_LINUX_DRM_DEVICE=/dev/dri/card2
LV_DISPLAY_ROTATION=270
LV_EVDEV_DEVICE=/dev/input/by-path/platform-i2c@0-event
EOF
    chmod 644 "${CONFIG_FILE}"
    exit 0
fi

DRM_DEVICE="/dev/dri/${DRM_CARD}"

# Determine display type from connector name.
case "$CONNECTED_CONNECTOR" in
    *HDMI*)
        # HDMI display (e.g. MPI4008 4-inch touchscreen).
        # Physical panel is 480x800 portrait, rotated 90 for landscape.
        DISPLAY_TYPE="HDMI"
        ROTATION=90
        ;;
    *DPI*)
        # DPI display (e.g. HyperPixel 4.0).
        # Physical panel is 480x800 portrait, rotated 270 for landscape.
        DISPLAY_TYPE="DPI"
        ROTATION=270
        ;;
    *)
        DISPLAY_TYPE="unknown"
        ROTATION=0
        echo "Unknown connector type: $CONNECTED_CONNECTOR"
        ;;
esac

# Find touchscreen input device.
TOUCH_DEVICE=""
if [ -e /dev/input/touchscreen0 ]; then
    # ADS7846 resistive touchscreen (MPI4008).
    TOUCH_DEVICE="/dev/input/touchscreen0"
elif [ -e /dev/input/by-path/platform-i2c@0-event ]; then
    # Goodix capacitive touchscreen (HyperPixel 4.0).
    TOUCH_DEVICE="/dev/input/by-path/platform-i2c@0-event"
else
    TOUCH_DEVICE="/dev/input/event0"
    echo "No known touchscreen found, falling back to ${TOUCH_DEVICE}"
fi

cat > "${CONFIG_FILE}" << EOF
# Auto-detected: ${CONNECTED_CONNECTOR} (${DISPLAY_TYPE}).
LV_LINUX_DRM_DEVICE=${DRM_DEVICE}
LV_DISPLAY_ROTATION=${ROTATION}
LV_EVDEV_DEVICE=${TOUCH_DEVICE}
EOF

chmod 644 "${CONFIG_FILE}"
echo "Configured for ${DISPLAY_TYPE} display on ${DRM_DEVICE} (rotation: ${ROTATION}, touch: ${TOUCH_DEVICE})"
