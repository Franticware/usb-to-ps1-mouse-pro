#include <stdio.h>
#include <string.h>

#include "parsemouse.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "pico/mutex.h"
#include "pico/stdlib.h"
#include "pio_usb.h"

auto_init_mutex(mtx);

static usb_device_t *usb_device = NULL;

static bool mouseReady = false;
static bool mouseEP_Pending = false;
static MouseConf mouseConf;
static uint8_t mouseAddress = 0;
static uint8_t mouseEP = 0;

#define DEBUG_STDOUT 0

void cb_hid_descriptor(uint8_t address, uint16_t length,
                       const volatile uint8_t *data) {
#if DEBUG_STDOUT
  printf("%02x | Report descriptor:", address);
  for (int i = 0; i < length; i++) {
    printf(" %02x", data[i]);
  }
  printf("\n");
#endif
  MouseConf mouseConfTmp;
  parseMouseDescr(data, length, &mouseConfTmp);
  if (mouseConfTmp.xI != 255 && mouseConfTmp.yI != 255) {
    mouseEP_Pending = true;
    mouseAddress = address;
    mouseConf = mouseConfTmp;
  }
  stdio_flush();
}

void cb_hid_epaddr(uint8_t address, uint8_t epaddr) {
#if DEBUG_STDOUT
  printf("%02x | EP 0x%02x\n", address, epaddr);
#endif
  if (mouseEP_Pending && mouseAddress == address) {
    mouseEP_Pending = false;
    mouseReady = true;
    mouseEP = epaddr;
  }
}

void cb_disconnect(uint8_t address) {
  if (mouseAddress == address) {
#if DEBUG_STDOUT
    printf("%02x | Disconnect\n", address);
#endif
    mouseReady = false;
  }
}

static int8_t gSumX = 0;
static int8_t gSumY = 0;

static int8_t gL = 0;
static int8_t gLUp = 1;
static int8_t gLDown = 0;

static int8_t gR = 0;
static int8_t gRUp = 1;
static int8_t gRDown = 0;

// sum with saturation
int8_t sumSat(int8_t a, int8_t b) {
  int16_t ret = (int16_t)a + (int16_t)b;
  if (ret < -128)
    ret = -128;
  if (ret > 127)
    ret = 127;
  return ret;
}

