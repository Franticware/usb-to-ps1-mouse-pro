#ifndef PARSEMOUSE_H
#define PARSEMOUSE_H

#include <stdint.h>

typedef struct {
  uint8_t isId;
  uint8_t id;
  uint8_t btnI;
  uint8_t xI;
  uint8_t xSize; // 1 or 2
  uint8_t yI;
  uint8_t ySize;
  uint8_t wheelI;
  uint8_t wheelSize;
} MouseConf;

void parseMouseDescr(const volatile uint8_t *descr, uint32_t descrLen,
                     MouseConf *conf);
int parseMouseData(const uint8_t *data, uint32_t dataLen, const MouseConf *conf,
                   int8_t o[4]);

#endif // PARSEMOUSE_H
