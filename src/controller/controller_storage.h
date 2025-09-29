#include <mist/config.h>
#include <mist/json.h>
#include <mist/tinythread.h>
#include <mist/util.h>
#include <string>

namespace Controller{
  extern std::string instanceId; ///< global storage of instanceId (previously uniqID) is set in controller.cpp
  extern std::string prometheus;     ///< Prometheus access string
  extern std::string accesslog;      ///< Where to write the access log
  extern Util::Config conf;          ///< Global storage of configuration.
  extern JSON::Value Storage;        ///< Global storage of data.
  extern tthread::mutex logMutex;    ///< Mutex for log thread.
  extern tthread::mutex configMutex; ///< Mutex for server config access.
  extern bool configChanged;         ///< Bool that indicates config must be written to SHM.
  extern bool isTerminal;            ///< True if connected to a terminal and not a log file.
  extern bool isColorized;           ///< True if we colorize the output
  extern uint64_t logCounter;        ///< Count of logged messages since boot
  extern uint64_t systemBoot;        ///< Unix time in milliseconds of system boot
  extern uint64_t lastBootTime;      ///< Unix time in milliseconds of last system boot/reboot
  extern bool isAgentMode;           ///< Global switch to check if running in agent mode

  Util::RelAccX *logAccessor();
  Util::RelAccX *accesslogAccessor();
  Util::RelAccX *streamsAccessor();

  void setAgentMode();

  /// Store and print a log message.
  void Log(const std::string &kind, const std::string &message, const std::string &stream = "", uint64_t progPid = 0,
           bool noWriteToLog = false);
  void logAccess(const std::string &sessId, const std::string &strm, const std::string &conn,
                 const std::string &host, uint64_t duration, uint64_t up, uint64_t down,
                 const std::string &tags);

  void normalizeTrustedProxies(JSON::Value &tp);

  /// Write contents to Filename.
  bool WriteFile(std::string Filename, std::string contents);
  void writeConfigToDisk();

  void handleMsg(void *err);
  void initState();
  void deinitState(bool leaveBehind);
  void writeConfig();
  void writeStream(const std::string &sName, const JSON::Value &sConf);
  void writeCapabilities();
  void writeProtocols();
  void sleepInSteps(int seconds);
  bool hasEnoughFreeSpace(const std::string path, double requiredPercentage);

}// namespace Controller
