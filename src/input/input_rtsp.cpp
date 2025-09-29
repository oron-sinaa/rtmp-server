#include "input_rtsp.h"
#define DEFAULTBUFFERSIZE 10000 // milliseconds

Mist::InputRTSP *classPointer = 0;
Socket::Connection *mainConn = 0;

void incomingPacket(const DTSC::Packet &pkt){
  classPointer->incoming(pkt);
}
void insertRTP(const uint64_t track, const RTP::Packet &p){
  classPointer->incomingRTP(track, p);
}

/// Function used to send RTP packets over UDP
///\param socket A UDP Connection pointer, sent as a void*, to keep portability.
///\param data The RTP Packet that needs to be sent
///\param len The size of data
///\param channel Not used here, but is kept for compatibility with sendTCP
void sendUDP(void *socket, const char *data, size_t len, uint8_t channel){
  ((Socket::UDPConnection *)socket)->SendNow(data, len);
  if (mainConn){mainConn->addUp(len);}
}

namespace Mist{
  void InputRTSP::incomingRTP(const uint64_t track, const RTP::Packet &p){
    sdpState.handleIncomingRTP(track, p, &meta);
  }

  InputRTSP::InputRTSP(Util::Config *cfg) : Input(cfg){
    needAuth = false;
    reDescribe = false;
    disconnectRTSP = false;
    setPacketOffset = false;
    packetOffset = 0;
    TCPmode = true;
    sdpState.myMeta = &meta;
    sdpState.incomingPacketCallback = incomingPacket;
    classPointer = this;
    standAlone = false;
    seenSDP = false;
    cSeq = 0;
    supportsGetParameter = false;
    shouldEncodeAudio = false;
    sessionTimeout = 30;
    capa["name"] = "RTSP";
    capa["desc"] = "This input allows pulling of live RTSP sources over either UDP or TCP.";
    capa["source_match"].append("rtsp://*");
    // These can/may be set to always-on mode
    capa["always_match"].append("rtsp://*");
    capa["priority"] = 9;
    capa["codecs"]["video"].append("H264");
    capa["codecs"]["video"].append("HEVC");
    capa["codecs"]["video"].append("MPEG2");
    capa["codecs"]["video"].append("VP8");
    capa["codecs"]["video"].append("VP9");
    capa["codecs"]["audio"].append("AAC");
    capa["codecs"]["audio"].append("MP3");
    capa["codecs"]["audio"].append("AC3");
    capa["codecs"]["audio"].append("ALAW");
    capa["codecs"]["audio"].append("ULAW");
    capa["codecs"]["audio"].append("PCM");
    capa["codecs"]["audio"].append("opus");
    capa["codecs"]["audio"].append("MP2");

    JSON::Value option;
    option["arg"] = "integer";
    option["long"] = "buffer";
    option["short"] = "b";
    option["help"] = "DVR buffer time in ms";
    option["value"].append(DEFAULTBUFFERSIZE);
    config->addOption("bufferTime", option);
    capa["optional"]["DVR"]["name"] = "Buffer time (ms)";
    capa["optional"]["DVR"]["help"] = "The target available buffer time for this live stream, in "
                                      "milliseconds. This is the time available to seek around in, "
                                      "and will automatically be extended to fit whole keyframes "
                                      "as well as the minimum duration needed for stable playback.";
    capa["optional"]["DVR"]["option"] = "--buffer";
    capa["optional"]["DVR"]["type"] = "uint";
    // Reduce default buffer size to reduce RAM usage significantly
    capa["optional"]["DVR"]["default"] = DEFAULTBUFFERSIZE;
    option.null();
    option["arg"] = "string";
    option["long"] = "transport";
    option["short"] = "t";
    option["help"] = "Transport protocol (TCP (default) or UDP)";
    option["value"].append("TCP");
    config->addOption("transport", option);
    capa["optional"]["transport"]["name"] = "Transport protocol";
    capa["optional"]["transport"]["help"] = "Sets the transport protocol to either TCP (default) "
                                            "or UDP. UDP requires ephemeral UDP ports to be open, "
                                            "TCP does not.";
    capa["optional"]["transport"]["option"] = "--transport";
    capa["optional"]["transport"]["type"] = "select";
    capa["optional"]["transport"]["select"].append("TCP");
    capa["optional"]["transport"]["select"].append("UDP");
    capa["optional"]["transport"]["default"] = "TCP";
    option.null();
    // From here
    option["long"] = "audio_encoder";
    option["short"] = "a";
    option["help"] = "Enable AAC encoder for ALAW/ULAW audio inputs";
    config->addOption("audio_encoder", option);
    capa["optional"]["audio_encoder"]["name"] = "AAC transcoder";
    capa["optional"]["audio_encoder"]["help"] = "Enable AAC encoder for ALAW and ULAW audio inputs";
    capa["optional"]["audio_encoder"]["option"] = "--audio_encoder";
    option.null();
    option["long"] = "disable_audio";
    option["short"] = "m";
    option["help"] = "Disable setting up any audio tracks";
    config->addOption("disable_audio", option);
    capa["optional"]["disable_audio"]["name"] = "Disable audio";
    capa["optional"]["disable_audio"]["help"] = "Disable setting up any audio tracks";
    capa["optional"]["disable_audio"]["option"] = "--disable_audio";
    option.null();
    // Till here
  }

