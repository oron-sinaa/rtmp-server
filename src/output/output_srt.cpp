#include "output_srt.h"
#include <iomanip>
#include <mist/checksum.h>
#include <mist/defines.h>
#include <mist/http_parser.h>

namespace Mist{
  OutSRT::OutSRT(Socket::Connection &conn) : HTTPOutput(conn){realTime = 0;}
  OutSRT::~OutSRT(){}

  void OutSRT::init(Util::Config *cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "SRT";
    capa["friendly"] = "SubRip/WebVTT over HTTP";
    capa["desc"] = "Pseudostreaming in SubRip Text (SRT) and WebVTT formats over HTTP";
    capa["url_match"].append("/$.srt");
    capa["url_match"].append("/$.vtt");
    capa["url_match"].append("/$.webvtt");
    capa["codecs"][0u][0u].append("subtitle");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/text/plain";
    capa["methods"][0u]["hrn"] = "SRT subtitle progressive";
    capa["methods"][0u]["priority"] = 8;
    capa["methods"][0u]["url_rel"] = "/$.srt";
    capa["methods"][1u]["handler"] = "http";
    capa["methods"][1u]["type"] = "html5/text/vtt";
    capa["methods"][1u]["hrn"] = "WebVTT subtitle progressive";
    capa["methods"][1u]["priority"] = 9;
    capa["methods"][1u]["url_rel"] = "/$.webvtt";
  }

  void OutSRT::sendNext(){
    char *dataPointer = 0;
    size_t len = 0;
    thisPacket.getString("data", dataPointer, len);
    // ignore empty subs
    if (len == 0 || (len == 1 && dataPointer[0] == ' ')){return;}
    std::stringstream tmp;
    if (!webVTT){tmp << lastNum++ << std::endl;}
    uint64_t time = thisPacket.getTime();

    // filter subtitle in specific timespan
    if (filter_from > 0 && time < filter_from){
      index++; // when using seek, the index is lost.
      seek(filter_from);
      return;
    }

    if (filter_to > 0 && time > filter_to && filter_to > filter_from){
      config->is_active = false;
      return;
    }

    char tmpBuf[50];
    size_t tmpLen =
        sprintf(tmpBuf, "%.2" PRIu64 ":%.2" PRIu64 ":%.2" PRIu64 ".%.3" PRIu64, (time / 3600000),
                ((time % 3600000) / 60000), (((time % 3600000) % 60000) / 1000), time % 1000);
    tmp.write(tmpBuf, tmpLen);
    tmp << " --> ";
    time += thisPacket.getInt("duration");
    if (time == thisPacket.getTime()){time += len * 75 + 800;}
    tmpLen = sprintf(tmpBuf, "%.2" PRIu64 ":%.2" PRIu64 ":%.2" PRIu64 ".%.3" PRIu64, (time / 3600000),
                     ((time % 3600000) / 60000), (((time % 3600000) % 60000) / 1000), time % 1000);
    tmp.write(tmpBuf, tmpLen);
    tmp << std::endl;
    myConn.SendNow(tmp.str());
    // prevent double newlines
    if (dataPointer[len - 1] == '\n'){--dataPointer;}
    myConn.SendNow(dataPointer, len);
    myConn.SendNow("\n\n");
  }

  void OutSRT::sendHeader(){
    H.setCORSHeaders();
    H.SetHeader("Content-Type", (webVTT ? "text/vtt; charset=utf-8" : "text/plain; charset=utf-8"));
    H.protocol = "HTTP/1.0";
    H.SendResponse("200", "OK", myConn);
    if (webVTT){myConn.SendNow("WEBVTT\n\n");}
    sentHeader = true;
  }

  void OutSRT::onHTTP(){
    std::string method = H.method;
    webVTT = (H.url.find(".vtt") != std::string::npos) || (H.url.find(".webvtt") != std::string::npos);
    if (H.GetVar("track") != ""){
      size_t tid = atoll(H.GetVar("track").c_str());
      if (M.getValidTracks().count(tid)){
        userSelect.clear();
        userSelect[tid].reload(streamName, tid);
      }
    }

    filter_from = 0;
    filter_to = 0;
    index = 0;

    if (H.GetVar("from") != ""){filter_from = JSON::Value(H.GetVar("from")).asInt();}
    if (H.GetVar("to") != ""){filter_to = JSON::Value(H.GetVar("to")).asInt();}
    if (filter_to){realTime = 0;}

    H.Clean();
    H.setCORSHeaders();
    if (method == "OPTIONS" || method == "HEAD"){
      H.SetHeader("Content-Type", (webVTT ? "text/vtt; charset=utf-8" : "text/plain; charset=utf-8"));
      H.protocol = "HTTP/1.0";
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
    }
    lastNum = 0;
    parseData = true;
    wantRequest = false;
  }
}// namespace Mist
