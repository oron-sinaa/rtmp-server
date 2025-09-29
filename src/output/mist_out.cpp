#include OUTPUTTYPE
#include <mist/config.h>
#include <mist/defines.h>
#include <mist/socket.h>
#include <mist/util.h>
#include <mist/stream.h>

int spawnForked(Socket::Connection &S){
  {
    struct sigaction new_action;
    new_action.sa_handler = SIG_IGN;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;
    sigaction(SIGUSR1, &new_action, NULL);
  }
  mistOut tmp(S);
  return tmp.run();
}

/// Checks in the server configuration if this stream is set to always on or not.
/// Returns true if it is, or if the stream could not be found in the configuration.
static bool isAlwaysOn(std::string &streamName){
  JSON::Value streamCfg = Util::getStreamConfig(streamName);
  // Return true if source is push yet the always_on option is present, for some reason?
  if (streamCfg.isMember("source") && streamCfg["source"].asString().substr(0, 7) == "push://"){
    return true;
  }
  // Return true if always_on key is not even present
  if (!streamCfg.isMember("always_on")){
    return true;
  }
  // Return false if always_on key is present and set to false
  if (!streamCfg["always_on"].asBool()){
    return false;
  }
  return true;
}

int main(int argc, char *argv[]){
  DTSC::trackValidMask = TRACK_VALID_EXT_HUMAN;
  Util::redirectLogsIfNeeded();
  Util::Config conf(argv[0]);
  mistOut::init(&conf);
  if (conf.parseArgs(argc, argv)){
    if (conf.getBool("json")){
      mistOut::capa["version"] = PACKAGE_VERSION;
      std::cout << mistOut::capa.toString() << std::endl;
      return -1;
    }
    {
      std::string defTrkSrt = conf.getString("default_track_sorting");
      if (!defTrkSrt.size()){
        defTrkSrt = Util::getGlobalConfig("default_track_sorting").asString();
      }
      if (defTrkSrt.size()){
        if (defTrkSrt == "bps_lth"){Util::defaultTrackSortOrder = Util::TRKSORT_BPS_LTH;}
        if (defTrkSrt == "bps_htl"){Util::defaultTrackSortOrder = Util::TRKSORT_BPS_HTL;}
        if (defTrkSrt == "id_lth"){Util::defaultTrackSortOrder = Util::TRKSORT_ID_LTH;}
        if (defTrkSrt == "id_htl"){Util::defaultTrackSortOrder = Util::TRKSORT_ID_HTL;}
        if (defTrkSrt == "res_lth"){Util::defaultTrackSortOrder = Util::TRKSORT_RES_LTH;}
        if (defTrkSrt == "res_htl"){Util::defaultTrackSortOrder = Util::TRKSORT_RES_HTL;}
      }
    }
    conf.activate();
    if (mistOut::listenMode()){
      mistOut::listener(conf, spawnForked);
      if (Socket::checkTrueSocket(0)){
        INFO_MSG("Reloading input while re-using server socket");
        execvp(argv[0], argv);
        FAIL_MSG("Error reloading: %s", strerror(errno));
      }
    }else{
      std::string streamName;
      if (conf.hasOption("stream")){
        streamName = conf.getString("stream");
      }else if (conf.hasOption("s")){
        streamName = conf.getString("s");
      }
      if (streamName.size() && !isAlwaysOn(streamName)){
        FAIL_MSG("Denying attempt to open a non-always-on stream");
        return 0;
      }
      Socket::Connection S(fileno(stdout), fileno(stdin));
      mistOut tmp(S);
      return tmp.run();
    }
  }
  INFO_MSG("Exit reason: %s", Util::exitReason);
  return 0;
}

