#include <stdio.h>
#include <string.h>

#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "pico/mutex.h"
#include "pico/stdlib.h"
#include "pio_usb.h"
#include "ws2812.h"

auto_init_mutex(mtx);

static usb_device_t *usb_device = NULL;

static bool keyboardReady = false;
static bool keyboardEP_Pending = false;
static uint8_t keyboardAddress = 0;
static uint8_t keyboardEP = 0;

#define DEBUG_STDOUT 0

#define COLOR_BLACK 0x000000
#define COLOR_FAINT_RED 0x000300
#define COLOR_FAINT_KEYBOARD_VIOLET 0x000202
#define COLOR_FAINT_WARM_WHITE 0x020201

static const uint16_t KEY_BUTTON_MAP[4 + 8 + 256 + 4] = {
    0x454b, 0x4d59, 0x5041, 0x3e3e, 0,      0,      0,      0,      0x0008,
    0x0001, 0,      0,      0,      0,      0,      0,      0x0080, 0,
    0,      0x0020, 0x0100, 0x4000, 0x2000, 0,      0x0200, 0,      0x8000,
    0x4000, 0,      0,      0x1000, 0x0800, 0x0400, 0x8000, 0x0040, 0x1000,
    0,      0,      0x0010, 0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,
    0x2000, 0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,
    0,      0x0020, 0x0080, 0x0040, 0x0010, 0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0,      0,      0,      0,      0,      0x3c3c, 0x454b,
    0x4d59, 0x5041,

};

static const uint16_t *KEY_PTR = KEY_BUTTON_MAP + 4 + 8;
static const uint16_t *MOD_PTR = KEY_BUTTON_MAP + 4;

bool testKeyboardDescr(const volatile uint8_t *descr, uint32_t descrLen) {
  return descrLen > 4 && descr[0] == 0x05 && descr[1] == 0x01 &&
         descr[2] == 0x09 && descr[3] == 0x06;
}

bool parseKeyboardData(const uint8_t *data, uint32_t dataLen,
                       uint16_t *buttonsPtr) {
  if (dataLen != 8) {
    return false;
  }
  if (data[2] == 1 || data[3] == 1 || data[4] == 1 || data[5] == 1 ||
      data[6] == 1 || data[7] == 1) {
    return false;
  }
  uint16_t buttons = 0;
  for (int i = 0; i != 8; ++i) {
    if (data[0] & (1 << i)) {
      buttons |= MOD_PTR[i];
    }
  }
  for (int i = 2; i != 8; ++i) {
    if (data[i]) {
      buttons |= KEY_PTR[data[i]];
    }
  }
  *buttonsPtr = buttons;
#if DEBUG_STDOUT
  printf(" %04x\n", (uint16_t)buttons);
  stdio_flush();
#endif
  return true;
}

void cb_hid_descriptor(uint8_t address, uint16_t length,
                       const volatile uint8_t *data) {
#if DEBUG_STDOUT
  printf("%02x | Report descriptor:", address);
  for (int i = 0; i < length; i++) {
    printf(" %02x", data[i]);
  }
  printf("\n");
#endif
  if (testKeyboardDescr(data, length)) {
    keyboardEP_Pending = true;
    keyboardAddress = address;
  }
#if DEBUG_STDOUT
  stdio_flush();
#endif
}

void cb_hid_epaddr(uint8_t address, uint8_t epaddr) {
#if DEBUG_STDOUT
  printf("%02x | EP 0x%02x\n", address, epaddr);
#endif
  if (keyboardEP_Pending && keyboardAddress == address) {
    keyboardEP_Pending = false;
    keyboardReady = true;
    keyboardEP = epaddr;
  }
}

void cb_disconnect(uint8_t address) {
  if (keyboardAddress == address) {
#if DEBUG_STDOUT
    printf("%02x | Disconnect\n", address);
#endif
    keyboardReady = false;
  }
}

static uint16_t gButtons = 0; // controller buttons
static bool gErrOvf = false;  // keyboard error - overflow

void core1_main(void) {
  sleep_ms(10);

  // To run USB SOF interrupt in core1, create alarm pool in core1.
  static pio_usb_configuration_t config = PIO_USB_DEFAULT_CONFIG;
  config.alarm_pool = (void *)alarm_pool_create(2, 1);
  usb_device = pio_usb_host_init(&config);

  //// Call pio_usb_host_add_port to use multi port
  // const uint8_t pin_dp2 = 8;
  // pio_usb_host_add_port(pin_dp2);

  for (;;) {
    pio_usb_host_task();

    if (usb_device != NULL) {
      for (int dev_idx = 0; dev_idx < PIO_USB_DEVICE_CNT; dev_idx++) {
        usb_device_t *device = &usb_device[dev_idx];
        if (!device->connected) {
          continue;
        }

        // Print received packet to EPs
        for (int ep_idx = 0; ep_idx < PIO_USB_DEV_EP_CNT; ep_idx++) {
          endpoint_t *ep = pio_usb_get_endpoint(device, ep_idx);

          if (ep == NULL) {
            break;
          }

          uint8_t temp[64];
          int len = pio_usb_get_in_data(ep, temp, sizeof(temp));

          if (len > 0) {
#if DEBUG_STDOUT
            printf("%02x | EP 0x%02x:", device->address, ep->ep_num);
            for (int i = 0; i < len; i++) {
              printf(" %02x", temp[i]);
            }
            printf("\n");
#endif
            if (keyboardReady && device->address == keyboardAddress &&
                ep->ep_num == keyboardEP) {
              uint16_t buttons = 0;
              if (parseKeyboardData(temp, len, &buttons)) {
                mutex_enter_blocking(&mtx);
                gButtons = buttons;
                gErrOvf = false;
                mutex_exit(&mtx);
              } else {
                mutex_enter_blocking(&mtx);
                gErrOvf = true;
                mutex_exit(&mtx);
              }
            }
          }
        }
      }
    }
#if DEBUG_STDOUT
    stdio_flush();
#endif
  }
}

