#include <vector>
#include <lame/lame.h>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include "defines.h"

#define LAME_QUALITY 5  // 0 = best, 9 = worst

namespace MP3Converter{
  class MP3Converter{
    private:
      lame_t lame;
      bool isInitialized;
      std::vector<char> mp3Buffer;

    public:
      MP3Converter(uint32_t inSampleRate, uint16_t inChannels, uint16_t outBitrate);
      ~MP3Converter();
      char* toMemory(std::string filePath, size_t *fileSize);
      std::vector<char> transcode(std::string inCodec, const char* inData, int inSize);
      int finalize(char* finalBuffer, int bufferSize);
      int getRemainingBytes();
  };
}// namespace MP3Converter