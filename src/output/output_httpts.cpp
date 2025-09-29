#include "output_httpts.h"
#include "lib/defines.h"
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/procs.h>
#include <mist/stream.h>
#include <mist/ts_packet.h>
#include <mist/ts_stream.h>
#include <mist/url.h>
#include <dirent.h>
#include <unistd.h>

// STD file includes
#include <cstdio>

namespace Mist{
  OutHTTPTS::OutHTTPTS(Socket::Connection &conn) : TSOutput(conn), prevTsFile(""), initialTsFile(""), prevUnixTime(0){
    sendRepeatingHeaders = 500; // PAT/PMT every 500ms (DVB spec)
    removeOldPlaylistFiles = false;
    previousStartTime = 0xFFFFFFFFFFFFFFFFull;
    addFinalHeader = false;
    isUrlTarget = false;
    forceVodPlaylist = true;
    writeFilenameOnly = false;
    // systemBoot = Util::getGlobalConfig("systemBoot").asInt();
    // currentStartTime = 0;
    char *_vision_args[128];
    HTTP::URL target(config->getString("target"));

    // set the duration from env or default after which playlists should be regulated
    char *intervalEnvVal = getenv(REGULATE_INTERVAL_ENV_KEY);
    if (intervalEnvVal){
      try{
        std::string regulationIntervalStr = std::string(intervalEnvVal);
        if (!regulationIntervalStr.empty()){
          regulationInterval = std::stoi(regulationIntervalStr);
        }
      }catch (const std::exception &ex){
        ERROR_MSG("EXCEPTION occured : '%s'", ex.what());
      }
    }else{
      INFO_MSG("[RTMPServer] env '%s' is not set.", REGULATE_INTERVAL_ENV_KEY);
    }

    // set the max allowed lines in a playlist from env or default
    char *indexMaxLinesVal = getenv(M3U8_INDEX_MAX_LINES_KEY);
    if (indexMaxLinesVal){
      try{
        std::string indexMaxLinesStr = std::string(indexMaxLinesVal);
        if (!indexMaxLinesStr.empty()){
          indexMaxLines = std::stoi(indexMaxLinesStr);
        }
      }catch (const std::exception &ex){
        ERROR_MSG("EXCEPTION occured : '%s'", ex.what());
      }
    }else{
      INFO_MSG("[RTMPServer] env '%s' is not set.", M3U8_INDEX_MAX_LINES_KEY);
    }

    if (config->getString("target").size()){
      addFinalHeader = true;
      HTTP::URL target(config->getString("target"));
      // If writing to a playlist file, set target strings and remember playlist location
      if(target.getExt() == "m3u8"){
        // Location to .m3u(8) file we will keep updated
        playlistLocation = target.getFilePath();
        // Subfolder name which gets prepended to each entry in the playlist file
        prepend = "./segments_" + target.path.substr(target.path.rfind("/") + 1, target.path.size() - target.getExt().size() - target.path.rfind("/") - 2) + "/";
        // First segment file name as approximate timestamp of recieving stream
        initialTsFile = JSON::Value(Util::unixMS()/1000).asString();
        HTTP::URL tsFolderPath(target.link(prepend + initialTsFile + ".ts").getFilePath());
        tsLocation = tsFolderPath.getFilePath();
        INFO_MSG("Playlist location will be '%s'. TS filename will be in the form of '%s'", playlistLocation.c_str(), tsLocation.c_str());
        // Remember target name including the $datetime variable
        setenv("MST_ORIG_TARGET", tsLocation.c_str(), 1);
        config->getOption("target", true).append(tsLocation);
        // Finally set split time in seconds
        std::stringstream ss;
        ss << config->getInteger("targetSegmentLength");
        targetParams["split"] = ss.str();
      }
    }
  }

  OutHTTPTS::~OutHTTPTS(){}

  void OutHTTPTS::initialSeek(){
    // Adds passthrough support to the regular initialSeek function
    if (targetParams.count("passthrough")){selectAllTracks();}
    Output::initialSeek();
  }

