#include "controller_capabilities.h"
#include "controller_limits.h" /*LTS*/
#include "controller_statistics.h"
#include "controller_storage.h"
#include "controller_push.h"
#include "controller_api.h"
#include "controller_streams.h"
#include "controller_license.h"
#include <mist/timing.h>
#include <map>
#include <mist/config.h>
#include <mist/defines.h>
#include <mist/dtsc.h>
#include <mist/procs.h>
#include <mist/shared_memory.h>
#include <mist/stream.h>
#include <mist/timing.h>
#include <mist/triggers.h> //LTS
#include <sys/stat.h>
#include <mist/dtsc.h>
#include "../io.h"
#include <future>
#include <mist/sql.h>
#include <sqlite3.h>

///\brief Holds everything unique to the controller.
namespace Controller{
  std::string recordExt = "m3u8";
  std::string initializeRecordExt(){
    char *recordExtEnv = getenv("RECORD_EXT");
    if (recordExtEnv){
      try{
        std::string recordExtStr = std::string(recordExtEnv);
        if (!recordExtStr.empty()){
          recordExt = recordExtStr;
        }
      }catch (const std::exception &ex){
        recordExt = "m3u8";
        ERROR_MSG("[RTMPServer] Cannot set record mode from env: '%s'", ex.what());
      }
    }else{
      INFO_MSG("[RTMPServer] env RECORD_EXT is not set.");
    }
    if (recordExt != "m3u" && recordExt != "m3u8"){
      ERROR_MSG("[RTMPServer] Invalid RECORD_EXT env provided");
      recordExt = "m3u8";
    }
    return recordExt;
  }

  std::map<std::string, pid_t> inputProcesses;

  /// Internal list of currently active processes
  class procInfo{
    public:
    JSON::Value stats;
    std::string source;
    std::string proc;
    std::string sink;
    uint64_t lastupdate;
    JSON::Value logs;
  };
  std::map<pid_t, procInfo> activeProcs;
  tthread::recursive_mutex procMutex;

  void procLogMessage(uint64_t id, const JSON::Value & msg){
    tthread::lock_guard<tthread::recursive_mutex> procGuard(procMutex);
    JSON::Value &log = activeProcs[id].logs;
    log.append(msg);
    log.shrink(25);
  }

  bool isProcActive(uint64_t id){
    tthread::lock_guard<tthread::recursive_mutex> procGuard(procMutex);
    return activeProcs.count(id);
  }


  void getProcsForStream(const std::string & stream, JSON::Value & returnedProcList){
    tthread::lock_guard<tthread::recursive_mutex> procGuard(procMutex);
    std::set<pid_t> wipeList;
    for (std::map<pid_t, procInfo>::iterator it = activeProcs.begin(); it != activeProcs.end(); ++it){
      if (!stream.size() || stream == it->second.sink || stream == it->second.source){
        JSON::Value & thisProc = returnedProcList[JSON::Value(it->first).asString()];
        thisProc = it->second.stats;
        thisProc["source"] = it->second.source;
        thisProc["sink"] = it->second.sink;
        thisProc["process"] = it->second.proc;
        thisProc["logs"] = it->second.logs;
        if (!Util::Procs::isRunning(it->first)){
          thisProc["terminated"] = true;
          wipeList.insert(it->first);
        }
      }
    }
    while (wipeList.size()){
      activeProcs.erase(*wipeList.begin());
      wipeList.erase(wipeList.begin());
    }
  }

  void setProcStatus(uint64_t id, const std::string & proc, const std::string & source, const std::string & sink, const JSON::Value & status){
    tthread::lock_guard<tthread::recursive_mutex> procGuard(procMutex);
    procInfo & prc = activeProcs[id];
    prc.lastupdate = Util::bootSecs();
    prc.stats.extend(status);
    if (!prc.proc.size() && sink.size() && source.size() && proc.size()){
      prc.sink = sink;
      prc.source = source;
      prc.proc = proc;
    }
  }

