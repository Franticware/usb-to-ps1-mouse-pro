/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *                    sekigon-gonnoc
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

// This example runs both host and device concurrently. The USB host receive
// reports from HID device and print it out.
// For TinyUSB roothub port0 is native usb controller

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "ws2812.pio.h"

#define PIO_USB_DP_PIN_DEFAULT 2

#include "pio_usb.h"
#include "tusb.h"

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+

// uncomment if you are using colemak layout
// #define KEYBOARD_COLEMAK

#ifdef KEYBOARD_COLEMAK
const uint8_t colemak[128] = {
    0, 0,  0,  0,  0,  0, 0, 22, 9,  23, 7, 0,  24, 17, 8, 12, 0, 14, 28, 51,
    0, 19, 21, 10, 15, 0, 0, 0,  13, 0,  0, 0,  0,  0,  0, 0,  0, 0,  0,  0,
    0, 0,  0,  0,  0,  0, 0, 0,  0,  0,  0, 18, 0,  0,  0, 0,  0, 0,  0,  0,
    0, 0,  0,  0,  0,  0, 0, 0,  0,  0,  0, 0,  0,  0,  0, 0,  0, 0,  0,  0};
#endif

static uint8_t const keycode2ascii[128][2] = {HID_KEYCODE_TO_ASCII};

/*------------- MAIN -------------*/

// core1: handle host events
void core1_main() {
  sleep_ms(10);

  // Use tuh_configure() to pass pio configuration to the host stack
  // Note: tuh_configure() must be called before
  tuh_hid_set_default_protocol(HID_PROTOCOL_REPORT);
  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
  tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);

  // To run USB SOF interrupt in core1, init host stack for pio_usb (roothub
  // port1) on core1
  tuh_init(1);

  while (true) {
    tuh_task(); // tinyusb host task
  }
}

// core0: handle device events
int main(void) {
  // default 125MHz is not appropriate. Sysclock should be multiple of 12MHz.
  set_sys_clock_khz(120000, true);

  sleep_ms(10);

  stdio_init_all();

  multicore_reset_core1();
  // all USB task run in core1
  multicore_launch_core1(core1_main);

  while (true) {
    tight_loop_contents();
  }

  return 0;
}

//--------------------------------------------------------------------+
// Host HID
//--------------------------------------------------------------------+

// Invoked when device with hid interface is mounted
// Report descriptor is also available for use.
// tuh_hid_parse_report_descriptor() can be used to parse common/simple enough
// descriptor. Note: if report descriptor length > CFG_TUH_ENUMERATION_BUFSIZE,
// it will be skipped therefore report_desc = NULL, desc_len = 0
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                      uint8_t const *desc_report, uint16_t desc_len) {
  printf("DESCR len=%u;", desc_len);
  for (int i = 0; i != desc_len; ++i) {
    printf(" %x", desc_report[i]);
  }
  printf("\r\n");

  (void)desc_report;
  (void)desc_len;

  // Interface protocol (hid_interface_protocol_enum_t)
  const char *protocol_str[] = {"None", "Keyboard", "Mouse"};
  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

  uint16_t vid, pid;
  tuh_vid_pid_get(dev_addr, &vid, &pid);

  printf("[%04x:%04x][%u] HID Interface%u, Protocol = %s\r\n", vid, pid,
         dev_addr, instance, protocol_str[itf_protocol]);

  // Receive report from boot keyboard & mouse only
  // tuh_hid_report_received_cb() will be invoked when report is available
  if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD ||
      itf_protocol == HID_ITF_PROTOCOL_MOUSE) {
    if (!tuh_hid_receive_report(dev_addr, instance)) {
      printf("Error: cannot request report\r\n");
    }
  }
}

// Invoked when device with hid interface is un-mounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
  printf("[%u] HID Interface%u is unmounted\r\n", dev_addr, instance);
}

// look up new key in previous keys
static inline bool find_key_in_report(hid_keyboard_report_t const *report,
                                      uint8_t keycode) {
  for (uint8_t i = 0; i < 6; i++) {
    if (report->keycode[i] == keycode)
      return true;
  }

  return false;
}

// convert hid keycode to ascii and print
static void process_kbd_report(uint8_t dev_addr,
                               hid_keyboard_report_t const *report) {
  (void)dev_addr;
  static hid_keyboard_report_t prev_report = {
      0, 0, {0}}; // previous report to check key released

  for (uint8_t i = 0; i < 6; i++) {
    uint8_t keycode = report->keycode[i];
    if (keycode) {
      if (find_key_in_report(&prev_report, keycode)) {
        // exist in previous report means the current key is holding
      } else {
// not existed in previous report means the current key is pressed

// remap the key code for Colemak layout
#ifdef KEYBOARD_COLEMAK
        uint8_t colemak_key_code = colemak[keycode];
        if (colemak_key_code != 0)
          keycode = colemak_key_code;
#endif

        bool const is_shift = report->modifier & (KEYBOARD_MODIFIER_LEFTSHIFT |
                                                  KEYBOARD_MODIFIER_RIGHTSHIFT);
        uint8_t ch = keycode2ascii[keycode][is_shift ? 1 : 0];

        if (ch) {
          if (ch == '\n')
            printf("\r");
          printf("%c", ch);
        }
      }
    }
    // TODO example skips key released
  }

  prev_report = *report;
}

// process mouse report and print
static void process_mouse_report(uint8_t dev_addr,
                                 hid_mouse_report_t const *report) {
  //------------- button state  -------------//
  // uint8_t button_changed_mask = report->buttons ^ prev_report.buttons;
  char l = report->buttons & MOUSE_BUTTON_LEFT ? 'L' : '-';
  char m = report->buttons & MOUSE_BUTTON_MIDDLE ? 'M' : '-';
  char r = report->buttons & MOUSE_BUTTON_RIGHT ? 'R' : '-';

  printf("[%u] %c%c%c %d %d %d\r\n", dev_addr, l, m, r, report->x, report->y,
         report->wheel);
}

// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                uint8_t const *report, uint16_t len) {
  printf("REPORT len=%u;", len);
  for (int i = 0; i != len; ++i) {
    printf(" %x", report[i]);
  }
  printf("\r\n");

  (void)len;
  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

  switch (itf_protocol) {
  case HID_ITF_PROTOCOL_KEYBOARD:
    process_kbd_report(dev_addr, (hid_keyboard_report_t const *)report);
    break;

  case HID_ITF_PROTOCOL_MOUSE:
    process_mouse_report(dev_addr, (hid_mouse_report_t const *)report);
    break;

  default:
    break;
  }

  // continue to request to receive report
  if (!tuh_hid_receive_report(dev_addr, instance)) {
    printf("Error: cannot request report\r\n");
  }
}