  void InputRTSP::sendCommand(const std::string &cmd, const std::string &cUrl, const std::string &body,
                              const std::map<std::string, std::string> *extraHeaders, bool reAuth){
    ++cSeq;
    sndH.Clean();
    sndH.protocol = "RTSP/1.0";
    sndH.method = cmd;
    sndH.url = cUrl;
    sndH.body = body;
    if ((username.size() || password.size()) && authRequest.size()){
      sndH.auth(username, password, authRequest);
    }
    sndH.SetHeader("User-Agent", APPIDENT);
    sndH.SetHeader("CSeq", JSON::Value(cSeq).asString());
    if (session.size()){
      sndH.SetHeader("Session", session);
    }
    if (extraHeaders && extraHeaders->size()){
      for (std::map<std::string, std::string>::const_iterator it = extraHeaders->begin();
           it != extraHeaders->end(); ++it){
        sndH.SetHeader(it->first, it->second);
        INFO_MSG("[RTMPServer] Setting command header -> `%s`", it->second.c_str());
      }
    }
    INFO_MSG("[RTMPServer] Sending command `%s` on url `%s` with body `%s`", cmd.c_str(), cUrl.c_str(), body.c_str());
    sndH.SendRequest(tcpCon, "", true);
    parsePacket(true);

    if ((reAuth && needAuth && authRequest.size() && (username.size() || password.size()) && tcpCon)){
      INFO_MSG("Authenticating %s...", cmd.c_str());
      sendCommand(cmd, cUrl, body, extraHeaders, false);
      if (needAuth){WARN_MSG("Authentication failed! Are the provided credentials correct?");}
    }
  }

  bool InputRTSP::checkArguments(){
    const std::string &inpt = config->getString("input");
    if (inpt.substr(0, 7) != "rtsp://"){
      FAIL_MSG("Unsupported RTSP URL: '%s'", inpt.c_str());
      return false;
    }
    const std::string &transport = config->getString("transport");
    if (transport != "TCP" && transport != "UDP" && transport != "tcp" && transport != "udp"){
      FAIL_MSG("Not a supported transport mode: %s", transport.c_str());
      return false;
    }
    if (transport == "UDP" || transport == "udp"){TCPmode = false;}
    url = HTTP::URL(config->getString("input"));
    username = url.user;
    password = url.pass;
    url.user = "";
    url.pass = "";
    return true;
  }

  bool InputRTSP::openStreamSource(){
    tcpCon.open(url.host, url.getPort(), false);
    mainConn = &tcpCon;
    if (!tcpCon){
      Util::logExitReason(ER_READ_START_FAILURE, "Opening TCP socket `%s:%s` failed", url.host.c_str(), url.getPort());
      return false;
    }
    INFO_MSG("Opened TCP stream source");
    return true;
    // return tcpCon;
  }