  ///\brief Checks whether two streams are equal.
  ///\param one The first stream for the comparison.
  ///\param two The second stream for the comparison.
  ///\return True if the streams are equal, false otherwise.
  bool streamsEqual(JSON::Value &one, JSON::Value &two){
    if (one.isMember("source") != two.isMember("source") || one["source"] != two["source"]){
      return false;
    }

    /// \todo Change this to use capabilities["inputs"] and only compare required/optional
    /// parameters. \todo Maybe change this to check for correct source and/or required parameters.

    // temporary: compare the two JSON::Value objects.
    return one == two;

    // nothing different? return true by default
    // return true;
  }

  ///\brief Checks the validity of a stream, updates internal stream status.
  ///\param name The name of the stream
  ///\param data The corresponding configuration values.
  void checkStream(std::string name, JSON::Value &data){
    if (!data.isMember("name")){data["name"] = name;}
    std::string prevState = data["error"].asStringRef();
    data["online"] = (std::string) "Checking...";
    data.removeMember("error");
    data.removeNullMembers();
    switch (Util::getStreamStatus(name)){
    case STRMSTAT_OFF:
      // Do nothing
      break;
    case STRMSTAT_INIT:
      data["online"] = 2;
      data["error"] = "Initializing...";
      return;
    case STRMSTAT_BOOT:
      data["online"] = 2;
      data["error"] = "Loading...";
      return;
    case STRMSTAT_WAIT:
      data["online"] = 2;
      data["error"] = "Waiting for data...";
      return;
    case STRMSTAT_READY: data["online"] = 1; return;
    case STRMSTAT_SHUTDOWN:
      data["online"] = 2;
      data["error"] = "Shutting down...";
      return;
    default:
      // Unknown state?
      data["error"] = "Unrecognized stream state";
      break;
    }
    data["online"] = 0;
    std::string URL;
    if (data.isMember("channel") && data["channel"].isMember("URL")){
      URL = data["channel"]["URL"].asString();
    }
    if (data.isMember("source")){URL = data["source"].asString();}
    if (!URL.size()){
      data["error"] = "PAUSED";
      if (data["error"].asStringRef() != prevState){
        Log("STRM", "Stream " + name + " Paused.");
      }
      return;
    }
    // Old style always on
    if (data.isMember("udpport") && data["udpport"].asStringRef().size() &&
        (!inputProcesses.count(name) || !Util::Procs::isRunning(inputProcesses[name]))){
      const std::string &udpPort = data["udpport"].asStringRef();
      const std::string &multicast = data["multicastinterface"].asStringRef();
      URL = "tsudp://" + udpPort;
      if (multicast.size()){URL.append("/" + multicast);}
      //  False: start TS input
      INFO_MSG("No TS input for stream %s, starting it: %s", name.c_str(), URL.c_str());
      std::deque<std::string> command;
      command.push_back(Util::getMyPath() + "MistInTS");
      command.push_back("-s");
      command.push_back(name);
      command.push_back(URL);
      int stdIn = 0;
      int stdOut = 1;
      int stdErr = 2;
      pid_t program = Util::Procs::StartPiped(command, &stdIn, &stdOut, &stdErr);
      if (program){inputProcesses[name] = program;}
    }
    // new style always on
    if (data.isMember("always_on") && data["always_on"].asBool() &&
        (!inputProcesses.count(name) || !Util::Procs::isRunning(inputProcesses[name]))){
      INFO_MSG("Starting always-on input %s: %s", name.c_str(), URL.c_str());
      std::map<std::string, std::string> empty_overrides;
      pid_t program = 0;
      Util::startInput(name, URL, true, false, empty_overrides, &program);
      if (program){inputProcesses[name] = program;}
    }
    // non-VoD stream
    if (URL.substr(0, 1) != "/"){return;}
    Util::streamVariables(URL, name, "");
    // VoD-style stream
    struct stat fileinfo;
    if (stat(URL.c_str(), &fileinfo) != 0){
      data["error"] = "Stream offline: Not found: " + URL;
      if (data["error"].asStringRef() != prevState){
        Log("BUFF", "Warning for VoD stream " + name + "! File not found: " + URL);
      }
      return;
    }
    if (!data.isMember("error")){data["error"] = "Available";}
    data["online"] = 2;
    return;
  }

