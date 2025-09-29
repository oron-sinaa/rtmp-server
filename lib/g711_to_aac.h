#include <vector>
#include <faac.h>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <cstring>
#include "util.h"
#include "defines.h"

namespace AACConverter {
  class AACConverter {
    private:
      faacEncHandle faac;
      bool isInitialized;
      int16_t* pcmBuffer;
      uint32_t pcmBufferSize;
      uint16_t frameDuration;
      bool first;
    public:
      AACConverter(uint32_t inSampleRate, uint16_t inChannels, uint16_t outBitrate, uint32_t outBandwidth);
      ~AACConverter();
      unsigned long numInputSamples;
      uint64_t frameMsTime;
      unsigned long maxOutAacBytes;
      void transcode(std::string inCodec, const char* inData, uint32_t inSize, unsigned char* &outAac, int32_t &outAacSize);
  };
}
