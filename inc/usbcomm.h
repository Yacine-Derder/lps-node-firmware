#ifndef __USB_COMM_H__
#define __USB_COMM_H

#include <stdbool.h>
#include <stdint.h>

uint32_t usbcommReceiveGeneration(void);
int usbcommRead(char* buffer, size_t len);

bool usbcommTryWrite(const char *data,unsigned len);

void usbcommWrite(char const *data, int len);

void usbcommSetSystemStarted(bool init);

void usbcommPrintWelcomeMessage();

#endif // __USB_COMM_H__