  ///\brief Checks all streams, restoring if needed.
  void CheckAllStreams(JSON::Value &data){
    // 1) Snapshot under lock
    JSON::Value copiedData;
    {
      tthread::lock_guard<tthread::mutex> guard(Controller::configMutex);
      copiedData = data;
    }

    // 2) First‐run structural check
    static bool firstRun = true;
    if (firstRun){
      firstRun = false;
      CheckIfStreamsChanged(copiedData);
    }

    // 3) Do status‐checks on the snapshot
    jsonForEach(copiedData, jit){
      // Return early if controller has stopped and is probably requesting monitorThread.join()
      if (!Controller::conf.is_active){return;}
      checkStream(jit.key(), (*jit));
    }

    // 4) If structure changed, bail out (don’t merge)
    if (CheckIfStreamsChanged(copiedData)){
      WARN_MSG("[RTMPServer] Streams config changed! Refreshing status in a while...");
      return;
    }

    // 5) Merge *only* the "online" field back into the live tree
    {
      tthread::lock_guard<tthread::mutex> guard(Controller::configMutex);
      jsonForEach(copiedData, jit){
        const std::string name = jit.key();
        if (!data.isMember(name)){
          // skip streams that could've been deleted by API
          continue;
        }
        if ((*jit).isMember("online")){
          data[name]["online"] = (*jit)["online"];
        }
      }
    }
  }

  ///\param streamData The Storage["streams"] for the server
  ///\return True if any stream's config was changed
  bool CheckIfStreamsChanged(JSON::Value &streamData){
    // check for changes in streams
    static JSON::Value cachedStreamData;

    if (!cachedStreamData.size()){
      cachedStreamData = streamData;
      return false;
    }

    if (cachedStreamData.size()){
      // Comparing SIZE of Storage["streams"] with cached streams
      if (cachedStreamData.size() != streamData.size()){
        WARN_MSG("[RTMPServer] Streams count changed");
        cachedStreamData = streamData;
        return true;
      }

      // Comparing SOURCES of Storage["streams"] with cached streams
      std::unordered_set<std::string> cachedStreamNames;
      jsonForEach(cachedStreamData, jit){
        std::string streamName = jit.key();
        cachedStreamNames.insert(streamName);

        std::string streamSource = std::string((*jit)["source"]);
        if (streamSource != std::string(streamData[streamName]["source"])){
          WARN_MSG("[RTMPServer] A Stream source did not match the cached set");
          cachedStreamData = streamData;
          return true;
        }
      }

      // Comparing NAMES of Storage["streams"] with cached streams
      std::unordered_set<std::string> storageStreamNames;
      jsonForEach(streamData, it){
        storageStreamNames.insert(it.key());
      }

      if (cachedStreamNames != storageStreamNames){
        WARN_MSG("[RTMPServer] A stream name did not match the cached set");
        cachedStreamData = streamData;
        return true;
      }
    }
    // cache the current streams config to compare later
    return false;
  }