  void InputRTSP::parseStreamHeader(){
    tcpCon.setBlocking(false);
    std::map<std::string, std::string> extraHeaders;
    sendCommand("OPTIONS", url.getUrl(), "");
    if (disconnectRTSP){
      return;
    }
    extraHeaders["Accept"] = "application/sdp";
    DEVEL_MSG("[RTMPServer] Sending DESCRIBE command on RTSP url");
    sendCommand("DESCRIBE", url.getUrl(), "", &extraHeaders);
    // Resend DESCRIBE command on the redirected URL
    if (reDescribe){
      INFO_MSG("[RTMPServer] Re-opening stream source...");
      openStreamSource();
      INFO_MSG("[RTMPServer] Resending DESCRIBE command on updated URL %s", url.getRawUrl().c_str());
      sendCommand("DESCRIBE", url.getRawUrl(), "", &extraHeaders);
      reDescribe = false;
    }
    if (!tcpCon || !seenSDP){
      WARN_MSG("Could not get stream description!");
      return;
    }
    if (sdpState.tracks.size()){
      bool atLeastOne = false;
      for (std::map<uint64_t, SDP::Track>::iterator it = sdpState.tracks.begin(); it != sdpState.tracks.end();){
        if (it->second.control.size() == 0 || (M.getType(it->first) == "audio" && config->getBool("disable_audio"))){
          WARN_MSG("[RTMPServer] Disabled audio track");
          if (config->getBool("disable_audio")) meta.removeTrack(it->first);
          it = sdpState.tracks.erase(it);  // Erase and move to the next element safely
        }else{
          ++it;  // Move to the next element
        }
      }
      for (std::map<uint64_t, SDP::Track>::iterator it = sdpState.tracks.begin();
           it != sdpState.tracks.end(); ++it){
        transportSet = false;
        extraHeaders.clear();
        extraHeaders["Transport"] = it->second.generateTransport(it->first, url.host, TCPmode);
        lastRequestedSetup = HTTP::URL(url.getUrl() + "/").link(it->second.control).getUrl();
        INFO_MSG("[RTMPServer] Requesting SETUP on %s", lastRequestedSetup.c_str());
        sendCommand("SETUP", lastRequestedSetup, "", &extraHeaders);
        if (disconnectRTSP){
          return;
        }
        if (tcpCon && transportSet){
          atLeastOne = true;
          continue;
        }
        if (!atLeastOne && tcpCon){
          INFO_MSG("Failed to set up transport for track %s, switching transports...", M.getTrackIdentifier(it->first).c_str());
          TCPmode = !TCPmode;
          extraHeaders["Transport"] = it->second.generateTransport(it->first, url.host, TCPmode);
          sendCommand("SETUP", lastRequestedSetup, "", &extraHeaders);
        }
        WARN_MSG("Could not setup track %s!", M.getTrackIdentifier(it->first).c_str());
        tcpCon.close();
        return;
      }
    }
    INFO_MSG("Setup complete");
    extraHeaders.clear();
    extraHeaders["Range"] = "npt=now-";
    VERYHIGH_MSG("[OPTION] audio_encoder = %d, disable_audio = %d", config->getBool("audio_encoder"), config->getBool("disable_audio"));
    if (!config->getBool("disable_audio") && config->getBool("audio_encoder")){
      bool shouldAddTrack = true;
      for (std::map<uint64_t, SDP::Track>::iterator it = sdpState.tracks.begin(); it != sdpState.tracks.end(); ++it){
        if (M.getCodec(it->first) == "AAC"){
          WARN_MSG("[RTMPServer] AAC track already exists - skipping encoder");
          // Do not add another AAC track if it already exists in the input
          shouldAddTrack = false;
          break;
        }
      }
      size_t tid;
      if (shouldAddTrack){
        tid = meta.addTrack();
        if (tid == INVALID_TRACK_ID){
          WARN_MSG("[RTMPServer] Could not add new track to encode to - skipping encoder");
          shouldAddTrack = false;
        }
      }
      if (shouldAddTrack){
        shouldEncodeAudio = true;
        std::string trackType = "audio";
        meta.setType(tid, trackType);
        meta.setID(tid, tid);
        meta.setCodec(tid, "AAC");
        meta.setRate(tid, 8000);
        meta.setSize(tid, 16);
        meta.setChannels(tid, 1);
        std::string configStr = "1588";
        meta.setInit(tid, Encodings::Hex::decode(configStr));
      }
    }
    sendCommand("PLAY", url.getUrl(), "", &extraHeaders);
    extraHeaders.clear();
    if (TCPmode){tcpCon.setBlocking(true);}
  }