  void OutHTTPTS::init(Util::Config *cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "HTTPTS";
    capa["friendly"] = "TS over HTTP";
    capa["desc"] = "Pseudostreaming in MPEG2/TS format over HTTP";
    capa["url_rel"] = "/$.ts";
    capa["url_match"] = "/$.ts";
    capa["socket"] = "http_ts";
    capa["codecs"][0u][0u].append("+H264");
    capa["codecs"][0u][0u].append("+HEVC");
    capa["codecs"][0u][0u].append("+MPEG2");
    capa["codecs"][0u][1u].append("+AAC");
    capa["codecs"][0u][1u].append("+MP3");
    capa["codecs"][0u][1u].append("+AC3");
    capa["codecs"][0u][1u].append("+EAC3");
    capa["codecs"][0u][1u].append("+MP2");
    capa["codecs"][0u][1u].append("+opus");
    capa["codecs"][1u][0u].append("rawts");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/video/mpeg";
    capa["methods"][0u]["hrn"] = "TS HTTP progressive";
    capa["methods"][0u]["priority"] = 1;
    capa["push_urls"].append("/*.ts");
    capa["push_urls"].append("ts-exec:*");
    capa["push_urls"].append("*.m3u8");

#ifndef WITH_SRT
    {
      pid_t srt_tx = -1;
      const char *_vision_args[] ={"srt-live-transmit", 0};
      srt_tx = Util::Procs::StartPiped(_vision_args, 0, 0, 0);
      if (srt_tx > 1){
        capa["push_urls"].append("srt://*");
        capa["desc"] = capa["desc"].asStringRef() +
                       ". Non-native SRT push output support (srt://*) is installed and available.";
      }else{
        capa["desc"] =
            capa["desc"].asStringRef() +
            ". To enable non-native SRT push output support, please install the srt-live-transmit binary.";
      }
    }
#endif

    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1;
    opt["help"] = "Target filename to store TS file as, '*.m3u8' for writing to a playlist, or - for stdout.";
    cfg->addOption("target", opt);

    opt.null();
    opt["arg"] = "integer";
    opt["long"] = "targetSegmentLength";
    opt["short"] = "l";
    opt["help"] = "Target time duration in seconds for TS files, when outputting to disk.";
    opt["value"].append(5);
    config->addOption("targetSegmentLength", opt);
    capa["optional"]["targetSegmentLength"]["name"] = "Length of TS files (ms)";
    capa["optional"]["targetSegmentLength"]["help"] = "Target time duration in milliseconds for TS files, when outputting to disk.";
    capa["optional"]["targetSegmentLength"]["option"] = "--targetLength";
    capa["optional"]["targetSegmentLength"]["type"] = "uint";
    capa["optional"]["targetSegmentLength"]["default"] = 5;
  }

  bool OutHTTPTS::listenMode(){return !(config->getString("target").size());}

  bool OutHTTPTS::isRecording(){return true;}

  bool OutHTTPTS::onFinish(){
    if(addFinalHeader){
      addFinalHeader = false;
      sendHeader();
    }
    // stop() to clear buffer
    stop();
    return false;
  }

  void OutHTTPTS::respondHTTP(const HTTP::Parser & req, bool headersOnly){
    HTTPOutput::respondHTTP(req, headersOnly);
    H.protocol = "HTTP/1.0";
    H.SendResponse("200", "OK", myConn);
    if (!headersOnly){
      parseData = true;
      wantRequest = false;
    }
  }