  ///
  /// \triggers
  /// The `"STREAM_ADD"` trigger is stream-specific, and is ran whenever a new stream is added to
  /// the server configuration. If cancelled, the stream is not added. Its payload is:
  /// ~~~~~~~~~~~~~~~
  /// streamname
  /// configuration in JSON format
  /// ~~~~~~~~~~~~~~~
  /// The `"STREAM_CONFIG"` trigger is stream-specific, and is ran whenever a stream's configuration
  /// is changed. If cancelled, the configuration is not changed. Its payload is:
  /// ~~~~~~~~~~~~~~~
  /// streamname
  /// configuration in JSON format
  /// ~~~~~~~~~~~~~~~
  ///
  void AddStreams(JSON::Value &in, JSON::Value &out){
    ///\param totalStreamCount is storing the number of total current active streams
    int totalStreamCount = Storage["streams"].size();
    int prevStreamCount = Storage["streams"].size();
    JSON::Value tmpStreams = Controller::Storage["streams"];
    /// open database
    sqlite3 *db = nullptr;
    if (Controller::conf.trafficConsumption){
      bool isDbOpen = Database::loadDatabase(db);
      if (!isDbOpen){
        FAIL_MSG("[RTMPServer] Could not open SQL DB to add streams");
        Controller::conf.trafficConsumption = false;
      }
    }
    // check for new streams and updates
    jsonForEach(in, jit){
      // make target
      std::string streamName = std::string(in[jit.key()]["name"]);
      if (streamName.size() <= 0){
        streamName = std::string(jit.key());
      }
      std::string target = std::string("/ndvr/") + streamName + std::string("/index.") + recordExt;
      JSON::Value streamDetail;
      streamDetail["stream"] = streamName;
      streamDetail["target"] = target;

      // add or update existing streams
      if (out.isMember(jit.key())){
        if (!streamsEqual((*jit), out[jit.key()])){
          /*LTS-START*/
          if (Triggers::shouldTrigger("STREAM_CONFIG")){
            std::string payload = jit.key() + "\n" + jit->toString();
            if (!Triggers::doTrigger("STREAM_CONFIG", payload, jit.key())){continue;}
          }
          /*LTS-END*/
          out[jit.key()] = (*jit);
          out[jit.key()].removeNullMembers();
          out[jit.key()]["name"] = jit.key();
          checkParameters(out[jit.key()]);
          Log("STRM", std::string("Updated stream ") + jit.key());
        }
      }else{
        /// [RTMPServer] checking the \b activeLicense as per as vision.lic
        if((totalStreamCount >= (Controller::maximumCameras)) && Controller::maximumCameras != -1){
          Log("STRM", std::string("Stream will not be added further, you exceeded the authorized limit."));
          break;
        }

        std::string checked = jit.key();
        Util::sanitizeName(checked);
        if (checked != jit.key() || !checked.size()){
          if (!checked.size()){
            FAIL_MSG("Invalid stream name '%s'", jit.key().c_str());
          }else{
            FAIL_MSG("Invalid stream name '%s'. Suggested alternative: '%s'", jit.key().c_str(),
                     checked.c_str());
          }
          continue;
        }
        /*LTS-START*/
        if (Triggers::shouldTrigger("STREAM_ADD")){
          std::string payload = jit.key() + "\n" + jit->toString();
          if (!Triggers::doTrigger("STREAM_ADD", payload, jit.key())){continue;}
        }
        /*LTS-END*/
        out[jit.key()] = (*jit);
        out[jit.key()].removeNullMembers();
        out[jit.key()]["name"] = jit.key();
        checkParameters(out[jit.key()]);
        Log("STRM", std::string("New stream ") + jit.key());
        totalStreamCount++;

      }
      Controller::writeStream(jit.key(), out[jit.key()]);

      /** \b [RTMPServer] : ading streams in DB and shared page of traffic only if \b TRAFFIC_CONSUMPTION = ON */
      if (Controller::conf.trafficConsumption){
        // Database::insertIfNotExists(db, jit.key());
        // Append the newStreams in SHM page also for calculating traffic statistics
        if (prevStreamCount < Storage["streams"].size()){
          HIGH_MSG("Stream adding in SHM");
          std::set<std::string> usersList = Controller::getUserPageFields(USER_LIST, "");
          /** \todo fix crash after this when adding first stream **/
          Controller::updateStreamInSHM();
          Controller::addUserInStreamCol("", "", usersList);
        }
      }
      Database::closeDatabase(db);

      /** \b [RTMPServer] : asynchronously add stream pushes to write NDVR if not running in agent mode */
      // if (!Controller::isAgentMode){
      //   std::async(std::launch::async, pushStream, std::ref(streamDetail), std::ref(streamName));
      // }
    }
  }

