#include "parsemouse.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  uint32_t min;
  uint32_t max;
  int32_t strId;
} HidPage;

typedef struct {
  uint32_t id;
  HidPage *page;
  uint32_t pagesize;
  int32_t strId;
} HidUsagePage;

#define USAGE_PAGE 0x01
#define USAGE 0x02
#define COLLECTION 0x28
#define END_COLLECTION 0xc0
#define USAGE_MINIMUM 0x30
#define USAGE_MAXIMUM 0x0A
#define LOGICAL_MINIMUM 0x05
#define LOGICAL_MAXIMUM 0x09
#define REPORT_COUNT 0x25
#define REPORT_SIZE 0x1D
#define INPUT 0x20
#define OUTPUT 0x24
#define REPORTID 0x21

//---------------COLLECTION-----------------
#define Application 0x01

//---------------USAGE_PAGE-----------------
#define GenericDesktop 0x01
#define Button 0x09
#define USAGE_PAGE_Keyboard 0x07
#define LEDs 0x08

//---------------USAGE-----------------
#define USAGE_Keyboard 0x06
#define Mouse 0x02
#define X 0x30
#define Y 0x31
#define USAGE_Wheel 0x38

//---------------INPUT-----------------
#define INPUT_Const 0x01
#define INPUT_Var 0x02
#define INPUT_Rel 0x04
#define INPUT_Wrap 0x08
#define INPUT_NLin 0x10
#define INPUT_NPrf 0x20
#define INPUT_Null 0x40
#define INPUT_Vol 0x80

//
#define StrId_Page_Button 0
#define StrId_NoButtons 1
#define StrId_Button 2
#define StrId_Page_GenericDesktop 3
#define StrId_Undefined 4
#define StrId_Pointer 5
#define StrId_Mouse 6
#define StrId_Reserved 7
#define StrId_X 8
#define StrId_Y 9
#define StrId_Wheel 10

HidPage hid_Button[] = {{0x0, 0x0, StrId_NoButtons}, {0x1, 0x1, StrId_Button}};
HidPage hid_Generic_Desktop[] = {
    {0x0, 0x0, StrId_Undefined}, {0x1, 0x1, StrId_Pointer},
    {0x2, 0x2, StrId_Mouse},     {0x3, 0x3, StrId_Reserved},
    {0x30, 0x30, StrId_X},       {0x31, 0x31, StrId_Y},
    {0x38, 0x38, StrId_Wheel}};

#define COUNT_OF(x)                                                            \
  ((sizeof(x) / sizeof(0 [x])) / ((size_t)(!(sizeof(x) % sizeof(0 [x])))))

HidUsagePage allpage[] = {
    {9, hid_Button, COUNT_OF(hid_Button), StrId_Page_Button},
    {1, hid_Generic_Desktop, COUNT_OF(hid_Generic_Desktop),
     StrId_Page_GenericDesktop},
};

int32_t getPageName(uint32_t id) {
  for (size_t i = 0; i < COUNT_OF(allpage); i++) {
    if (allpage[i].id == id) {
      return allpage[i].strId;
    }
  }
  return -1;
}

int32_t getUsageName(uint32_t id, uint32_t usageid) {
  for (size_t i = 0; i < COUNT_OF(allpage); i++) {
    if (allpage[i].id == id) {
      HidPage *usage = allpage[i].page;

      for (size_t p = 0; p < allpage[i].pagesize; p++) {
        if ((usage[p].min >= usageid) && (usage[p].max <= usageid)) {
          return usage[p].strId;
        }
      }
      return -1;
    }
  }
  return -1;
}

#define FieldUndefined -1
#define FieldReportId 0
#define FieldUsagePage 1
#define FieldReportCount 2
#define FieldReportSize 3
#define FieldInput 4