  void OutHTTPTS::sendHeader(){
    double segmentDuration = double(lastPacketTime - previousStartTime) / 1000;
    // currentStartTime = Output::currentTime();
    currentUnixTime = (Util::unixMS())/1000;
    // pre-validations
    // First segment and the consecutive two could often be written almost at the same time at start
    if (currentUnixTime == prevUnixTime){
      currentUnixTime += 1;
      WARN_MSG("[RTMPServer] Previous segment time %d is the same as current, incrementing to %d", prevUnixTime, currentUnixTime);
    }
    prevUnixTime = currentUnixTime;
    // Do not rewrite the previous data
    if (segmentDuration < 1){ return; }
    // Hard code to make sure that the first segment's length is not an incorrectly long value
    if (segmentDuration > 100){ segmentDuration = atof(targetParams["split"].c_str()); }

    // Add the previous TS file to the playlist
    if (prevTsFile != ""){
      std::ofstream outPlsFile;
      HIGH_MSG("Duration of TS file at location '%s' is %f ms", prevTsFile.c_str(), segmentDuration);
      if (segmentDuration > config->getInteger("targetSegmentLength")){
        DEVEL_MSG("Segment duration exceeds target segment duration. This may cause playback stalls or other errors");
      }
      DEVEL_MSG("Adding new segment of %.2f seconds to playlist '%s'", segmentDuration, playlistLocation.c_str());
      // Strip path to the ts file if necessary
      if (writeFilenameOnly){ prevTsFile = prevTsFile.substr(prevTsFile.rfind("/") + 1); }

      /* Set regulation TS */
      if (beginTS == 0){
        beginTS = std::stoi(prevTsFile.substr(prevTsFile.rfind("/")+1, prevTsFile.length() -  3));
        if (!removeOldPlaylistFiles){
          uint32_t timestamp = getBeginTSFromFile(playlistLocation);
          if (timestamp != 0){
            beginTS = timestamp;
            DEVEL_MSG("[RTMPServer] [regulate] got beginning timestamp from file --> '%d'", beginTS);
          }
        }
      }
      currentTS = std::stoi(prevTsFile.substr(prevTsFile.rfind("/")+1, prevTsFile.length() - 3));
      elapsedInterval = currentTS - beginTS;

      /** Check elapsed time and compare with \b regulationInterval */
      if (elapsedInterval >= (regulationInterval + DEFAULT_NVDR_REGULATION_INTERVAL)){
        DEVEL_MSG("[RTMPServer] [regulate] elapsed interval -> '%d'", elapsedInterval);
        DEVEL_MSG("[RTMPServer] [regulate] regulating manifest -> '%s'", playlistLocation.c_str());
        try{regulateManifest(playlistLocation);}catch (const std::exception &ex){
          FAIL_MSG("[RTMPServer] [regulate] while regulating manifest : '%s'", ex.what());
        }
        beginTS = 0;
      }
    }
    // Append duration & TS filename to playlist file
    appendTS(playlistLocation, prevTsFile, "#EXTINF:"+JSON::Value(segmentDuration).asString()+",");

    // Save the target we were pushing towards as the latest completed segment
    prevTsFile = tsLocation;
    // Set the start time of the current segment. We will know the end time once the next split point is reached
    previousStartTime = lastPacketTime;
    // Set the next filename we will push towards once the split point is reached
    HTTP::URL target(getenv("MST_ORIG_TARGET"));
    if (isUrlTarget){
      tsLocation = target.link(std::string("./") + JSON::Value(currentUnixTime).asString() + ".ts").getUrl();
    }else{
      tsLocation = target.link(std::string("./") + JSON::Value(currentUnixTime).asString() + ".ts").getFilePath();
    }
    INFO_MSG("Setting next target TS file '%s'", tsLocation.c_str());
    setenv("MST_ORIG_TARGET", tsLocation.c_str(), 1);

    TSOutput::sendHeader();
  }

