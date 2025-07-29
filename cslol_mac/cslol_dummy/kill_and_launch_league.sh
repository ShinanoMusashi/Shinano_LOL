#!/bin/bash
# kill_and_launch_league.sh

# Set environment variables
export DYLD_INSERT_LIBRARIES="/Users/user/Documents/lol-skins-main/skins/cslol_mac/cslol_dummy/libcslol_minimal_test.dylib"
export CSLOL_MOD_PATH="/Users/user/Library/Application Support/moonshadow565/customskinlol-manager/profiles/Default Profile/"

# Kill existing processes
echo "Killing all League and Riot processes..."
pkill -f "League"
pkill -f "Riot"
sleep 5

# Launch Riot Client with environment variables
echo "Launching Riot Client with environment variables..."
echo "DYLD_INSERT_LIBRARIES: $DYLD_INSERT_LIBRARIES"
echo "CSLOL_MOD_PATH: $CSLOL_MOD_PATH"

"/Users/Shared/Riot Games/Riot Client.app/Contents/MacOS/RiotClientServices" --launch-product=league_of_legends --launch-patchline=live &

echo "Waiting for League to start..."
sleep 10

# Check if injection worked
echo "Checking for League processes..."
ps aux | grep -i league | grep -v grep

echo "Checking if dylib is loaded..."
LEAGUE_PID=$(pgrep -f "LeagueofLegends" | head -1)
if [ ! -z "$LEAGUE_PID" ]; then
    echo "League PID: $LEAGUE_PID"
    lsof -p $LEAGUE_PID | grep cslol || echo "No cslol dylib found"
    cat /tmp/cslol_minimal_test.log 2>/dev/null || echo "No test log found"
else
    echo "No League process found"
fi
