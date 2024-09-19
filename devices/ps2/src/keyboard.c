/*
 * luxOS - a unix-like operating system
 * Omar Elghoul, 2024
 * 
 * ps2: Driver for PS/2 (and USB emulated) keyboards and mice
 */

#include <ps2/ps2.h>
#include <liblux/liblux.h>
#include <sys/io.h>

/* keyboardInit(): initializes a PS/2 keyboard
 * params: none
 * returns: nothing
 */

void keyboardInit() {
    // enable the keyboard port
    ps2sendNoACK(PS2_CONTROLLER, PS2_ENABLE_KEYBOARD);

    // check if there is a keyboard connected
    if(ps2send(PS2_KEYBOARD, PS2_KEYBOARD_ECHO) != PS2_KEYBOARD_ECHO) return;

    // reset the keyboard
    while(ps2send(PS2_KEYBOARD, PS2_KEYBOARD_RESET) != PS2_DEVICE_ACK);
    while(!readReady());
    uint8_t status = inb(0x60);
    if(status != PS2_DEVICE_PASS) {
        luxLogf(KPRINT_LEVEL_ERROR, "failed to reset PS/2 keyboard, response byte 0x%02X\n", status);
        return;
    }

    // disable scanning
    while(ps2send(PS2_KEYBOARD, PS2_KEYBOARD_DISABLE_SCAN) != PS2_DEVICE_ACK);

    // set autorepeat rate
    ps2send(PS2_KEYBOARD, PS2_KEYBOARD_SET_AUTOREPEAT);
    ps2send(PS2_KEYBOARD, 0x20);    // 500 ms delay

    // set scancode set
    ps2send(PS2_KEYBOARD, PS2_KEYBOARD_SET_SCANCODE);
    ps2send(PS2_KEYBOARD, 2);       // scancode set 2

    // and enable the keyboard
    ps2send(PS2_KEYBOARD, PS2_KEYBOARD_ENABLE_SCAN);

    luxLogf(KPRINT_LEVEL_DEBUG, "using keyboard scancode set 2\n");
}