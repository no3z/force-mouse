#!/bin/bash
# Test script to identify mouse buttons
# Copy this to your Force and run it

echo "========================================"
echo "Mouse Button Tester"
echo "========================================"
echo ""
echo "This script will show you which buttons your mouse has."
echo "Press CTRL+C to exit when done."
echo ""
echo "Press different buttons on your mouse..."
echo ""

# Replace event2 with your mouse device
MOUSE_DEVICE="/dev/input/event2"

if [ ! -e "$MOUSE_DEVICE" ]; then
    echo "Error: $MOUSE_DEVICE not found"
    echo "Available input devices:"
    ls -la /dev/input/event*
    exit 1
fi

# Monitor mouse events and show button codes
hexdump -v -e '1/8 "%ld.%06ld " 2/2 "type=%d code=%d " 1/4 "value=%d\n"' "$MOUSE_DEVICE" | \
    grep "type=1" | \
    while read line; do
        # Extract button code
        code=$(echo "$line" | sed -n 's/.*code=\([0-9]*\).*/\1/p')
        value=$(echo "$line" | sed -n 's/.*value=\([0-9]*\).*/\1/p')

        # Map codes to button names
        case $code in
            272) button="BTN_LEFT" ;;
            273) button="BTN_RIGHT" ;;
            274) button="BTN_MIDDLE" ;;
            275) button="BTN_SIDE" ;;
            276) button="BTN_EXTRA" ;;
            277) button="BTN_FORWARD" ;;
            278) button="BTN_BACK" ;;
            279) button="BTN_TASK" ;;
            *) button="UNKNOWN_$code" ;;
        esac

        if [ "$value" = "1" ]; then
            echo ">>> PRESSED: $button (code=$code)"
        else
            echo "    Released: $button (code=$code)"
        fi
    done