  void pushStream(JSON::Value &streamDetail, std::string &streamName){
     /* stream pushes */
      size_t foundIndex = streamName.rfind("norec");
      if (foundIndex != std::string::npos && foundIndex == streamName.size() - 5){
        /* Handle, if there is "norec" format */
        Log("STRM", "Stream " + streamName + " is in norec format.");
      }else{
        Controller::addPush(streamDetail);
        Log("STRM", "Stream " + streamName + " added in AutoPush.");
      }
  }

  ///\brief Parse a given stream configuration.
  ///\param in The requested configuration.
  ///\param out The new configuration after parsing.
  ///
  /// \api
  /// `"streams"` requests take the form of:
  /// ~~~~~~~~~~~~~~~{.js}
  ///{
  ///   "streamname_here":{//name of the stream
  ///     "source": "/mnt/media/a.dtsc" //full path to a VoD file, or "push://" followed by the IP
  ///     or hostname of the machine allowed to push live data. Empty means everyone is allowed to
  ///     push live data. "DVR": 30000 //optional. For live streams, indicates the requested minimum
  ///     size of the available DVR buffer in milliseconds.
  ///},
  ///   //the above structure repeated for all configured streams
  ///}
  /// ~~~~~~~~~~~~~~~
  /// and are responded to as:
  /// ~~~~~~~~~~~~~~~{.js}
  ///{
  ///   "streamname_here":{//name of the configured stream
  ///     "error": "Available", //error state, if any. "Available" is a special value for VoD
  ///     streams, indicating it has no current viewers (is not active), but is available for
  ///     activation. "h_meta": 1398113185, //unix time the stream header (if any) was last
  ///     processed for metadata "l_meta": 1398115447, //unix time the stream itself was last
  ///     processed for metadata "meta":{//available metadata for this stream, if any
  ///       "format": "dtsc", //detected media source format
  ///       "tracks":{//list of tracks in this stream
  ///         "audio_AAC_2ch_48000hz_2":{//human-readable track name
  ///           "bps": 16043,
  ///           "channels": 2,
  ///           "codec": "AAC",
  ///           "firstms": 0,
  ///           "init": "\u0011Vå\u0000",
  ///           "lastms": 596480,
  ///           "rate": 48000,
  ///           "size": 16,
  ///           "trackid": 2,
  ///           "type": "audio"
  ///},
  ///         //the above structure repeated for all tracks
  ///},
  ///       "vod": 1 //indicates VoD stream, or "live" to indicated live stream.
  ///},
  ///     "name": "a", //the stream name, guaranteed to be equal to the object name.
  ///     "online": 2, //online state. 0 = error, 1 = active, 2 = inactive.
  ///     "source": "/home/thulinma/a.dtsc" //source for this stream, as configured.
  ///},
  ///   //the above structure repeated for all configured streams
  ///}
  /// ~~~~~~~~~~~~~~~
  /// Through this request, ALL streams must always be configured. To remove a stream, simply leave
  /// it out of the request. To add a stream, simply add it to the request. To edit a stream, simply
  /// edit it in the request. The LTS edition has additional requests that allow per-stream changing
  /// of the configuration.
  void CheckStreams(JSON::Value &in, JSON::Value &out){
    // check for new streams and updates
    AddStreams(in, out);

    // check for deleted streams
    std::set<std::string> toDelete;
    jsonForEach(out, jit){
      if (!in.isMember(jit.key())){toDelete.insert(jit.key());}
    }
    // actually delete the streams
    while (toDelete.size() > 0){
      std::string deleting = *(toDelete.begin());
      deleteStream(deleting, out);
      toDelete.erase(deleting);
    }

    // update old-style configurations to new-style
    jsonForEach(in, jit){
      if (jit->isMember("channel")){
        if (!jit->isMember("source")){(*jit)["source"] = (*jit)["channel"]["URL"];}
        jit->removeMember("channel");
      }
      if (jit->isMember("preset")){jit->removeMember("preset");}
    }
  }

