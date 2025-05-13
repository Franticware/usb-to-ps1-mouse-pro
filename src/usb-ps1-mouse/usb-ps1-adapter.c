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

#define IS_RGBW false
#define WS2812_PIN 16

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




  // todo get free sm
  PIO pio;
  uint sm;
  uint offset;

  // This will find a free pio and state machine for our program and load it for us
  // We use pio_claim_free_sm_and_add_program_for_gpio_range (for_gpio_range variant)
  // so we will get a PIO instance suitable for addressing gpios >= 32 if needed and supported by the hardware
  bool success = pio_claim_free_sm_and_add_program_for_gpio_range(&ws2812_program, &pio, &sm, &offset, WS2812_PIN, 1, true);
  hard_assert(success);

  ws2812_program_init(pio, sm, offset, WS2812_PIN, 800000, IS_RGBW);

  uint32_t pixel_grb = 0;

  while (true) {
    //tight_loop_contents();

    pixel_grb = 0x110000;

    sleep_ms(250);
    pio_sm_put_blocking(pio, sm, pixel_grb << 8u);

    pixel_grb = 0x001100;

    sleep_ms(250);
    pio_sm_put_blocking(pio, sm, pixel_grb << 8u);

    pixel_grb = 0x000011;

    sleep_ms(250);
    pio_sm_put_blocking(pio, sm, pixel_grb << 8u);

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

  // Interface protocol (hid_interface_protocol_enum_t)
  const char *protocol_str[] = {"None", "Keyboard", "Mouse"};
  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

  uint16_t vid, pid;
  tuh_vid_pid_get(dev_addr, &vid, &pid);

  absolute_time_t currentTime = to_us_since_boot(get_absolute_time());

  printf("{\"event\":\"mount\",\"timestamp\":\"%llu\",\"vid\":\"%04x\",\"pid\":\"%04x\",\"address\":\"%u\",\"instance\":\"%u\",\"protocol\":\"%s\",", currentTime, vid, pid, dev_addr, instance, protocol_str[itf_protocol]);

  printf("\"data\":\"");

  for (int i = 0; i != desc_len; ++i) {
    if (i != 0)
      printf(" ");
    printf("%02x", desc_report[i]);
  }
  printf("\"");


  // Receive report from boot keyboard & mouse only
  // tuh_hid_report_received_cb() will be invoked when report is available
  if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD ||
      itf_protocol == HID_ITF_PROTOCOL_MOUSE) {
    if (!tuh_hid_receive_report(dev_addr, instance)) {
      printf(",\"error\":\"cannot request report\"");
    }
  }
  printf("},\n");
}

// Invoked when device with hid interface is un-mounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
  absolute_time_t currentTime = to_us_since_boot(get_absolute_time());
  printf("{\"event\":\"umount\",\"timestamp\":\"%llu\",\"address\":\"%u\",\"instance\":\"%u\"},\n", currentTime, dev_addr, instance);
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

// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                uint8_t const *report, uint16_t len) {

  absolute_time_t currentTime = to_us_since_boot(get_absolute_time());

  printf("{\"event\":\"report\",\"timestamp\":\"%llu\",\"address\":\"%u\",\"instance\":\"%u\",", currentTime, dev_addr, instance);

  printf("\"data\":\"");

  for (int i = 0; i != len; ++i) {
    if (i != 0)
      printf(" ");
    printf("%02x", report[i]);
  }
  printf("\"");

  // continue to request to receive report
  if (!tuh_hid_receive_report(dev_addr, instance)) {
    printf(",\"error\":\"cannot request report\"");
  }

  printf("},\n");
}