#define GP_ATT 7
#define GP_CLK 10
#define GP_DAT 11
#define GP_CMD 14
#define GP_ACK 15

#define NO_ATT 0x100

static inline uint8_t noAtt(void) {
  if (gpio_get(GP_ATT)) {
    return 1;
  } else {
    return 0;
  }
}

static inline void setBus(uint gpio) {
  gpio_set_dir(gpio, GPIO_IN);
  gpio_set_mask(1 << gpio);
}

static inline void clrBus(uint gpio) {
  gpio_clr_mask(1 << gpio);
  gpio_set_dir(gpio, GPIO_OUT);
}

uint16_t readCmdWriteData(uint8_t data) {
  uint8_t ret = 0;
  for (int i = 0; i != 8; ++i) {
    while (gpio_get(GP_CLK)) // wait for 0
    {
      tight_loop_contents();
    }
    if (noAtt()) {
      setBus(GP_DAT);
      return NO_ATT;
    }

    if (data & (1 << i)) {
      setBus(GP_DAT);
    } else {
      clrBus(GP_DAT);
    }

    while (!gpio_get(GP_CLK)) // wait for 1
    {
      tight_loop_contents();
    }
    if (noAtt()) {
      setBus(GP_DAT);
      return NO_ATT;
    }
    ret |= gpio_get(GP_CMD) << i;
  }
  sleep_us(2);
  setBus(GP_DAT);
  return ret;
}

uint16_t readCmd(void) {
  uint8_t ret = 0;
  for (int i = 0; i != 8; ++i) {
    while (gpio_get(GP_CLK)) // wait for 0
    {
      tight_loop_contents();
    }
    if (noAtt()) {
      return NO_ATT;
    }

    while (!gpio_get(GP_CLK)) // wait for 1
    {
      tight_loop_contents();
    }
    if (noAtt()) {
      return NO_ATT;
    }

    ret |= gpio_get(GP_CMD) << i;
  }
  sleep_us(2);
  return ret;
}

void postAck(void) {
  sleep_us(11);
  clrBus(GP_ACK);
  sleep_us(3);
  setBus(GP_ACK);
}

void core0_main(void) {
  gpio_init(GP_ATT);
  gpio_set_dir(GP_ATT, GPIO_IN);

  gpio_init(GP_CLK);
  gpio_set_dir(GP_CLK, GPIO_IN);

  gpio_init(GP_DAT);
  gpio_set_slew_rate(GP_DAT, GPIO_SLEW_RATE_SLOW);
  gpio_set_dir(GP_DAT, GPIO_IN);
  gpio_clr_mask((1 << GP_DAT));

  gpio_init(GP_CMD);
  gpio_set_dir(GP_CMD, GPIO_IN);

  gpio_init(GP_ACK);
  gpio_set_slew_rate(GP_ACK, GPIO_SLEW_RATE_SLOW);
  gpio_set_dir(GP_ACK, GPIO_IN);
  gpio_clr_mask((1 << GP_ACK));

  gpio_init(GP_RGB);
  gpio_set_slew_rate(GP_RGB, GPIO_SLEW_RATE_SLOW);
  gpio_set_dir(GP_RGB, GPIO_OUT);
  gpio_clr_mask(1 << GP_RGB);
  sleep_us(WS2812_RESET_US);
  set_ws2812(COLOR_BLACK);
  sleep_us(WS2812_RESET_US);
  set_ws2812(COLOR_FAINT_KEYBOARD_VIOLET);
  sleep_us(WS2812_RESET_US);

  bool updateRgb = false;
  uint16_t buttons = 0;
  bool errOvf = false;

  for (;;) {
    while (!noAtt()) // wait to finish current attention cycle
    {
      tight_loop_contents();
    }

    if (updateRgb) {
      set_ws2812(errOvf ? COLOR_FAINT_RED
                        : (buttons & 8 ? COLOR_FAINT_WARM_WHITE
                                       : COLOR_FAINT_KEYBOARD_VIOLET));
      updateRgb = false;
    }

    while (noAtt()) // wait for new attention signal
    {
      tight_loop_contents();
    }

    if (readCmd() != 0x01) {
      continue;
    }
    postAck();

    if (readCmdWriteData(0x41) != 0x42) {
      continue;
    }
    postAck();

    if (readCmdWriteData(0x5A) == NO_ATT) {
      continue;
    }
    postAck();

    mutex_enter_blocking(&mtx);
    buttons = gButtons;
    errOvf = gErrOvf;
    mutex_exit(&mtx);

    if (readCmdWriteData(~buttons) == NO_ATT) {
      continue;
    }
    postAck();

    if (readCmdWriteData(~(buttons >> 8)) == NO_ATT) {
      continue;
    }

    // no ack here!
    updateRgb = true;
  }
}

int main() {
  // default 125MHz is not appropriate. Sysclock should be multiple of 12 MHz.
  set_sys_clock_khz(120000, true);

#if DEBUG_STDOUT
  stdio_usb_init();
#endif

  sleep_ms(10);

  multicore_reset_core1();
  // all USB tasks run in core1
  multicore_launch_core1(core1_main);

  core0_main();
}
