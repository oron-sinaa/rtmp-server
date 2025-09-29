#pragma once
#include <deque>
#include <map>
#include <set>

#include "bitstream.h"
#include "mp4_generic.h"
#include "nal.h"

namespace h265{
  std::deque<nalu::nalData> analysePackets(const char *data, unsigned long len);

  const char *typeToStr(uint8_t type);
  bool isKeyframe(const char *data, uint32_t len);

  void updateProfileTierLevel(Utils::bitstream &bs, MP4::HVCC &hvccBox, unsigned long maxSubLayersMinus1);
  std::string printProfileTierLevel(Utils::bitstream &bs, unsigned long maxSubLayersMinus1, size_t indent);

  struct metaInfo{
    uint64_t width;
    uint64_t height;
    double fps;
    uint8_t general_profile_space;
    bool general_tier_flag;
    uint8_t general_profile_idc;
    uint32_t general_profile_compatflags;
    uint8_t constraint_flags[6];
    uint8_t general_level_idc;
  };

  class initData{
  public:
    initData();
    initData(const std::string &hvccData);
    void addUnit(const char *data);
    void addUnit(const std::string &data);
    bool haveRequired();
    std::string generateHVCC();
    metaInfo getMeta();
    const std::set<std::string> &getVPS() const;
    const std::set<std::string> &getSPS() const;
    const std::set<std::string> &getPPS() const;

  protected:
    std::map<unsigned int, std::set<std::string> > nalUnits;
  };

  class vpsUnit{
  public:
    vpsUnit(const std::string &_data);
    void updateHVCC(MP4::HVCC &hvccBox);
    std::string toPrettyString(size_t indent);

  private:
    std::string data;
  };

  class spsUnit{
  public:
    spsUnit(const std::string &_data);
    void updateHVCC(MP4::HVCC &hvccBox);
    std::string toPrettyString(size_t indent = 0);
    void getMeta(metaInfo &res);

  private:
    std::string data;
  };

  // NOTE: no ppsUnit, as the only information it contains is parallelism mode, which can be set to
  // 0 for 'unknown'
}// namespace h265