  void InputRTSP::closeStreamSource(){
    std::map<std::string, std::string> extraHeaders;
    DEVEL_MSG("[RTMPServer] Sending TEARDOWN on RTSP and closing TCP connection");
    extraHeaders["Connection"] = "close";
    sendCommand("TEARDOWN", url.getUrl(), "", &extraHeaders);
    tcpCon.close();
  }

  void InputRTSP::streamMainLoop(){
    Comms::Connections statComm;
    uint64_t startTime = Util::epoch();
    uint64_t lastPing = Util::bootSecs();
    uint64_t lastSecs = 0;
    while (keepAlive() && parsePacket()){
      uint64_t currSecs = Util::bootSecs();
      handleUDP();
      // Ping on RTSP connection server every 30 seconds to accomodate for sessionTimeout
      if (supportsGetParameter && Util::bootSecs() - lastPing > sessionTimeout){
        DEVEL_MSG("[RTMPServer] Sending routine GET_PARAMETER on RTSP connection");
        sendCommand("GET_PARAMETER", url.getUrl(), "");
        lastPing = Util::bootSecs();
      }
      if (lastSecs != currSecs){
        lastSecs = currSecs;
        // Connect to stats for INPUT detection
        statComm.reload(streamName, getConnectedBinHost(), JSON::Value(getpid()).asString(), "INPUT:" + capa["name"].asStringRef(), "");
        if (statComm){
          if (statComm.getStatus() & COMM_STATUS_REQDISCONNECT){
            config->is_active = false;
            Util::logExitReason(ER_CLEAN_CONTROLLER_REQ, "received shutdown request from controller");
            return;
          }
          uint64_t now = Util::bootSecs();
          statComm.setNow(now);
          statComm.setStream(streamName);
          statComm.setConnector("INPUT:" + capa["name"].asStringRef());
          statComm.setUp(tcpCon.dataUp());
          statComm.setDown(tcpCon.dataDown());
          statComm.setTime(now - startTime);
          statComm.setLastSecond(0);
        }
      }
    }
    if (!tcpCon){
      Util::logExitReason(ER_CLEAN_REMOTE_CLOSE, "TCP connection closed");
    }
  }

