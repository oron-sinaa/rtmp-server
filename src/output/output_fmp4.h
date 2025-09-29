#define TAG_SEQ_DISCONTINUITY "#EXT-X-DISCONTINUITY-SEQUENCE"
#define TAG_DISCONTINUITY "#EXT-X-DISCONTINUITY"
#define TAG_MAP_URI "#EXT-X-MAP:URI="
#define TAG_EXTINF "#EXTINF:"
#define MAX_BUFFER_BYTES 52428800
#define MAX_DATA_WAIT_SEC 30
#include <list>
#include <fstream>
#include <inttypes.h>
#include <mist/nal.h>
#include <mist/mp4.h>
#include <mist/defines.h>
#include <mist/bitfields.h>
#include <mist/mp4_generic.h>
#include <mist/mp4_dash.h>
#include "output_http.h"

namespace Mist{
  class OutfMP4 : public HTTPOutput{
    public:
      OutfMP4(Socket::Connection &conn);
      ~OutfMP4();
      static void init(Util::Config *cfg);
      bool isFileTarget();
      void sendNext();
      void sendHeader();
      void sendMoov(size_t trackID);
      void sendMoof(uint64_t startFragmentTime, uint64_t endFragmentTime, uint32_t mdatSize, size_t trackID);
      uint64_t mp4moofSize(uint64_t firstPartIdx, uint64_t lastPartIdx, size_t trackID);
      virtual bool onFinish();
    protected:
      std::string playlistLocation;
      size_t videoFragSeqNum;       // the sequence number of the next video fragment when producing fragmented MP4's
      size_t audioFragSeqNum;       // the sequence number of the next audio fragment when producing fragmented MP4's
      size_t vidTrack;         // the video track we use as fragmenting base
      Util::ResizeablePointer audioMdatBuffer;
      Util::ResizeablePointer videoMdatBuffer;
      uint32_t videoMdatBufferSize;
      uint32_t audioMdatBufferSize;
      uint64_t splitTime;
      std::string audioSegmentPrefix;
      std::string videoSegmentPrefix;
      bool setFileName;
      size_t videoTrackID;
      size_t audioTrackID;
      uint64_t thisPktTime;
      uint64_t startPktTime;
      uint64_t regulationInterval;
      uint64_t indexMaxLines;
      uint32_t currentFmp4Epoch;
      uint64_t prevRegulateSec;
      uint64_t prevFileTime;
      std::string thisFmp4Location;
      std::string thisInitLocation;
      std::string lastInitTime;
      std::string lastInitLine;
      std::string bootInitLine;
      uint64_t startAudioPktTime;
      uint64_t prevAudioPktTime;
      uint64_t prevVideoPktTime;
      uint64_t prevVGetTime;
      uint64_t prevAGetTime;
      // mp4MoofSize() variables
      size_t videoPrevEndPart;
      size_t audioPrevEndPart;
      bool audioFirstRun;
      bool videoFirstRun;
      size_t thisPktSize;
      std::string currentUnixTime;
      uint64_t firstKeyPktTime;
      uint64_t bootTime;
    private:
      void writeManifest(const std::string &targetLocation, const std::string &line, bool overwrite = false);
      void regulateManifest(std::string playlistLoc);
      bool switchFile(std::string &filePath);
      uint32_t removeOldFmp4(std::vector<std::string> &playlist);
      void sendFirst();
      void sendInit();
      void bufferData(const std::string &trackType);
      void flushVideo();
      void flushAudio();
      void performPublish(uint64_t currentFileTime, uint64_t pktInterval);
      void performRegulate();
  };

}// namespace Mist

typedef Mist::OutfMP4 mistOut;