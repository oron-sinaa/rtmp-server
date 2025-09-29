#include <mist/config.h>
#include <mist/json.h>
#include <mist/tinythread.h>
#include <string>

namespace Controller{
  // Functions for current pushes, start/stop/list
  void startPush(const std::string &streamname, std::string &target);
  void stopPush(unsigned int ID);
  void listPush(JSON::Value &output);
  void pushLogMessage(uint64_t id, const JSON::Value & msg);
  void setPushStatus(uint64_t id, const JSON::Value & status);
  bool isPushActive(uint64_t id);

  // Functions for automated pushes, add/remove
  void addPush(JSON::Value &request);
  void removePush(const JSON::Value &request, JSON::Value &response);
  void removeAllPush(const std::string &streamname);

  // internal use only
  void removePush(const JSON::Value &request);
  void doAutoPush(std::string &streamname);
  void readPushList();
  void pushCheckLoop(void *np);
  bool isPushActive(const std::string &streamname, const std::string &target);
  void stopActivePushes(const std::string &streamname, const std::string &target);

  // for storing/retrieving settings
  void listPush(JSON::Value &output, bool listOnly);
  void pushSettings(const JSON::Value &request, JSON::Value &response);
}// namespace Controller