  /// Deletes the stream (name) from the config (out), optionally also deleting the VoD source file if sourceFileToo is true.
  int deleteStream(const std::string &name, JSON::Value &out, bool sourceFileToo){
    int ret = 0;
    if (sourceFileToo){
      std::string cleaned = name;
      Util::sanitizeName(cleaned);
      std::string strmSource;
      if (Util::getStreamStatus(cleaned) != STRMSTAT_OFF){
        DTSC::Meta M(cleaned, false, false);
        if (M && M.getSource().size()){strmSource = M.getSource();}
      }
      if (!strmSource.size()){
        std::string smp = cleaned.substr(0, cleaned.find_first_of("+ "));
        if (out.isMember(smp) && out[smp].isMember("source")){
          strmSource = out[smp]["source"].asStringRef();
        }
      }
      bool noFile = false;
      if (strmSource.size()){
        std::string prevInput;
        while (true){
          std::string oldSrc = strmSource;
          JSON::Value inputCapa = Util::getInputBySource(oldSrc, true);
          if (inputCapa["name"].asStringRef() == prevInput){break;}
          prevInput = inputCapa["name"].asStringRef();
          strmSource = inputCapa["source_file"].asStringRef();
          if (!strmSource.size()){
            noFile = true;
            break;
          }
          Util::streamVariables(strmSource, cleaned, oldSrc);
        }
      }
      if (noFile){
        WARN_MSG("Not deleting source for stream %s, since the stream does not have an unambiguous "
                 "source file.",
                 cleaned.c_str());
      }else{
        Util::streamVariables(strmSource, cleaned);
        if (!strmSource.size()){
          FAIL_MSG("Could not delete source for stream %s: unable to detect stream source URI "
                   "using any method",
                   cleaned.c_str());
        }else{
          if (unlink(strmSource.c_str())){
            FAIL_MSG("Could not delete source %s for %s: %s (%d)", strmSource.c_str(),
                     cleaned.c_str(), strerror(errno), errno);
          }else{
            ++ret;
            Log("STRM", "Deleting source file for stream " + cleaned + ": " + strmSource);
            // Delete dtsh, ignore failures
            if (!unlink((strmSource + ".dtsh").c_str())){++ret;}
          }
        }
      }
    }
    if (!out.isMember(name)){return ret;}
    /*LTS-START*/
    if (Triggers::shouldTrigger("STREAM_REMOVE")){
      if (!Triggers::doTrigger("STREAM_REMOVE", name, name)){return ret;}
    }
    /*LTS-END*/
    Log("STRM", "Deleted stream " + name);
    out.removeMember(name);
    Controller::writeStream(name, JSON::Value()); // Null JSON value = delete
    ++ret;
    ret *= -1;
    if (inputProcesses.count(name)){
      pid_t procId = inputProcesses[name];
      if (Util::Procs::isRunning(procId)){Util::Procs::Stop(procId);}
      inputProcesses.erase(name);
    }

    /**
     * \b [RTMPServer] : remove the streams from automatic push and pushes streams
     */
    JSON::Value list;
    Controller::listPush(list);
    Controller::removePushes(list, name);

    return ret;
  }