  void OutHTTPTS::appendTS(std::string playlistLoc, std::string ts, std::string duration){
    std::ofstream fileOutDesc(playlistLoc.c_str(), std::ios::app);
    static bool firstCall = true;
    if (fileOutDesc.is_open()){
      if (ts.empty()) return;
      if (firstCall){
        INFO_MSG("[RTMPServer] [regulate] regulating manifest on boot");
        try{regulateManifest(playlistLocation);}catch (const std::exception &ex){
          FAIL_MSG("[RTMPServer] [regulate] while regulating manifest : '%s'", ex.what());
        }
        beginTS = 0;
        // Do not call this scope ever again in the process runtime
        firstCall = false;
        /*
          - We want to add the #EXT-X-DISCONTINUITY tag if this is the first chunk
            being written after this process started.
          - Moved it here (from the constructor) because the former caused
            multiple #EXT-X-DISCONTINUITY tags written on erroneous streams restarts
        */
        fileOutDesc << "#EXT-X-DISCONTINUITY\n";
      }
      DEVEL_MSG("[RTMPServer] [regulate] writing -> '%s'", ts.c_str());
      // Add current server timestamp
      // uint64_t unixMs = M.getBootMsOffset() + systemBoot + currentStartTime;
      // fileOutDesc << "#EXT-X-PROGRAM-DATE-TIME:" + Util::getUTCStringMillis(unixMs) + "\n";
      fileOutDesc << duration << "\n" << ts << "\n";
      fileOutDesc.close();
    }
  }

  uint32_t OutHTTPTS::removeOldTS(std::vector<std::string> &playlist){
    uint32_t chunkTime = 0;
    std::string extinfo;
    std::vector<std::string> tempPlaylist(playlist);
    uint16_t trimIdx = 0;

    /* Get trim index */
    if (playlist.size() > indexMaxLines){
      // Approximates to around 12 hours of playlist lines by default, please configure VISION_NDVR_INDEX_MAX_LINES env accordingly
      FAIL_MSG("[RTMPServer] [regulate] reducing size of overflown m3u8 playlist");
      // Create a new vector containing elements from its 5000th element (around 3 hours worth) to the last one
      uint64_t linesInPlaylist = (indexMaxLines < 5000) ? indexMaxLines : 5000;
      std::vector<std::string> trimmedPlaylist(playlist.end()-linesInPlaylist, playlist.end());
      tempPlaylist.clear();
      tempPlaylist = trimmedPlaylist;
      playlist.clear();
      playlist = trimmedPlaylist;
      // Remove old ts from this shortened playlist now
      uint32_t chunkTime = removeOldTS(playlist);
      return chunkTime;
    }
    DEVEL_MSG("[RTMPServer] [regulate] playlist length before trimming --> '%d'", playlist.size());

    size_t discontinuityCount = 0;
    size_t tsCount = 0;
    std::string chunk;
    /* Loop to parse and find the first chunk time to regulate the playlist from */
    for (trimIdx = 0; trimIdx < playlist.size(); ++trimIdx){
      chunk = playlist[trimIdx];
      if (chunk.rfind('\0') != std::string::npos){
        // Skip if this is the last line
        continue;
      }
      if (chunk.substr(0, 7) == "#EXTINF"){
        // Skip info tags
        continue;
      }else if (chunk.substr(0, 20) == "#EXT-X-DISCONTINUITY"){
        // Skip and count discontinuity tags
        discontinuityCount += 1;
        continue;
      }else if (chunk.substr(chunk.length() - 3, 3) == ".ts"){
        // At this point, we must have .ts chunk written in the next line
        ++tsCount;
        std::string chunkTs = chunk.substr(chunk.rfind("/")+1, chunk.length()-3);
        chunkTime = std::stoi(chunkTs);
        if ((currentTS - (regulationInterval)) <= chunkTime){
          break;
        }
      }else{
        // Skip anything else that comes up as unhandled
        ERROR_MSG("[RTMPServer] [regulate] unhandled line of length %u in playlist --> '%s'", chunk.length(), chunk.c_str());
        continue;
      }
    }
    if (!tsCount){
      if (discontinuityCount){
        // Our playlist only contains #EXT-X-DISCONTINUITY, shit.
        ERROR_MSG("[RTMPServer] [regulate] clearing playlist that only has #EXT-X-DISCONTINUITY and no TS files");
      }else{
        // Our playlist contains no ts files :(
        ERROR_MSG("[RTMPServer] [regulate] clearing playlist that has no TS files");
      }
      prevTsFile = "";
      playlist.clear();
      return 0;
    }
    /* Make sub vector from trim index to end and mark it as the new playlist */
    playlist.clear();
    playlist = {tempPlaylist.begin() + trimIdx, tempPlaylist.end()};
    DEVEL_MSG("[RTMPServer] [regulate] playlist length after trimming --> '%d'", playlist.size());
    return chunkTime;
  }

