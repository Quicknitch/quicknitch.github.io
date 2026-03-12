/**
 * ota.h
 */
#ifndef OTA_H
#define OTA_H
#include "quicknitch.h"

void ota_init(void);
void ota_process_i2c_cmd(const uint8_t *buf, uint8_t len);
bool ota_is_active(void);

#endif /* OTA_H */
