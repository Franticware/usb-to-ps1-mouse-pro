#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "parsemouse.h"
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

#define GP_ATT 7
#define GP_CLK 10
#define GP_DAT 11
#define GP_CMD 14
#define GP_ACK 15

/*------------- MAIN -------------*/

#define PIX_OFF 0
#define PIX_BLINK 1
#define PIX_MOUSE 2
#define PIX_KEYB 3
#define PIX_CLICK 4
#define PIX_OVF 5

#define COLOR_BLACK 0x000000
#define COLOR_FAINT_MOUSE_GREEN 0x020001
#define COLOR_FAINT_KEYBOARD_VIOLET 0x000202
#define COLOR_FAINT_RED 0x000300
#define COLOR_FAINT_WARM_WHITE 0x020201

uint8_t gPixState = PIX_OFF;

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

  PIO pio;
  uint sm;
  uint offset;

  // This will find a free pio and state machine for our program and load it for
  // us We use pio_claim_free_sm_and_add_program_for_gpio_range (for_gpio_range
  // variant) so we will get a PIO instance suitable for addressing gpios >= 32
  // if needed and supported by the hardware
  bool success = pio_claim_free_sm_and_add_program_for_gpio_range(
      &ws2812_program, &pio, &sm, &offset, WS2812_PIN, 1, true);
  hard_assert(success);

  ws2812_program_init(pio, sm, offset, WS2812_PIN, 800000, IS_RGBW);

  uint64_t prevTime = to_us_since_boot(get_absolute_time());

  uint64_t timeSum = 0;

  const uint64_t updatePeriod = 50 * 1000;

  gPixState = PIX_BLINK;
  uint8_t blinkI = 0;

  uint32_t pixGRB = 0;

  while (true) {
    tuh_task(); // tinyusb host task

    uint64_t currTime = to_us_since_boot(get_absolute_time());
    timeSum += (currTime - prevTime);
    prevTime = currTime;

    if (timeSum >= updatePeriod) {
      timeSum -= updatePeriod;
      switch (gPixState) {
      case PIX_OFF:
        pixGRB = COLOR_BLACK;
        break;
      case PIX_BLINK:
        if (blinkI < 10) {
          pixGRB = COLOR_FAINT_WARM_WHITE;
        } else {
          pixGRB = COLOR_BLACK;
        }
        break;
      case PIX_MOUSE:
        pixGRB = COLOR_FAINT_MOUSE_GREEN;
        break;
      case PIX_KEYB:
        pixGRB = COLOR_FAINT_KEYBOARD_VIOLET;
        break;
      case PIX_OVF:
        pixGRB = COLOR_FAINT_RED;
        break;
      case PIX_CLICK:
        pixGRB = COLOR_FAINT_WARM_WHITE;
        break;
      default:
        gPixState = PIX_OFF;
        pixGRB = COLOR_BLACK;
        break;
      }
      ++blinkI;
      if (blinkI == 20) {
        blinkI = 0;
      }
      pio_sm_put_blocking(pio, sm, pixGRB << 8u);
    }
  }
}

#define SM_A0 0
#define SM_A1 1
#define SM_A0C1 2
#define SM_A0C0 3

typedef struct {
  uint8_t state;
  uint8_t i; // bit index
  uint8_t y; // byte index
  uint8_t cmd[2];
  uint8_t data[10]; // mouse/pad data
  uint8_t size;     // mouse/pad data size
} ConSM;

ConSM gSM;

#define PROT_NONE 0
#define PROT_KEYB 1
#define PROT_MOUSE 2

uint8_t gContrProt = PROT_NONE;

auto_init_mutex(mtx);

static int8_t gSumX = 0;
static int8_t gSumY = 0;
static bool gL = false;
static bool gR = false;

// sum with saturation
int8_t sumSat(int8_t a, int8_t b) {
  int16_t ret = (int16_t)a + (int16_t)b;
  if (ret < -128)
    ret = -128;
  if (ret > 127)
    ret = 127;
  return ret;
}

