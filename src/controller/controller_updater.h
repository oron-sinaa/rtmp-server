/// \file controller_updater.h
/// Contains all code for the controller updater.

#include <mist/json.h>
#include <mist/socket.h>
#include <string>

namespace Controller{
  void updateThread(void *np);
  JSON::Value checkUpdateInfo();
  void checkUpdates();
  void insertUpdateInfo(JSON::Value &ret);
}// namespace Controller
