#pragma once
#include <cmath>
#include <deque>
#include <stdint.h>
#include <string>
#include <sys/types.h>

namespace vorbis{
  struct mode{
    bool blockFlag;
    unsigned short windowType;
    unsigned short transformType;
    char mapping;
  };

  inline unsigned int ilog(unsigned int input){return (std::log(input)) / (std::log(2)) + 1;}

  bool isHeader(const char *newData, unsigned int length);
  class header{
  public:
    ~header();
    header(char *newData, unsigned int length);
    int getHeaderType();
    long unsigned int getVorbisVersion();
    char getAudioChannels();
    long unsigned int getAudioSampleRate();
    long unsigned int getBitrateMaximum();
    long unsigned int getBitrateNominal();
    long unsigned int getBitrateMinimum();
    char getBlockSize0();
    char getBlockSize1();
    char getFramingFlag();
    std::string toPrettyString(size_t indent = 0);
    std::deque<mode> readModeDeque(char audioChannels);
    bool isHeader();
    unsigned int getDataSize(){return datasize;}

  protected:
    uint32_t getInt32(size_t index);
    uint32_t getInt24(size_t index);
    uint16_t getInt16(size_t index);

  private:
    std::deque<mode> modes;
    char *data;
    unsigned int datasize;
    bool checkDataSize(unsigned int size);
    bool validate();
  };
}// namespace vorbis
