#!/bin/sh
# DirtSim configuration setup - runs once at boot before services start.
# Ensures proper permissions for config files and directories.

# Fix permissions on .local config overrides so dirtsim user can read them.
if ls /etc/dirtsim/*.local >/dev/null 2>&1; then
    chmod 644 /etc/dirtsim/*.local
    echo "Fixed permissions on /etc/dirtsim/*.local"
fi

# Ensure dirtsim user owns their home directory.
if [ -d /home/dirtsim ]; then
    chown -R dirtsim:dirtsim /home/dirtsim
    echo "Set ownership on /home/dirtsim"
fi

# Ensure dirtsim user owns their working directory.
if [ -d /data/dirtsim ]; then
    chown -R dirtsim:dirtsim /data/dirtsim
    echo "Set ownership on /data/dirtsim"
fi

exit 0