void parseMouseDescr(const volatile uint8_t *hid, uint32_t hidlen,
                     MouseConf *conf) {
  conf->isId = 0;
  conf->id = 0;
  conf->btnI = 255;
  conf->xI = 255;
  conf->xSize = 255;
  conf->yI = 255;
  conf->ySize = 255;
  conf->wheelI = 255;
  conf->wheelSize = 255;
  int isMouse = 0;
  uint32_t mouseAccum = 0; // mouse offset accumulator
  uint8_t level = 0;
  uint32_t usage = 0, usagepage = 0;
  uint32_t reportCount = 0, reportSize = 0;
  uint32_t usageIndex = 0, usageIndexX = 255, usageIndexY = 255,
           usageIndexWheel = 255;

  for (uint32_t i = 0; i < hidlen;) {
    uint8_t cmd = hid[i];
    uint8_t datalen = 0;
    uint32_t cmdtype = 0;
    switch (cmd & 3) {
    case 3: {
      datalen = 4;
    } break;
    default: {
      datalen = cmd & 3;
    } break;
    }
    i += 1;
    int32_t field = FieldUndefined;
    switch ((cmd >> 2) & 0x3) {
    case 0: {
      switch (cmd >> 4) {
      case 8: {
        cmdtype = 3;
        field = FieldInput;
      } break;
      case 9: {
        cmdtype = 3;
      } break;
      case 0xa: {
        level += 1;
        usageIndex = 0;
        cmdtype = 4;
      } break;
      case 0xc: {
        usageIndex = 0;
        level -= 1;
      } break;
      }
    } break;
    case 1: {
      switch (cmd >> 4) {
      case 0: {
        field = FieldUsagePage;
        cmdtype = 2;
      } break;
      case 7: {
        field = FieldReportSize;
      } break;
      case 8: {
        field = FieldReportId;
      } break;
      case 9: {
        field = FieldReportCount;
      } break;
      }
    } break;
    case 2: {
      switch (cmd >> 4) {
      case 0: {
        cmdtype = 1;
      } break;
      }
    } break;
    }
    uint32_t data = 0;
    for (uint32_t mi = 0; mi != datalen; ++mi) {
      data |= ((uint32_t)hid[mi + i]) << (mi << 3);
    }

    switch (cmdtype) {
    case 1: // usage
    {
      usage = data;
      int32_t usagename = getUsageName(usagepage, usage);
      if (level == 0) {
        if (usagename == StrId_Mouse) {
          isMouse = 1;
        } else {
          isMouse = 0;
        }
      } else {
        if (isMouse) {
          if (usagename == StrId_X && usageIndexX == 255) {
            usageIndexX = usageIndex;
          } else if (usagename == StrId_Y && usageIndexY == 255) {
            usageIndexY = usageIndex;
          } else if (usagename == StrId_Wheel && usageIndexWheel == 255) {
            usageIndexWheel = usageIndex;
          }
        }
      }
      ++usageIndex;
    } break;
    case 2: // usage page
    {
      usagepage = data;
      int32_t usagepagename = getPageName(usagepage);
      if (isMouse && conf->btnI == 255 && usagepagename == StrId_Page_Button) {
        if (mouseAccum % 8)
          return;
        conf->btnI = mouseAccum / 8;
      }
    } break;
    case 3: {
      if (isMouse) {
        if (field == FieldInput) {
          if (usageIndexX != 255 && conf->xI == 255) {
            uint32_t i8 = mouseAccum + usageIndexX * reportSize;
            if (i8 % 8)
              return;
            if (reportSize != 8 && reportSize != 16)
              return;
            conf->xSize = reportSize / 8;
            conf->xI = i8 / 8;
          }
          if (usageIndexY != 255 && conf->yI == 255) {
            uint32_t i8 = mouseAccum + usageIndexY * reportSize;
            if (i8 % 8)
              return;
            if (reportSize != 8 && reportSize != 16)
              return;
            conf->ySize = reportSize / 8;
            conf->yI = i8 / 8;
          }
          if (usageIndexWheel != 255 && conf->wheelI == 255) {
            uint32_t i8 = mouseAccum + usageIndexWheel * reportSize;
            if (i8 % 8)
              return;
            if (reportSize != 8 && reportSize != 16)
              return;
            conf->wheelSize = reportSize / 8;
            conf->wheelI = i8 / 8;
          }
          mouseAccum += reportCount * reportSize;
          usageIndex = 0;
          usageIndexX = 255;
          usageIndexY = 255;
          usageIndexWheel = 255;
        }
      }
    } break;
    default: {
      if (isMouse) {
        if (field == FieldReportId) {
          mouseAccum += 8;
          conf->isId = 1;
          conf->id = data;
        } else if (field == FieldReportCount) {
          reportCount = data;
        } else if (field == FieldReportSize) {
          reportSize = data;
        }
      }
    } break;
    }
    i += datalen;
  }
}

int parseMouseData(const uint8_t *data, uint32_t dataLen, const MouseConf *conf,
                   int8_t o[4]) {
  const int ok = 0;
  const int err = 1;
  o[0] = o[1] = o[2] = o[3] = 0;
  if (conf->isId && conf->id != data[0]) {
    return err;
  }
  if ((uint32_t)(conf->btnI + 1) > dataLen) {
    return err;
  }
  o[0] = data[conf->btnI];
  for (uint32_t i = 0; i != 3; ++i) {
    uint8_t aI = 0;
    uint8_t aSize = 0;

    switch (i) {
    case 0:
      aI = conf->xI;
      aSize = conf->xSize;
      break;
    case 1:
      aI = conf->yI;
      aSize = conf->ySize;
      break;
    case 2:
      aI = conf->wheelI;
      aSize = conf->wheelSize;
      break;
    }
    if ((uint32_t)aI + (uint32_t)aSize <= dataLen) {
      if (aSize == 1) {
        o[i + 1] = data[aI];
      } else if (aSize == 2) {
        int16_t a =
            (int16_t)(((uint16_t)data[aI]) | (((uint16_t)data[aI + 1]) << 8));
        if (a < -128)
          a = 128;
        if (a > 127)
          a = 127;
        o[i + 1] = a;
      } else {
        return err;
      }
    }
  }
  return ok;
}
