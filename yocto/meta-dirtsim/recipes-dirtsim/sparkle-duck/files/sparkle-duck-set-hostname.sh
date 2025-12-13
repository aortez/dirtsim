#!/bin/sh
# Set hostname from /boot/hostname.txt if it exists.
# This allows customizing the hostname per-device after flashing but before first boot.

HOSTNAME_FILE="/boot/hostname.txt"
DEFAULT_HOSTNAME="dirtsim"

if [ -f "${HOSTNAME_FILE}" ]; then
    NEW_HOSTNAME=$(cat "${HOSTNAME_FILE}" | tr -d '[:space:]')

    # Validate hostname (alphanumeric and hyphens only, not starting with hyphen).
    if echo "${NEW_HOSTNAME}" | grep -qE '^[a-zA-Z0-9][a-zA-Z0-9-]*$'; then
        echo "Setting hostname to: ${NEW_HOSTNAME}"
        hostnamectl set-hostname "${NEW_HOSTNAME}"
    else
        echo "Invalid hostname in ${HOSTNAME_FILE}: ${NEW_HOSTNAME}"
        echo "Using default: ${DEFAULT_HOSTNAME}"
        hostnamectl set-hostname "${DEFAULT_HOSTNAME}"
    fi
else
    echo "No ${HOSTNAME_FILE} found, keeping current hostname"
fi
