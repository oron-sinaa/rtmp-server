#include <chrono>
#include "output_http.h"
#include "output_ts_base.h"

#define DEFAULT_NVDR_REGULATION_INTERVAL    3600
#define REGULATE_INTERVAL_ENV_KEY           "VISION_NDVR_REGULATE_INTERVAL"
#define M3U8_INDEX_MAX_LINES_KEY            "VISION_NDVR_INDEX_MAX_LINES"
#define M3U8_INDEX_MAX_LINES                24000
#define TS_PACKET_PAYLOAD_SIZE              188

namespace Mist{
  class OutHTTPTS : public TSOutput{
  public:
    OutHTTPTS(Socket::Connection &conn);
    ~OutHTTPTS();
    static void init(Util::Config *cfg);
    static bool listenMode();
    void respondHTTP(const HTTP::Parser & req, bool headersOnly);
    void sendTS(const char *tsData, size_t len = 188);
    void initialSeek();

  private:
    bool isRecording();
    bool isFileTarget(){
      HTTP::URL target(config->getString("target"));
      if (isRecording() && (target.getExt() == "ts" && config->getString("target").substr(0, 8) != "ts-exec:")){return true;}
      return false;
    }
    virtual bool inlineRestartCapable() const{return true;}
    void sendHeader();
    bool onFinish();
    // Location of playlist file which we need to keep updated
    std::string playlistLocation;
    // First segment file name as approximate timestamp of recieving stream
    std::string initialTsFile;
    std::string tsLocation;
    std::string prevTsFile;
    // Subfolder name (based on playlist name) which gets prepended to each entry in the playlist file
    std::string prepend;
    // Defaults to True. When exporting to .m3u8 & TS, it will overwrite the existing playlist file and remove existing .TS files
    bool removeOldPlaylistFiles;
    uint64_t previousStartTime;
    bool addFinalHeader;
    bool isUrlTarget;
    // Determines whether #EXT-X-PLAYLIST-TYPE:"VOD/EVENT"
    bool forceVodPlaylist;
    bool writeFilenameOnly;
    Socket::Connection plsConn;

    /**
     * \b VISION
     * @brief Clear old ts from \b m3u8 file from top
     * till \b VISION_NDVR_REGULATE_INTERVAL is
     * satisfied
     *
     * @param playlistLoc location of manifest file
     *
     * @throws \b std::exception
     */
    void regulateManifest(std::string playlistLoc);

    /**
     * \b VISION
     * @brief Remove old ts names from the vector
     *
     * @param playlist playlist lines
     *
     * @return \b int new beginning Timestamp
     */
    uint32_t removeOldTS(std::vector<std::string> &playlist);

    /**
     * \b VISION
     * @brief Append ts to the manifest file
     *
     * @param playlistLoc playlist location
     * @param ts timestamp line
     * @param duration duration of the ts
     *
     * @throws \b std::exception
     */
    void appendTS(std::string playlistLoc, std::string ts, std::string duration);

    /**
     * \b VISION
     * @brief Get begin timestamp from m3u8 file
     * if @a removeOldPlaylistFiles is not set
     * @note If there is no file, ts is calculated
     * the default way.
     *
     * @param playlistLoc playlist file location
     *
     * @return \b int timestamp value
     * @throws \b std::exception
     */
    uint32_t getBeginTSFromFile(std::string playlistLoc);

    /** \b JARVIS vars */
    uint16_t regulationInterval = DEFAULT_NVDR_REGULATION_INTERVAL;
    uint16_t indexMaxLines = M3U8_INDEX_MAX_LINES;
    std::chrono::time_point<std::chrono::system_clock> beginTimestamp = std::chrono::system_clock::now();
    uint32_t beginTS = 0;
    uint32_t elapsedInterval = 0;
    uint32_t currentTS = 0;
    std::string currentSegmentDuration = "";
    uint64_t currentUnixTime;
    uint64_t prevUnixTime;
    // uint64_t systemBoot;
    // uint64_t currentStartTime;
    Util::ResizeablePointer packetBuffer;
  };
}// namespace Mist

typedef Mist::OutHTTPTS mistOut;
