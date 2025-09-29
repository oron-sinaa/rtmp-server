#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <cstdlib>
#include <cstring>
#include <math.h> //for log

#include "bitfields.h"
#include "bitstream.h"
#include "defines.h"
#include "nal.h"

namespace nalu{
  std::deque<int> parseNalSizes(DTSC::Packet &pack){
    std::deque<int> result;
    char *data;
    size_t dataLen;
    pack.getString("data", data, dataLen);
    int offset = 0;
    while (offset < dataLen){
      int nalSize = Bit::btohl(data + offset);
      result.push_back(nalSize + 4);
      offset += nalSize + 4;
    }
    return result;
  }

  std::string removeEmulationPrevention(const std::string &data){
    std::string result;
    result.resize(data.size());
    result[0] = data[0];
    result[1] = data[1];
    size_t dataPtr = 2;
    size_t dataLen = data.size();
    size_t resPtr = 2;
    while (dataPtr + 2 < dataLen){
      if (!data[dataPtr] && !data[dataPtr + 1] && data[dataPtr + 2] == 3){// We have found an emulation prevention
        result[resPtr++] = data[dataPtr++];
        result[resPtr++] = data[dataPtr++];
        dataPtr++; // Skip the emulation prevention byte
      }else{
        result[resPtr++] = data[dataPtr++];
      }
    }

    while (dataPtr < dataLen){result[resPtr++] = data[dataPtr++];}
    return result.substr(0, resPtr);
  }

  unsigned long toAnnexB(const char *data, unsigned long dataSize, char *&result){
    // toAnnexB keeps the same size.
    if (!result){result = (char *)malloc(dataSize);}
    int offset = 0;
    while (offset < dataSize){
      // Read unit size
      unsigned long unitSize = Bit::btohl(data + offset);
      // Write annex b header
      memset(result + offset, 0x00, 3);
      result[offset + 3] = 0x01;
      // Copy the nal unit
      memcpy(result + offset + 4, data + offset + 4, unitSize);
      // Update the offset
      offset += 4 + unitSize;
    }
    return dataSize;
  }

  /// Scans data for the last non-zero byte, returning a pointer to it.
  const char *nalEndPosition(const char *data, uint32_t dataSize){
    while (dataSize && !data[dataSize - 1]){--dataSize;}
    return data + dataSize;
  }

  /// Scan data for Annex B start code. Returns pointer to it when found, null otherwise.
  const char *scanAnnexB(const char *data, uint32_t dataSize){
    char *offset = (char *)data;
    const char *maxData = data + dataSize - 2;
    while (offset < maxData){
      if (offset[2] > 1){
        // We have no zero in the third byte, so we need to skip at least 3 bytes forward
        offset += 3;
        continue;
      }
      if (!offset[2]){
        // We COULD skip forward 1 or 2 bytes depending on contents of the second byte
        // offset += (offset[1]?2:1);
        //... but skipping a single byte (removing the 'if') is actually faster (benchmarked).
        ++offset;
        continue;
      }
      if (!offset[0] && !offset[1]){return offset;}
      // We have no zero in the third byte, so we need to skip at least 3 bytes forward
      offset += 3;
    }
    return 0;
  }

  unsigned long fromAnnexB(const char *data, unsigned long dataSize, char *&result){
    const char *lastCheck = data + dataSize - 3;
    if (!result){
      FAIL_MSG("No output buffer given to FromAnnexB");
      return 0;
    }
    int offset = 0;
    int newOffset = 0;
    while (offset < dataSize){
      const char *begin = data + offset;
      while (begin < lastCheck && !(!begin[0] && !begin[1] && begin[2] == 0x01)){
        begin++;
        if (begin < lastCheck && begin[0]){begin++;}
      }
      begin += 3; // Initialize begin after the first 0x000001 pattern.
      if (begin > data + dataSize){
        offset = dataSize;
        continue;
      }
      const char *end = (const char *)memmem(begin, dataSize - (begin - data), "\000\000\001", 3);
      if (!end){end = data + dataSize;}
      // Check for 4-byte lead in's. Yes, we access -1 here
      if (end > begin && (end - data) != dataSize && end[-1] == 0x00){end--;}
      unsigned int nalSize = end - begin;
      Bit::htobl(result + newOffset, nalSize);
      memcpy(result + newOffset + 4, begin, nalSize);

      newOffset += 4 + nalSize;
      offset = end - data;
    }
    return newOffset;
  }
}// namespace nalu