  void OutHTTPTS::regulateManifest(std::string playlistLoc){
    std::vector<std::string> lines;
    // plsConn.close();
    std::ifstream fileDesc(playlistLoc.c_str());
    std::string line;

    /* Get lines from file */
    if (!fileDesc.is_open()){
      ERROR_MSG("[RTMPServer] [regulate] could not open playlist file");
      return;
    }
    while (std::getline(fileDesc, line)){
      Util::sleep(1);
      if (line.empty()){
        INSANE_MSG("[RTMPServer] [regulate] empty line in playlist");
        continue;
      }
      lines.push_back(line);
    }
    fileDesc.close();

    /* Remove old ts and set current top as beginTS */
    if (lines.empty()){
      INFO_MSG("[RTMPServer] [regulate] skipping regulation, empty manifest");
      return;
    }
    try {beginTS = removeOldTS(lines);} catch (const std::exception &ex){
      ERROR_MSG("[RTMPServer] [regulate] remove old ts : '%s'", ex.what());
      return;
    }

    INFO_MSG("[RTMPServer] [regulate] regulated new begin TS --> '%d'", beginTS);
    for (auto str : lines){
      INSANE_MSG("[RTMPServer] [regulate] new line --> '%s'", str.c_str());
    }

    /* Write the modified manifest lines back to the file */
    std::ofstream fileOutDesc(playlistLoc.c_str(), std::ios::trunc);
    if (fileOutDesc.is_open()){
      for (const auto& line : lines){
          if (line.empty()) continue;
          fileOutDesc << line << "\n";
      }

      fileOutDesc.close();
    }
  }

  uint32_t OutHTTPTS::getBeginTSFromFile(std::string playlistLoc){
    uint32_t timestamp = 0;
    std::ifstream fileDesc(playlistLoc.c_str());
    /* Get timestamp value if file exists */
    if (!fileDesc.is_open()){
      FAIL_MSG("[RTMPServer] [regulate] failed to read playlist to get beginning TS time");
      return 0;
    }else{
      /* Get first timestamp string */
      std::string line = "";
      uint64_t numLines = 0;
      while (std::getline(fileDesc, line)){
        ++numLines;
        if (numLines % 100 == 0){
          // Sleep at every 100th line
          Util::sleep(10);
        }
        if (line.empty() || (line.compare(0, 6, "/ndvr/") != 0)){
          continue;
        }
        size_t lastSlashPos = line.find_last_of("/");
        if (lastSlashPos != std::string::npos){
          std::string timestampStr = line.substr(lastSlashPos + 1);
          try{
            timestamp = std::stoi(timestampStr);
          }catch (const std::invalid_argument &ia){
            ERROR_MSG("[RTMPServer] [regulate] got invalid timestamp '%s'", timestampStr.c_str());
          }catch (const std::out_of_range &oor){
            ERROR_MSG("[RTMPServer] [regulate] got out of range timestamp '%s'", timestampStr.c_str());
          }catch (const std::exception &ex){
            WARN_MSG("[RTMPServer] [regulate] unhandled exception while getting begin TS: '%s'", ex.what());
          }
          break;
        }
      }
      return timestamp;
    }
  }

  void OutHTTPTS::sendTS(const char *tsData, size_t len){
    myConn.SendNow(tsData, len);
    return;
  }

}// namespace Mist
