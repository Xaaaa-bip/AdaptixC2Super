#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void EncryptSessionTransportAES(unsigned char* data, int dataLength, const unsigned char* sessionKey16, const unsigned char* salt16);
void DecryptSessionTransportAES(unsigned char* data, int dataLength, const unsigned char* sessionKey16, const unsigned char* salt16);

#ifdef __cplusplus
}
#endif