  bool InputRTSP::parsePacket(bool mustHave){
    uint32_t waitTime = 500;
    if (!TCPmode){waitTime = 50;}
    do{
      // No new data? Sleep and retry, if connection still open
      if (!tcpCon.Received().size() || !tcpCon.Received().available(1)){
        if (!tcpCon.spool() && tcpCon && keepAlive()){
          Util::sleep(waitTime);
          if (!mustHave){return tcpCon;}
        }
        continue;
      }
      if (tcpCon.Received().copy(1) != "$"){
        // not a TCP RTP packet, read RTSP commands
        if (recH.Read(tcpCon)){
          // Print received response
          DEVEL_MSG("[RTMPServer] Received reponse code %s with body %s", recH.url.c_str(), recH.GetResponseStr().c_str());
          if (recH.url == "403"){
            disconnectRTSP = true;
            WARN_MSG("[RTMPServer] 403: Forbidden");
            return false;
          }
          if (recH.url == "453"){
            disconnectRTSP = true;
            WARN_MSG("[RTMPServer] 453: Not enough bandwidth");
            return false;
          }
          if (recH.hasHeader("WWW-Authenticate")){
            authRequest = recH.GetHeader("WWW-Authenticate");
          }
          needAuth = (recH.url == "401");
          if (needAuth){
            INFO_MSG("Requires authentication");
            recH.Clean();
            return true;
          }
          // Check if the reponse contains redirection response code
          needRedirection = (recH.url == "302");
          if (needRedirection){
            INFO_MSG("[RTMPServer] RTSP server requires redirection");
            if (recH.hasHeader("Location") || recH.hasHeader("LocationEx")){
              INFO_MSG("[RTMPServer] Changing base URL to %s", recH.GetHeader("Location").c_str());
              tcpCon.close();
              // Change base URL to the redirected "Location" header
              url = HTTP::URL(recH.GetHeader("Location"));
              reDescribe = true;
            }
          }
          if (recH.hasHeader("Content-Location")){
            INFO_MSG("Changing base URL from %s to %s", url.getUrl().c_str(),
                     recH.GetHeader("Content-Base").c_str());
            url = HTTP::URL(recH.GetHeader("Content-Location"));
          }
          if (recH.hasHeader("Content-Base") && recH.GetHeader("Content-Base") != "" &&
              recH.GetHeader("Content-Base") != url.getUrl()){
            INFO_MSG("Changing base URL from %s to %s", url.getUrl().c_str(),
                     recH.GetHeader("Content-Base").c_str());
            url = HTTP::URL(recH.GetHeader("Content-Base"));
          }
          if (recH.hasHeader("Session")){
            session = recH.GetHeader("Session");
            std::size_t timeoutPos = session.find("timeout=");
            if (timeoutPos != std::string::npos){
              timeoutPos += 8;
              std::size_t endPos = session.find(";", timeoutPos);
              if (endPos == std::string::npos){
                endPos = session.length();
              }
              sessionTimeout = std::stoi(session.substr(timeoutPos, endPos - timeoutPos));
              if (sessionTimeout > 5){
                sessionTimeout -= 5;
                INFO_MSG("[RTMPServer] Set session timeout to %u", sessionTimeout);
              }else{
                // Do not send GET_PARAMETER to manage timeout
                supportsGetParameter = false;
              }
            }
            std::size_t semicolonPos = session.find(';');
            if (session.find(';') != std::string::npos){
                session.erase(semicolonPos);
            }
          }
          if ((recH.hasHeader("Content-Type") &&
               recH.GetHeader("Content-Type") == "application/sdp") ||
              (recH.hasHeader("Content-type") &&
               recH.GetHeader("Content-type") == "application/sdp")){
            INFO_MSG("Received SDP");
            seenSDP = true;
            sdpState.parseSDP(recH.body);
            recH.Clean();
            INFO_MSG("SDP contained %zu tracks", M.getValidTracks().size());
            return true;
          }
          if (recH.hasHeader("Transport")){
            INFO_MSG("Received setup response");
            recH.url = lastRequestedSetup;
            size_t trackNo = sdpState.parseSetup(recH, url.host, "");
            if (trackNo != INVALID_TRACK_ID){
              INFO_MSG("Parsed transport for track: %zu", trackNo);
              transportSet = true;
            }else{
              INFO_MSG("Could not parse transport string!");
            }
            recH.Clean();
            return true;
          }
          if (recH.url == "200" && recH.hasHeader("Public")){
            if (recH.GetHeader("Public").find("GET_PARAMETER") != std::string::npos){
              INFO_MSG("Stream supports GET_PARAMETER");
              supportsGetParameter = true;
            }
          }
          if (recH.url == "200" && recH.hasHeader("RTP-Info")){
            INFO_MSG("Playback starting");
            recH.Clean();
            return true;
          }
          // Ignore "OK" replies beyond this point
          if (recH.url == "200"){
            recH.Clean();
            return true;
          }
          if (recH.url != "200" || recH.url != "302" || recH.url != "401" || recH.url != "403" || recH.url != "404" || recH.url != "453"){
            INFO_MSG("[RTMPServer] Unknown RTSP response: Code: '%s'; Body: '%s'", recH.url.c_str(), recH.GetResponseStr().c_str());
          }
          // DO NOT Print anything possibly interesting to cerr
          // std::cerr << recH.BuildRequest() << std::endl;
          recH.Clean();
          return true;
        }
        if (!tcpCon.spool() && tcpCon && keepAlive()){Util::sleep(waitTime);}
        continue;
      }
      if (!tcpCon.Received().available(4)){
        if (!tcpCon.spool() && tcpCon && keepAlive()){Util::sleep(waitTime);}
        continue;
      }// a TCP RTP packet, but not complete yet

      // We have a TCP packet! Read it...
      // Format: 1 byte '$', 1 byte channel, 2 bytes len, len bytes binary data
      std::string tcpHead = tcpCon.Received().copy(4);
      uint16_t len = ntohs(*(short *)(tcpHead.data() + 2));
      if (!tcpCon.Received().available(len + 4)){
        if (!tcpCon.spool() && tcpCon){Util::sleep(waitTime);}
        continue;
      }// a TCP RTP packet, but not complete yet
      // remove whole packet from buffer, including 4 byte header
      std::string tcpPacket = tcpCon.Received().remove(len + 4);
      RTP::Packet pkt(tcpPacket.data() + 4, len);
      uint8_t chan = tcpHead.data()[1];
      size_t trackNo = sdpState.getTrackNoForChannel(chan);
      EXTREME_MSG("Received %ub RTP packet #%u on channel %u, time %" PRIu32, len,
                  pkt.getSequence(), chan, pkt.getTimeStamp());
      if (trackNo == INVALID_TRACK_ID){
        if ((chan % 2) != 1){
          WARN_MSG("Received packet for unknown track number on channel %u", chan);
        }
        return true;
      }
      // We override the rtpSeq number because in TCP mode packet loss is not a thing.
      sdpState.tracks[trackNo].sorter.rtpSeq = pkt.getSequence();
      sdpState.handleIncomingRTP(trackNo, pkt, &meta, shouldEncodeAudio);

      return true;

    }while (tcpCon && keepAlive());
    return false;
  }