  void removePushes(JSON::Value &list, const std::string &name){
    /**
     * Remove the pushed stream and autopushes stream
     * from \b pushedStreams that containing the list of all
     *  streams in autopush
     */
    size_t foundIndex = name.rfind("norec");
    if (foundIndex != std::string::npos && foundIndex == name.size() - 5){
        /* Handle, if there is "norec" format */
    }
    else
    {
        Controller::removePush(name);
        Log("STRM", "Stream " + name + " Remove from Automatic Push.");
    }

    for (JSON::Iter iter(list); iter; ++iter){
      JSON::Value& element = *iter;
      JSON::Value& firstIndex = element[(uint32_t)0];
      JSON::Value& secondIndex = element[(uint32_t)1];
      if(secondIndex.asString() == name){
        Controller::stopPush(firstIndex.asInt());
      }
    }
  }

  bool isMatch(const std::string &source, const std::string &match){
    std::string front = match.substr(0, match.find('*'));
    std::string back = match.substr(match.find('*') + 1);
    // if the length of the source is smaller than the front and back matching parts together, it can never match
    if (source.size() < front.size() + back.size()){return false;}
    return (source.substr(0, front.size()) == front && source.substr(source.size() - back.size()) == back);
  }

  void checkParameters(JSON::Value &streamObj){
    JSON::Value &inpt = Controller::capabilities["inputs"];
    std::string match;
    jsonForEach(inpt, it){
      if ((*it)["source_match"].isArray()){
        jsonForEach((*it)["source_match"], subIt){
          if (isMatch(streamObj["source"].asStringRef(), (*subIt).asStringRef())){
            match = (*it)["name"].asString();
          }
        }
      }
      if ((*it)["source_match"].isString()){
        if (isMatch(streamObj["source"].asStringRef(), (*it)["source_match"].asStringRef())){
          match = (*it)["name"].asString();
        }
      }
    }
    if (match != ""){
      jsonForEach(inpt[match]["hardcoded"], it){streamObj[it.key()] = *it;}
    }
  }

  void getStreamData(JSON::Value &res){
    JSON::Value streams = Controller::Storage["streams"];
    std::string resolution, bitrate, status, codec, fps;
    jsonForEach(streams, jit){
      bool isTrackPresent = false;
      res[jit.key()]["metadata"].null();
      JSON::Value streamsData, metaData;
      // creating template
      metaData["audio"]["bps"].null();
      metaData["audio"]["codec"].null();
      metaData["video"]["bps"].null();
      metaData["video"]["codec"].null();
      metaData["video"]["fpks"].null();
      metaData["video"]["height"].null();
      metaData["video"]["width"].null();
      if (std::string(streams[jit.key()]["online"]) == "1"){
        DTSC::Meta M(jit.key(), false, false);
        if (M){
          // getting the data
          M.toJSON(streamsData, true);
          // finding tracks
          JSON::Value tracks = streamsData["tracks"];
          jsonForEach(tracks, jit2){
            std::string key = jit2.key();
            if (key.substr(0,5) == "audio"){
              if (metaData["audio"]["codec"].isNull()){
                metaData["audio"]["bps"] = streamsData["tracks"][key]["bps"];
                metaData["audio"]["codec"] = streamsData["tracks"][key]["codec"];
              }
              if (!metaData["audio"]["codec"].isNull() && streamsData["tracks"][key]["codec"] == "AAC"){
                metaData["audio"]["bps"] = streamsData["tracks"][key]["bps"];
                metaData["audio"]["codec"] = streamsData["tracks"][key]["codec"];
              }
              isTrackPresent = true;
            }
            if (key.substr(0,5) == "video"){
              metaData["video"]["bps"] = streamsData["tracks"][key]["bps"];
              metaData["video"]["codec"] = streamsData["tracks"][key]["codec"];
              metaData["video"]["fpks"] = streamsData["tracks"][key]["fpks"];
              metaData["video"]["height"] = streamsData["tracks"][key]["height"];
              metaData["video"]["width"] = streamsData["tracks"][key]["width"];
              isTrackPresent = true;
            }
          }
        }
      }
      res[jit.key()]["status"] = isTrackPresent == true ? 1 : 0;
      res[jit.key()]["metadata"] = metaData;
    }
  }

}// namespace Controller