void SM_init() {
  if (gpio_get(GP_ATT)) {
    gSM.state = SM_A1;
  } else {
    gSM.state = SM_A0;
  }
}

void SM_task() {
  switch (gSM.state) {
  case SM_A0: {
    gpio_set_dir(GP_DAT, GPIO_IN);
    if (gpio_get(GP_ATT)) {
      gSM.state = SM_A1;
    }
    break;
  }
  case SM_A1: {
    gpio_set_dir(GP_DAT, GPIO_IN);
    if (!gpio_get(GP_ATT)) {
      gSM.i = gSM.y = 0;
      gSM.cmd[0] = gSM.cmd[1] = 0;
      gSM.state = SM_A0C1;
    }
    break;
  }
  case SM_A0C1: {
    if (!gpio_get(GP_CLK)) {
      gSM.state = SM_A0C0;
      // falling edge clock
      if (gSM.y > 0 && gSM.y <= gSM.size) {
        if (gSM.data[gSM.y - 1] & (1 << gSM.i)) {
          gpio_set_dir(GP_DAT, GPIO_IN);
        } else {
          gpio_set_dir(GP_DAT, GPIO_OUT);
        }
      }
    } else if (gpio_get(GP_ATT)) {
      gSM.state = SM_A1;
    }
    break;
  }
  case SM_A0C0: {
    if (gpio_get(GP_CLK)) {
      gSM.state = SM_A0C1;
      // rising edge clock
      if (gpio_get(GP_CMD)) {
        if (gSM.y < 2) {
          gSM.cmd[gSM.y] |= 1 << (gSM.i);
        }
      }
      ++gSM.i;
      if (gSM.i == 8) {
        gSM.i = 0;
        if (gSM.y == 0) {
          if (gSM.cmd[gSM.y] != 1) {
            gSM.state = SM_A0;
            break;
          } else {
            /*
            gSM.size = 6;
            gSM.data[0] = 0x12;
            gSM.data[1] = 0x5A;
            gSM.data[2] = 0xFF;
            gSM.data[3] = 0x00;
            gSM.data[4] = 0x00;
            gSM.data[5] = 0x00;*/

            bool buttonL = false;
            bool buttonR = false;

            if (gContrProt == PROT_MOUSE)
            {
              mutex_enter_blocking(&mtx);
              int8_t sumX = gSumX;
              gSumX = 0;
              int8_t sumY = gSumY;
              gSumY = 0;
              buttonL = gL;
              buttonR = gR;
              mutex_exit(&mtx);

              uint8_t buttons1 = 3;
              if (buttonL) {
                buttons1 |= 8;
              }
              if (buttonR) {
                buttons1 |= 4;
              }

              gSM.size = 6;
              gSM.data[0] = 0x12;
              gSM.data[1] = 0x5A;
              gSM.data[2] = 0xFF;
              gSM.data[3] = ~buttons1;
              gSM.data[4] = sumX;
              gSM.data[5] = sumY;
            }
            else
            {
              gSM.state = SM_A0;
              break;
            }



          }
        } else if (gSM.y == 1) {
          if (gSM.cmd[gSM.y] != 0x42) {
            gSM.state = SM_A0;
            break;
          }
        }
        sleep_us(11);
        gpio_set_dir(GP_DAT, GPIO_IN);
        if (gSM.y < gSM.size) {
          gpio_set_dir(GP_ACK, GPIO_OUT);
          sleep_us(3);
          gpio_set_dir(GP_ACK, GPIO_IN);
        }
        ++gSM.y;
      }
    } else if (gpio_get(GP_ATT)) {
      gSM.state = SM_A1;
    }
    break;
  }
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

  SM_init();

  while (true) {
    SM_task();
  }

  return 0;
}

//--------------------------------------------------------------------+
// Host HID
//--------------------------------------------------------------------+

typedef struct {
  uint8_t protocol;
  uint8_t dev_addr;
  uint8_t instance;
  MouseConf mouse;
} USBDev;

#define gUSBDevsCount 8
static USBDev gUSBDevs[gUSBDevsCount] = {{0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}};

USBDev* findEmptyDev()
{
  for (uint8_t i = 0; i != gUSBDevsCount; ++i)
  {
    if (gUSBDevs[i].protocol == PROT_NONE)
    {
      return gUSBDevs + i;
    }
  }
  return NULL;
}

USBDev* findDev(uint8_t dev_addr, uint8_t instance)
{
  for (uint8_t i = 0; i != gUSBDevsCount; ++i)
  {
    if (gUSBDevs[i].dev_addr == dev_addr && gUSBDevs[i].instance == instance)
    {
      return gUSBDevs + i;
    }
  }
  return NULL;
}

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

  uint64_t currentTime = to_us_since_boot(get_absolute_time());

  printf("{\"event\":\"mount\",\"timestamp\":\"%llu\",\"vid\":\"%04x\",\"pid\":"
         "\"%04x\",\"address\":\"%u\",\"instance\":\"%u\",\"protocol\":\"%s\",",
         currentTime, vid, pid, dev_addr, instance, protocol_str[itf_protocol]);

  printf("\"data\":\"");

  for (int i = 0; i != desc_len; ++i) {
    if (i != 0)
      printf(" ");
    printf("%02x", desc_report[i]);
  }
  printf("\"");

  if (itf_protocol == PROT_KEYB)
  {
    USBDev* usbdev = NULL;
    if ((usbdev = findEmptyDev()))
    {
      usbdev->protocol = PROT_KEYB;
      usbdev->dev_addr = dev_addr;
      usbdev->instance = instance;
      gPixState = PIX_KEYB;
      gContrProt = PROT_KEYB;
    }
  }
  else if (itf_protocol == PROT_MOUSE)
  {
    USBDev* usbdev = NULL;
    MouseConf mouseConfTmp;
    parseMouseDescr(desc_report, desc_len, &mouseConfTmp);
    if (mouseConfTmp.xI != 255 && mouseConfTmp.yI != 255) {
      if ((usbdev = findEmptyDev()))
      {
        usbdev->protocol = PROT_MOUSE;
        usbdev->dev_addr = dev_addr;
        usbdev->instance = instance;
        usbdev->mouse = mouseConfTmp;
        gPixState = PIX_MOUSE;
        gContrProt = PROT_MOUSE;
      }
    }
  }

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
  uint64_t currentTime = to_us_since_boot(get_absolute_time());
  printf("{\"event\":\"umount\",\"timestamp\":\"%llu\",\"address\":\"%u\","
         "\"instance\":\"%u\"},\n",
         currentTime, dev_addr, instance);

  USBDev* usbdev = NULL;
  if ((usbdev = findDev(dev_addr, instance)))
  {
    usbdev->protocol = PROT_NONE;
  }
}

// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                uint8_t const *report, uint16_t len) {

  uint64_t currentTime = to_us_since_boot(get_absolute_time());

  printf("{\"event\":\"report\",\"timestamp\":\"%llu\",\"address\":\"%u\","
         "\"instance\":\"%u\",",
         currentTime, dev_addr, instance);

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


  USBDev* usbdev = NULL;
  if ((usbdev = findDev(dev_addr, instance)))
  {
    if (usbdev->protocol == PROT_MOUSE)
    {
      int8_t o[4];
      if (parseMouseData(report, len, &usbdev->mouse, o) == 0) {
        mutex_enter_blocking(&mtx);
        if (gContrProt != PROT_MOUSE)
        {
          gSumX = 0;
          gSumY = 0;
        }
        gSumX = sumSat(gSumX, o[1]);
        gSumY = sumSat(gSumY, o[2]);
        gL = o[0] & 1;
        gR = o[0] & 2;
        gContrProt = PROT_MOUSE;
        gPixState = gL ? PIX_CLICK : PIX_MOUSE;
        mutex_exit(&mtx);
      }
    }
    else if (usbdev->protocol == PROT_KEYB)
    {

    }
  }
}