  /// Reads and handles RTP packets over UDP, if needed
  bool InputRTSP::handleUDP(){
    if (TCPmode){return false;}
    bool r = false;
    for (std::map<uint64_t, SDP::Track>::iterator it = sdpState.tracks.begin();
         it != sdpState.tracks.end(); ++it){
      Socket::UDPConnection &s = it->second.data;
      it->second.sorter.setCallback(it->first, insertRTP);
      while (s.Receive()){
        r = true;
        // if (s.getDestPort() != it->second.sPortA){
        //  // wrong sending port, ignore packet
        //  continue;
        //}
        tcpCon.addDown(s.data.size());
        RTP::Packet pack(s.data, s.data.size());
        if (!it->second.theirSSRC){it->second.theirSSRC = pack.getSSRC();}
        it->second.sorter.addPacket(pack);
      }
      if (Util::bootSecs() != it->second.rtcpSent){
        it->second.rtcpSent = Util::bootSecs();
        it->second.pack.sendRTCP_RR(it->second, sendUDP);
      }
    }
    return r;
  }

  void InputRTSP::incoming(const DTSC::Packet &pkt){
    if (!M.getBootMsOffset()){
      meta.setBootMsOffset(Util::bootMS() - pkt.getTime());
      packetOffset = 0;
      setPacketOffset = true;
    }else if (!setPacketOffset){
      packetOffset = (Util::bootMS() - pkt.getTime()) - M.getBootMsOffset();
      setPacketOffset = true;
    }
    static DTSC::Packet newPkt;
    char *pktData;
    size_t pktDataLen;
    pkt.getString("data", pktData, pktDataLen);
    size_t idx = M.trackIDToIndex(pkt.getTrackId(), getpid());

    if (idx == INVALID_TRACK_ID){
      INFO_MSG("Invalid index for track number %zu", pkt.getTrackId());
    }else{
      if (!userSelect.count(idx)){
        WARN_MSG("Reloading track %zu, index %zu", pkt.getTrackId(), idx);
        userSelect[idx].reload(streamName, idx, COMM_STATUS_ACTIVE | COMM_STATUS_SOURCE | COMM_STATUS_DONOTTRACK);
      }
      if (userSelect[idx].getStatus() & COMM_STATUS_REQDISCONNECT){
        Util::logExitReason(ER_CLEAN_LIVE_BUFFER_REQ, "buffer requested shutdown");
        tcpCon.close();
      }
    }

    bufferLivePacket(pkt.getTime() + packetOffset, pkt.getInt("offset"), idx, pktData,
                     pktDataLen, 0, pkt.getFlag("keyframe"));
  }

}// namespace Mist
