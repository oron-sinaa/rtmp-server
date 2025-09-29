#include <mist/json.h>
#include <mist/sql.h>
#include <sqlite3.h>
#include <unordered_set>

namespace Controller{
  std::string initializeRecordExt();
  void setProcStatus(uint64_t id, const std::string & proc, const std::string & source, const std::string & sink, const JSON::Value & status);
  void getProcsForStream(const std::string & stream, JSON::Value & returnedProcList);
  void procLogMessage(uint64_t id, const JSON::Value & msg);
  bool isProcActive(uint64_t id);
  bool streamsEqual(JSON::Value &one, JSON::Value &two);
  void checkStream(std::string name, JSON::Value &data);
  void CheckAllStreams(JSON::Value &data);
  bool CheckIfStreamsChanged(JSON::Value &copiedData);
  void CheckStreams(JSON::Value &in, JSON::Value &out);
  void AddStreams(JSON::Value &in, JSON::Value &out);
  int deleteStream(const std::string &name, JSON::Value &out, bool sourceFileToo = false);
  void checkParameters(JSON::Value &stream);

   /**
   * \b VISION
   * @brief remove all pushes for \b streamName
   * in \b list
   *
   * @param list list of streams
   * @param streamName streamName of push stream to remove
   */
  void removePushes(JSON::Value &list, const std::string &streamName);

  /**
   * \b VISION
   * @brief push the stream of \b streamName
   * in \b streams
   *
   * @param streams list of streams
   */
  void pushStream(JSON::Value &streamDetail, std::string &streamName);

  /**
   * \b VISION
   * @brief api for getting metadata of all streams in \b res as response
   * \b api = streams_status
  */
  void getStreamData(JSON::Value &res);

  struct liveCheck{
    long long int lastms;
    long long int last_active;
  };

}// namespace Controller