void core1_main() {
  sleep_ms(10);

  // To run USB SOF interrupt in core1, create alarm pool in core1.
  static pio_usb_configuration_t config = PIO_USB_DEFAULT_CONFIG;
  config.alarm_pool = (void *)alarm_pool_create(2, 1);
  usb_device = pio_usb_host_init(&config);

  //// Call pio_usb_host_add_port to use multi port
  // const uint8_t pin_dp2 = 8;
  // pio_usb_host_add_port(pin_dp2);

  while (true) {
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
            if (mouseReady && device->address == mouseAddress &&
                ep->ep_num == mouseEP) {
              int8_t o[4];
              if (parseMouseData(temp, len, &mouseConf, o) == 0) {
#if DEBUG_STDOUT
                printf("%02x %02x %02x %02x\n", (uint8_t)o[0], (uint8_t)o[1],
                       (uint8_t)o[2], (uint8_t)o[3]);
#endif
                mutex_enter_blocking(&mtx);
                gSumX = sumSat(gSumX, o[1]);
                gSumY = sumSat(gSumY, o[2]);

                gL = o[0] & 1;
                if (gL) {
                  gLDown = 1;
                } else {
                  gLUp = 1;
                }

                gR = o[0] & 2;
                if (gR) {
                  gRDown = 1;
                } else {
                  gRUp = 1;
                }
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

uint8_t noAtt() {
  if (gpio_get(GP_ATT)) {
    return 1;
  } else {
    return 0;
  }
}

uint16_t readCmdWriteData(uint8_t data) {
  uint8_t ret = 0;
  for (int i = 0; i != 8; ++i) {
    while (gpio_get(GP_CLK)) // wait for 0
    {
      tight_loop_contents();
    }
    if (noAtt()) {
      gpio_set_dir(GP_DAT, GPIO_IN);
      return NO_ATT;
    }

    if (data & (1 << i)) {
      gpio_set_dir(GP_DAT, GPIO_IN);
    } else {
      gpio_set_dir(GP_DAT, GPIO_OUT);
      gpio_clr_mask((1 << GP_DAT));
    }

    while (!gpio_get(GP_CLK)) // wait for 1
    {
      tight_loop_contents();
    }
    if (noAtt()) {
      gpio_set_dir(GP_DAT, GPIO_IN);
      return NO_ATT;
    }
    ret |= gpio_get(GP_CMD) << i;
  }
  sleep_us(2);
  gpio_set_dir(GP_DAT, GPIO_IN);
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
  gpio_set_dir(GP_ACK, GPIO_OUT);
  gpio_clr_mask((1 << GP_ACK));
  sleep_us(3);
  gpio_set_dir(GP_ACK, GPIO_IN);
}

void core0_main() {
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

  int8_t sumX = 0;
  int8_t sumY = 0;

  int8_t buttonL = 0;
  int8_t prevL = 0;
  int8_t LUp = 0;
  int8_t LDown = 0;

  int8_t buttonR = 0;
  int8_t prevR = 0;
  int8_t RUp = 0;
  int8_t RDown = 0;

  while (1) {
    while (!gpio_get(GP_ATT)) // wait for 1
    {
      tight_loop_contents();
    }
    while (gpio_get(GP_ATT)) // wait for 0
    {
      tight_loop_contents();
    }

    mutex_enter_blocking(&mtx);
    sumX = sumSat(sumX, gSumX);
    gSumX = 0;
    sumY = sumSat(sumY, gSumY);
    gSumY = 0;

    LDown = LDown || gLDown;
    LUp = LUp || gLUp;
    gLDown = gL;
    gLUp = !gL;

    RDown = RDown || gRDown;
    RUp = RUp || gRUp;
    gRDown = gR;
    gRUp = !gR;
    mutex_exit(&mtx);

    if (LUp && LDown) {
      buttonL = !prevL;
    } else if (LDown) {
      buttonL = 1;
    } else {
      buttonL = 0;
    }

    if (RUp && RDown) {
      buttonR = !prevR;
    } else if (RDown) {
      buttonR = 1;
    } else {
      buttonR = 0;
    }

    uint8_t buttons1 = 3;
    if (buttonL) {
      buttons1 |= 8;
    }
    if (buttonR) {
      buttons1 |= 4;
    }

    if (readCmd() != 0x01) {
      continue;
    }
    postAck();

    if (readCmdWriteData(0x12) != 0x42) {
      continue;
    }
    postAck();

    if (readCmdWriteData(0x5A) == NO_ATT) {
      continue;
    }
    postAck();

    if (readCmdWriteData(0xFF) == NO_ATT) {
      continue;
    }
    postAck();

    if (readCmdWriteData(~buttons1) == NO_ATT) // buttons
    {
      continue;
    }
    postAck();

    if (readCmdWriteData(sumX) == NO_ATT) // dx
    {
      continue;
    }
    postAck();

    if (readCmdWriteData(sumY) == NO_ATT) // dy
    {
      continue;
    }
    // no ack here!

    {
      sumX = 0;
      sumY = 0;
      prevL = buttonL;
      prevR = buttonR;
      LUp = 0;
      LDown = 0;
      RUp = 0;
      RDown = 0;
    }
  }
}

int main() {
  // default 125MHz is not appropriate. Sysclock should be multiple of 12 MHz.
  set_sys_clock_khz(120000, true);

  stdio_usb_init();

  sleep_ms(10);

  multicore_reset_core1();
  // all USB tasks run in core1
  multicore_launch_core1(core1_main);

  core0_main();
}
