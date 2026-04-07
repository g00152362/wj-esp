#pragma once

#include <string>

bool SdCardInit();
bool SdCardHasGifs();
const char* SdCardGetGifPath(const char* emotion);
void SdCardRegisterLvglFs();
