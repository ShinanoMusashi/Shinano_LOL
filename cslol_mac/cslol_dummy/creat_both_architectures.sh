#!/bin/bash
# Create dylibs for both architectures with debug symbols

echo "Creating dylibs for both ARM64 and x86_64 with debug symbols..."

# Compile ARM64 version (for native Mac processes)
echo "Compiling ARM64 version..."
gcc -g -arch arm64 -shared -fPIC -o libcslol_minimal_test_arm64.dylib minimal_cslol_test.c

# Compile x86_64 version (for League under Rosetta)
echo "Compiling x86_64 version..."
gcc -g -arch x86_64 -shared -fPIC -o libcslol_minimal_test_x86.dylib minimal_cslol_test.c

# Create universal binary (contains both architectures)
echo "Creating universal binary..."
lipo -create libcslol_minimal_test_arm64.dylib libcslol_minimal_test_x86.dylib -output libcslol_minimal_test_universal.dylib

# Compile test targets with debug
gcc -g -arch arm64 -o test_target test_target.c
gcc -g -arch x86_64 -o test_target_x86 test_target.c

echo ""
echo "=== Architecture Check ==="
file libcslol_minimal_test_arm64.dylib
file libcslol_minimal_test_x86.dylib
file libcslol_minimal_test_universal.dylib
file test_target
file test_target_x86

echo "x86_64 version:"
file libcslol_minimal_test_x86.dylib

echo "Universal version:"
file libcslol_minimal_test_universal.dylib

echo ""
echo "=== Testing with ARM64 target ==="
# Clear old log
rm -f /tmp/cslol_minimal_test.log

# Test with ARM64 dylib and native test_target
if [ -f test_target ]; then
    echo "Testing ARM64 dylib with native test_target..."
    DYLD_INSERT_LIBRARIES="./libcslol_minimal_test_arm64.dylib" ./test_target &
    TEST_PID=$!
    sleep 2
    kill $TEST_PID 2>/dev/null
    
    if [ -f /tmp/cslol_minimal_test.log ]; then
        echo "✅ ARM64 injection succeeded!"
        cat /tmp/cslol_minimal_test.log
    else
        echo "❌ ARM64 injection failed"
    fi
else
    echo "No test_target found"
fi

echo ""
echo "=== Testing with x86_64 target ==="
# Clear old log
rm -f /tmp/cslol_minimal_test.log

# Test with x86_64 dylib and x86_64 test_target
if [ -f test_target_x86 ]; then
    echo "Testing x86_64 dylib with x86_64 test_target..."
    DYLD_INSERT_LIBRARIES="./libcslol_minimal_test_x86.dylib" ./test_target_x86 &
    TEST_PID=$!
    sleep 2
    kill $TEST_PID 2>/dev/null
    
    if [ -f /tmp/cslol_minimal_test.log ]; then
        echo "✅ x86_64 injection succeeded!"
        cat /tmp/cslol_minimal_test.log
    else
        echo "❌ x86_64 injection failed"
    fi
else
    echo "No test_target_x86 found - creating one..."
    gcc -arch x86_64 -o test_target_x86 test_target.c
    echo "Testing x86_64 dylib with x86_64 test_target..."
    DYLD_INSERT_LIBRARIES="./libcslol_minimal_test_x86.dylib" ./test_target_x86 &
    TEST_PID=$!
    sleep 2
    kill $TEST_PID 2>/dev/null
    
    if [ -f /tmp/cslol_minimal_test.log ]; then
        echo "✅ x86_64 injection succeeded!"
        cat /tmp/cslol_minimal_test.log
    else
        echo "❌ x86_64 injection failed"
    fi
fi

echo ""
echo "=== Testing Universal Binary ==="
# Clear old log
rm -f /tmp/cslol_minimal_test.log

echo "Testing universal dylib with native test_target..."
DYLD_INSERT_LIBRARIES="./libcslol_minimal_test_universal.dylib" ./test_target &
TEST_PID=$!
sleep 2
kill $TEST_PID 2>/dev/null

if [ -f /tmp/cslol_minimal_test.log ]; then
    echo "✅ Universal binary injection succeeded!"
    cat /tmp/cslol_minimal_test.log
else
    echo "❌ Universal binary injection failed"
fi

echo ""
echo "=== Summary ==="
echo "Now you can test with League using the x86_64 version:"
echo "DYLD_INSERT_LIBRARIES=\"./libcslol_minimal_test_x86.dylib\" [launch League]"
echo ""
echo "Files created:"
ls -la libcslol_minimal_test*.dylib
