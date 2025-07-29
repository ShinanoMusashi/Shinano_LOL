#!/bin/bash

# Set environment variables
export DYLD_INSERT_LIBRARIES="/Users/user/Documents/lol-skins-main/skins/cslol_mac/cslol-manager/build/cslol-manager.app/Contents/MacOS/cslol-tools/libcslol_interception.dylib"
export CSLOL_MOD_PATH="/Users/user/Library/Application Support/moonshadow565/customskinlol-manager/profiles/Default Profile/"

# Kill existing processes completely
echo "Killing all League and Riot processes..."
pkill -f "League"
pkill -f "Riot"
sleep 5

# Launch Riot Client with environment variables
echo "Launching Riot Client with environment variables..."
echo "DYLD_INSERT_LIBRARIES: $DYLD_INSERT_LIBRARIES"
echo "CSLOL_MOD_PATH: $CSLOL_MOD_PATH"

# Launch through the Riot Client directly
"/Users/Shared/Riot Games/Riot Client.app/Contents/MacOS/RiotClientServices" --launch-product=league_of_legends --launch-patchline=live &

echo "Riot Client launched. Wait for League to fully load, then start a game."
echo "The environment variables should be inherited by the game process."

# Wait and monitor
sleep 10
echo "Checking for League processes..."
ps aux | grep -i league | grep -v grep
