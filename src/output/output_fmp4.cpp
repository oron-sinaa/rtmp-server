#include "output_fmp4.h"

static uint64_t unixBootDiff = Util::unixMS();

namespace Mist{

  OutfMP4::OutfMP4(Socket::Connection &conn) : HTTPOutput(conn){
    splitTime = config->getInteger("targetFragmentLength");
    setFileName = false;
    videoFragSeqNum = 0;
    audioFragSeqNum = 0;
    audioMdatBufferSize = 0;
    videoMdatBufferSize = 0;
    targetParams["append"] = true;
    HTTP::URL target(config->getString("target"));
    audioSegmentPrefix = "/" + target.path.substr(0, target.path.rfind("/") + 1) + "audio/segments_index/";
    videoSegmentPrefix = "/" + target.path.substr(0, target.path.rfind("/") + 1) + "video/segments_index/";
    playlistLocation = target.getFilePath();
    regulationInterval = 3600;
    currentFmp4Epoch = 0;
    prevRegulateSec = 0;
    thisFmp4Location = "";
    lastInitLine = "";
    audioFirstRun = true;
    videoFirstRun = true;
    startAudioPktTime = 0;
    prevFileTime = 0;
    prevAudioPktTime = 0;
    prevVideoPktTime = 0;
    firstKeyPktTime = 0;
    thisPktSize = 0;
    prevVGetTime = 0;
    prevAGetTime = 0;
    bootTime = Util::unixMS()/1000;

    char *intervalEnvVal = getenv("VISION_NDVR_REGULATE_INTERVAL");
    if (intervalEnvVal){
      try{
        std::string regulationIntervalStr = std::string(intervalEnvVal);
        if (!regulationIntervalStr.empty()){
          // Regulate every these many seconds
          regulationInterval = std::stoi(regulationIntervalStr);
        }
      }catch (const std::exception &ex){
        ERROR_MSG("[RTMPServer] [regulate] Cannot set regulationInterval: '%s'", ex.what());
      }
    }else{
      INFO_MSG("[RTMPServer] [regulate] env VISION_NDVR_REGULATE_INTERVAL is not set.");
    }
    indexMaxLines = 6000;
    char *indexMaxLinesVal = getenv("VISION_NDVR_INDEX_MAX_LINES");
    if (indexMaxLinesVal){
      try{
        std::string indexMaxLinesStr = std::string(indexMaxLinesVal);
        if (!indexMaxLinesStr.empty()){
          // Regulate every these many seconds
          indexMaxLines = std::stoi(indexMaxLinesStr);
        }
      }catch (const std::exception &ex){
        ERROR_MSG("[RTMPServer] [regulate] Cannot set indexMaxLines: '%s'", ex.what());
      }
    }else{
      INFO_MSG("[RTMPServer] [regulate] env VISION_NDVR_INDEX_MAX_LINES is not set.");
    }
  }
  OutfMP4::~OutfMP4(){}

  void OutfMP4::init(Util::Config *cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "FMP4";
    capa["friendly"] = "fMP4 over file socket";
    capa["desc"] = "Pseudostreaming in fMP4 format over file socket";
    capa["url_match"][2u] = "/$.m4s";
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("HEVC");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("MP3");
    capa["codecs"][0u][1u].append("AC3");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/video/mp4";
    capa["methods"][0u]["hrn"] = "MP4 progressive";
    capa["methods"][0u]["priority"] = 1;
    capa["methods"][0u]["url_rel"] = "/$.m4s";
    capa["optional"]["targetFragmentLength"]["name"] = "Length of fMP4 files (ms)";
    capa["optional"]["targetFragmentLength"]["help"] = "Target time duration in milliseconds for fMP4 files, when outputting to disk.";
    capa["optional"]["targetFragmentLength"]["option"] = "--targetLength";
    capa["optional"]["targetFragmentLength"]["type"] = "uint";
    capa["optional"]["targetFragmentLength"]["default"] = 5000;
    capa["push_urls"].append("*.m3u");

    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "/ndvr/" + cfg->getString("streamname") + "/index.m3u";
    opt["arg_num"] = 1;
    opt["help"] = "Target filename to store fMP4 playlist file as, '*.m3u' for writing to a playlist.";
    cfg->addOption("target", opt);
    opt.null();

    opt["arg"] = "integer";
    opt["long"] = "targetFragmentLength";
    opt["short"] = "l";
    opt["help"] = "Target time duration in seconds for fmp4 files, when outputting to disk.";
    opt["value"].append(5000);
    config->addOption("targetFragmentLength", opt);
  }

  bool OutfMP4::isFileTarget(){
    return true;
  }

  uint64_t OutfMP4::mp4moofSize(uint64_t firstPartIdx, uint64_t lastPartIdx, size_t trackID){
    uint64_t tmpRes = 0;
    if (M.getType(trackID) != "objects"){
      // TODO: Elaborate the numbers
      tmpRes += 108 + (lastPartIdx - firstPartIdx + 1)*12;
    }
    return tmpRes;
  }

  void OutfMP4::sendMoov(size_t trackID){
    MP4::FTYP ftypBox;
    ftypBox.setMajorBrand("isom");
    ftypBox.setCompatibleBrands("cmfc", 0);
    ftypBox.setCompatibleBrands("isom", 1);
    ftypBox.setCompatibleBrands("dash", 2);
    ftypBox.setCompatibleBrands("iso9", 3);
    myConn.SendNow(ftypBox.asBox(), ftypBox.boxedSize());

    MP4::MOOV moovBox;

    MP4::MVHD mvhdBox(0);
    mvhdBox.setTrackID(0xFFFFFFFF); // This value needs to point to an unused trackid
    uint32_t moovOffset = 0;
    moovBox.setContent(mvhdBox, moovOffset++);
    for (std::map<size_t, Comms::Users>::const_iterator it = userSelect.begin(); it != userSelect.end(); ++it){
      if (it->first != trackID){continue;}
      std::string tType = M.getType(it->first);
      trackID = it->first;
      MP4::TRAK trakBox;

      MP4::TKHD tkhdBox(M, trackID);
      tkhdBox.setDuration(0);
      trakBox.setContent(tkhdBox, 0);

      MP4::MDIA mdiaBox;

      MP4::MDHD mdhdBox(0, M.getLang(trackID));
      mdiaBox.setContent(mdhdBox, 0);

      MP4::HDLR hdlrBox(tType, M.getType(trackID));
      mdiaBox.setContent(hdlrBox, 1);

      MP4::MINF minfBox;

      if (tType == "video"){
        MP4::VMHD vmhdBox;
        vmhdBox.setFlags(1);
        minfBox.setContent(vmhdBox, 0);
      }else if (tType == "audio"){
        MP4::SMHD smhdBox;
        minfBox.setContent(smhdBox, 0);
      }else{
        MP4::NMHD nmhdBox;
        minfBox.setContent(nmhdBox, 0);
      }

      MP4::DINF dinfBox;
      MP4::DREF drefBox;
      dinfBox.setContent(drefBox, 0);
      minfBox.setContent(dinfBox, 1);

      MP4::STBL stblBox;

      // Add STSD box
      MP4::STSD stsdBox(0);
      if (tType == "video"){
        MP4::VisualSampleEntry sampleEntry(M, trackID);
        MP4::BTRT btrtBox;
        btrtBox.setDecodingBufferSize(0xFFFFFFFFull);
        btrtBox.setAverageBitrate(M.getBps(trackID));
        btrtBox.setMaxBitrate(M.getMaxBps(trackID));

        sampleEntry.setBoxEntry(sampleEntry.getBoxEntryCount(), btrtBox);
        stsdBox.setEntry(sampleEntry, 0);
      }else if (tType == "audio"){
        MP4::AudioSampleEntry sampleEntry(M, trackID);
        MP4::BTRT btrtBox;
        btrtBox.setDecodingBufferSize(0xFFFFFFFFull);
        btrtBox.setAverageBitrate(M.getBps(trackID));
        btrtBox.setMaxBitrate(M.getMaxBps(trackID));

        sampleEntry.setBoxEntry(sampleEntry.getBoxEntryCount(), btrtBox);
        stsdBox.setEntry(sampleEntry, 0);
      }else if (tType == "meta"){
        MP4::TextSampleEntry sampleEntry(M, trackID);

        MP4::FontTableBox ftab;
        sampleEntry.setFontTableBox(ftab);
        stsdBox.setEntry(sampleEntry, 0);
      }

      stblBox.setContent(stsdBox, 0);

      MP4::STTS sttsBox(0);
      stblBox.setContent(sttsBox, 1);
      MP4::STSC stscBox(0);
      stblBox.setContent(stscBox, 2);
      MP4::STSZ stszBox(0);
      stblBox.setContent(stszBox, 3);
      MP4::STCO stcoBox(0);
      stblBox.setContent(stcoBox, 4);

      minfBox.setContent(stblBox, 2);
      mdiaBox.setContent(minfBox, 2);
      trakBox.setContent(mdiaBox, 1);
      moovBox.setContent(trakBox, moovOffset++);
    }

    MP4::MVEX mvexBox;
    size_t mvexOffset = 0;
    for (std::map<size_t, Comms::Users>::const_iterator it = userSelect.begin(); it != userSelect.end(); ++it){
      if (it->first != trackID){continue;}
      if (M.getCodec(it->first) == "objects"){continue;}
      MP4::TREX trexBox(it->first+1);
      trexBox.setDefaultSampleDuration(1000);
      mvexBox.setContent(trexBox, mvexOffset++);
    }

    moovBox.setContent(mvexBox, moovOffset++);
    myConn.SendNow(moovBox.asBox(), moovBox.boxedSize());
  }

  void OutfMP4::sendMoof(uint64_t startFragmentTime, uint64_t endFragmentTime, uint32_t mdatSize, size_t trackID){
    std::string trackType = M.getType(trackID);
    MP4::MOOF moofBox;
    MP4::MFHD mfhdBox;
    if (trackType == "video"){
      mfhdBox.setSequenceNumber(++videoFragSeqNum);
    }
    if (trackType == "audio"){
      mfhdBox.setSequenceNumber(++audioFragSeqNum);
    }
    // Create box MOOF -> MFHD
    uint8_t moofOffset = 0;
    uint64_t relativeOffset = 0;
    uint64_t totalPartsSize = 0;
    moofBox.setContent(mfhdBox, moofOffset);
    // Loop through all tracks
    for (std::map<size_t, Comms::Users>::const_iterator subIt = userSelect.begin(); subIt != userSelect.end(); ++subIt){
      if (trackID != subIt->first){continue;}
      if (M.getCodec(trackID) == "objects"){continue;}
      size_t firstPart, endPart;
      if (trackType == "video"){
        endPart = M.getPrevToKeyPartTimeIndex(endFragmentTime, trackID);
        if (videoFirstRun){
          videoFirstRun = false;
          firstPart = M.getFirstKeyPart(startFragmentTime, trackID);
        }else{
          firstPart = videoPrevEndPart + 1;
        }
        videoPrevEndPart = endPart;
      }else if (trackType == "audio"){
        endPart = M.getPartIndex(endFragmentTime, trackID);
        if (audioFirstRun){
          audioFirstRun = false;
          firstPart = M.getPartIndex(startFragmentTime, trackID);
        }else{
          firstPart = audioPrevEndPart + 1;
        }
        audioPrevEndPart = endPart;
      }
      if (endPart == INVALID_RECORD_INDEX || firstPart == INVALID_RECORD_INDEX){
        FAIL_MSG("[RTMPServer] No valid part found! Exiting...");
        // Close the input, expecting a restart and clean buffer without errors.
        config->is_active = false;
        nukeStream(streamName);
        return;
      }
      HIGH_MSG("[RTMPServer] For TrackID: %s: firstPart = %u, endPart = %u", trackType.c_str(), firstPart, endPart);
      MP4::TRAF trafBox;
      MP4::TFHD tfhdBox;
      tfhdBox.setFlags(MP4::tfhdSampleFlag | MP4::tfhdBaseIsMoof | MP4::tfhdSampleDesc);
      tfhdBox.setTrackID(trackID + 1);
      tfhdBox.setDefaultSampleDuration((trackType == "audio") ? AUDIO_KEY_INTERVAL : 444);
      tfhdBox.setDefaultSampleSize(444);
      tfhdBox.setDefaultSampleFlags((trackType == "video")
                                      ? (MP4::noIPicture | MP4::noKeySample)
                                      : (MP4::isIPicture | MP4::isKeySample));
      tfhdBox.setSampleDescriptionIndex(1);
      trafBox.setContent(tfhdBox, 0);
      MP4::TFDT tfdtBox;
      tfdtBox.setBaseMediaDecodeTime(startFragmentTime);
      trafBox.setContent(tfdtBox, 1);
      MP4::TRUN trunBox;
      trunBox.setFlags(
                        MP4::trundataOffset |
                        MP4::trunfirstSampleFlags |
                        MP4::trunsampleSize |
                        MP4::trunsampleDuration |
                        MP4::trunsampleOffsets
                      );
      relativeOffset = mp4moofSize(firstPart, endPart, trackID);
      DTSC::Parts parts(M.parts(trackID));
      trunBox.setDataOffset(relativeOffset);
      trunBox.setFirstSampleFlags(MP4::isIPicture | MP4::isKeySample);
      size_t trunOffset = 0;
      for (size_t p = firstPart; p <= endPart; ++p){
        MP4::trunSampleInformation sampleInfo;
        sampleInfo.sampleSize = parts.getSize(p);
        totalPartsSize += sampleInfo.sampleSize;
        sampleInfo.sampleDuration = parts.getDuration(p);
        // Keep the sampleOffset 0 so that we do not rely on
        // invalid offset (PTS) and keep it same as DTS
        sampleInfo.sampleOffset = 0;
        trunBox.setSampleInformation(sampleInfo, trunOffset++);
      }
      trafBox.setContent(trunBox, 2);
      // Increment the offset for each track
      moofBox.setContent(trafBox, ++moofOffset);
    }
    uint64_t estimatedMoofSize = relativeOffset - 8;
    uint64_t actualMoofSize = moofBox.boxedSize();
    HIGH_MSG("[RTMPServer] For TrackID: %s estimated moof size = %u, actual moof size = %u",
              trackType.c_str(),
              estimatedMoofSize,
              actualMoofSize
            );
    HIGH_MSG("[RTMPServer] %s mdat size = %u, parts size = %u",
              trackType.c_str(),
              mdatSize,
              totalPartsSize
            );
    if (estimatedMoofSize != actualMoofSize){
      FAIL_MSG("[RTMPServer] MOOF size mismatch! exiting recording.");
      // Close the input, expecting a restart and clean buffer without errors.
      config->is_active = false;
      nukeStream(streamName);
      return;
    }
    if (mdatSize != totalPartsSize){
      FAIL_MSG("[RTMPServer] MDAT size mismatch! exiting recording.");
      // Close the input, expecting a restart and clean buffer without errors.
      config->is_active = false;
      nukeStream(streamName);
      return;
    }
    myConn.SendNow(moofBox.asBox(), moofBox.boxedSize());

    char mdatHeader[8] = {0x00, 0x00, 0x00, 0x00, 'm', 'd', 'a', 't'};
    /* The following fills the first four bytes of the above mdatHeader
    with the actual size of the mdat media content */
    Bit::htobl(mdatHeader, mdatSize+8);
    myConn.SendNow(mdatHeader, 8);
  }

  void OutfMP4::bufferData(const std::string &trackType){
    // Obtain a pointer to the data of this packet and store it's length
    char *dataPointer = 0;
    thisPacket.getString("data", dataPointer, thisPktSize);

    // We keep buffering the video data until:
    // - split seconds were reached, and
    // - a video keyframe was found
    if (trackType == "video"){
      // Now append the incoming data
      videoMdatBuffer.append(dataPointer, thisPktSize);
      videoMdatBufferSize += thisPktSize;
      INSANE_MSG("[RTMPServer] Video thisPktTime = %u %s", thisPktTime, thisPacket.getFlag("keyframe")?"(key)":"");
      videoTrackID = thisPacket.getTrackId();
    }

    // Keep buffering the audio data until:
    // - video split point was reached
    if (trackType == "audio"){
      if (!startAudioPktTime){
        startAudioPktTime = thisPacket.getTime();
      }
      audioMdatBuffer.append(dataPointer, thisPktSize);
      audioMdatBufferSize += thisPktSize;
      INSANE_MSG("[RTMPServer] Audio thisPktTime = %u", thisPktTime);
      audioTrackID = thisPacket.getTrackId();
    }

    // check if buffer has not gone beyond a safe limit (50 MB)
    if (videoMdatBufferSize > MAX_BUFFER_BYTES){
      ERROR_MSG("[RTMPServer] Buffer exceeded safe limit - closing input");
      config->is_active = false;
      nukeStream(streamName);
    }
  }

  void OutfMP4::sendFirst(){
    // This will also create the video/ directory
    thisInitLocation = videoSegmentPrefix + "init_" + currentUnixTime + ".m4s";
    if (!switchFile(thisInitLocation)){
      onFinish();
      return;
    }
    // Write an init_epoch.m4s for audio
    sendMoov(videoTrackID);
    lastInitTime = currentUnixTime;
    std::string targetLocation = videoSegmentPrefix.substr(0, videoSegmentPrefix.size() - 15) + "index.m3u";
    std::string thisUriText = TAG_MAP_URI + std::string("\"") + thisInitLocation + std::string("\"");
    std::string discontinuityWithUriText = TAG_SEQ_DISCONTINUITY + std::string("\n") + TAG_DISCONTINUITY + std::string("\n") + thisUriText;
    // Write init segment of the upcoming segment file to the manifest
    writeManifest(targetLocation, discontinuityWithUriText);
    if (audioMdatBufferSize){
      targetLocation = audioSegmentPrefix + "init_" + currentUnixTime + ".m4s";
      // This will also create the audio/ directory
      if (!switchFile(targetLocation)){
        onFinish();
        return;
      }
      // Write an init_epoch.m4s for audio
      sendMoov(audioTrackID);
      targetLocation = audioSegmentPrefix.substr(0, audioSegmentPrefix.size() - 15) + "index.m3u";
      thisInitLocation = audioSegmentPrefix + "init_" + currentUnixTime + ".m4s";
      thisUriText = TAG_MAP_URI + std::string("\"") + thisInitLocation + std::string("\"");
      discontinuityWithUriText = TAG_SEQ_DISCONTINUITY + std::string("\n") + TAG_DISCONTINUITY + std::string("\n") + thisUriText;
      // Write init segment of the upcoming segment file to the manifest
      writeManifest(targetLocation, discontinuityWithUriText);
    }

    // Write the master manifest with info of all the present tracks
    std::string master = "#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-INDEPENDENT-SEGMENTS\n";
    if (audioMdatBufferSize){
      std::string audioInfo = "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"audio\",AUTOSELECT=YES,URI=\"";
      audioInfo += audioSegmentPrefix.substr(0, audioSegmentPrefix.size() - 15) + "index.m3u";
      audioInfo += "\"\n";
      master += audioInfo;
    }
    master += "#EXT-X-STREAM-INF:BANDWIDTH=" + std::to_string(M.getBps(videoTrackID)) + \
      ",RESOLUTION=" + std::to_string(M.getWidth(videoTrackID)) + "x" + std::to_string(M.getHeight(videoTrackID)) + \
      ",CODECS=\"" + Util::codecString(M.getCodec(videoTrackID), M.getInit(videoTrackID));
    if (audioMdatBufferSize){
      master += "," + Util::codecString(M.getCodec(audioTrackID), M.getInit(audioTrackID)) + "\",AUDIO=\"audio\"";
    }else{
      master += "\"";
    }
    master += "\n" + videoSegmentPrefix.substr(0, videoSegmentPrefix.size() - 15) + "index.m3u";
    writeManifest(playlistLocation, master, true);

    setFileName = true;
  }

  void OutfMP4::sendInit(){
    INFO_MSG("[RTMPServer] Sending periodic init (15 minutes later)");

    // VIDEO:
    lastInitTime = currentUnixTime;
    std::string thisUriText;
    std::string targetLocation;
    std::string discontinuityWithUriText;
    if (videoMdatBufferSize){
      thisInitLocation = videoSegmentPrefix + "init_" + currentUnixTime + ".m4s";
      if (!switchFile(thisInitLocation)){
        onFinish();
        return;
      }
      // Write "video/init_epoch.m4s"
      sendMoov(videoTrackID);
      targetLocation = videoSegmentPrefix.substr(0, videoSegmentPrefix.size() - 15) + "index.m3u";
      thisUriText = TAG_MAP_URI + std::string("\"") + thisInitLocation + std::string("\"");
      discontinuityWithUriText = TAG_DISCONTINUITY + std::string("\n") + thisUriText;
      // Write init segment of the upcoming segment file
      writeManifest(targetLocation, discontinuityWithUriText);
    }

    // AUDIO:
    if (audioMdatBufferSize){
      thisInitLocation = audioSegmentPrefix + "init_" + currentUnixTime + ".m4s";
      if (!switchFile(thisInitLocation)){
        onFinish();
        return;
      }
      // Write "audio/init_epoch.m4s"
      sendMoov(audioTrackID);
      targetLocation = audioSegmentPrefix.substr(0, audioSegmentPrefix.size() - 15) + "index.m3u";
      thisUriText = TAG_MAP_URI + std::string("\"") + thisInitLocation + std::string("\"");
      discontinuityWithUriText = TAG_DISCONTINUITY + std::string("\n") + thisUriText;
      // Write init segment of the upcoming segment file
      writeManifest(targetLocation, discontinuityWithUriText);
    }
  }

  void OutfMP4::flushVideo(){
    if (videoMdatBufferSize){
      thisFmp4Location = videoSegmentPrefix + currentUnixTime + ".m4s";
      // Write the actual media segment file
      if (!switchFile(thisFmp4Location)){
        onFinish();
        return;
      }

      // Sends all the header boxes starting from MOOF until MDAT header
      VERYHIGH_MSG("[RTMPServer] Sending moof with firstKeyPktTime = %u, prevVideoPktTime = %u", firstKeyPktTime, prevVideoPktTime);
      // We send endFragmentTime as thisPktTime because getPrevToKeyPartTimeIndex()
      // will find the part previous to this keyframe given the timestamp
      sendMoof(firstKeyPktTime, thisPktTime, videoMdatBufferSize-thisPktSize, videoTrackID);
      if (!config->is_active){
        return;
      }
      // Send the actual media data on the socket connection and hence the file
      myConn.SendNow(videoMdatBuffer, videoMdatBufferSize-thisPktSize);
      // Resize the buffer to only contain the keyframe for next fragment only
      videoMdatBuffer.shift(videoMdatBufferSize-thisPktSize);
      // Reset the media buffer size for next file
      videoMdatBufferSize = thisPktSize;
      // Write content to the playlist as well
      std::string targetLocation = videoSegmentPrefix.substr(0, videoSegmentPrefix.size() - 15) + "index.m3u";
      std::string extInf = TAG_EXTINF + std::to_string((double)(prevVideoPktTime - firstKeyPktTime)/1000) + ",\n";
      std::string segmentInfo = extInf + thisFmp4Location;
      writeManifest(targetLocation, segmentInfo);
    }
  }

  void OutfMP4::flushAudio(){
    if (audioMdatBufferSize){
      thisFmp4Location = audioSegmentPrefix + currentUnixTime + ".m4s";
      // Write the actual media segment file
      if (!switchFile(thisFmp4Location)){
        onFinish();
        return;
      }

      // Sends from all the header boxes and fill moof until mdat header
      VERYHIGH_MSG("[RTMPServer] Sending moof with startAudioPktTime = %u, prevAudioPktTime = %u", startAudioPktTime, prevAudioPktTime);
      sendMoof(startAudioPktTime, prevAudioPktTime, audioMdatBufferSize, audioTrackID);
      if (!config->is_active){
        return;
      }
      // Send the actual media data on the socket connection and hence the file
      myConn.SendNow(audioMdatBuffer, audioMdatBufferSize);
      // Clear the buffer
      audioMdatBuffer.clear();
      // Reset the media buffer size for next file
      audioMdatBufferSize = 0;
      // Write content to the playlist as well
      std::string targetLocation = audioSegmentPrefix.substr(0, audioSegmentPrefix.size() - 15) + "index.m3u";
      std::string extInf = TAG_EXTINF + std::to_string((double)(prevAudioPktTime - startAudioPktTime)/1000) + ",";
      std::string segmentInfo = extInf + "\n" + thisFmp4Location;
      writeManifest(targetLocation, segmentInfo);
    }
  }

  void OutfMP4::performPublish(uint64_t currentFileTime, uint64_t pktInterval){
    // Comparision to avoid same file names
    if (currentFileTime == prevFileTime){
      ++currentFileTime;
      FAIL_MSG("[RTMPServer] Previous segment time %d is the same as current, incrementing to %d", prevFileTime, currentFileTime);
    }

    currentUnixTime = JSON::Value(currentFileTime).asString();
    HIGH_MSG("[RTMPServer] Splitting after %u ms", (pktInterval));

    // Write init (data and manifest) of both audio and video on boot
    if (!setFileName){
      sendFirst();
    }

    // Send init (data and manifest) of both audio and video after every 15 minutes
    if ((std::stoull(currentUnixTime) - std::stoull(lastInitTime)) > 900ULL){
      sendInit();
    }

    // Write buffered (and valid) video data and manifest to their respective files
    flushVideo();
    // Write buffered (and valid) audio data and manifest to their respective files
    flushAudio();

    // State reset block
    {
      // Store this packet's (key's) time to start data from the next fragment
      firstKeyPktTime = thisPktTime;
      // Save this packet's time as the last sent header time
      startAudioPktTime = prevAudioPktTime;
      // Store for comparison to avoid same file names
      prevFileTime = currentFileTime;
    }
  }

  void OutfMP4::performRegulate(){
    // Regulate the playlist by time and length when:
    // - the process boots up
    // - after every regulation interval was reached/passed during runtime
    uint64_t currentSecs = Util::bootSecs();
    if (!prevRegulateSec || (currentSecs - prevRegulateSec >= regulationInterval)){
      // Store previous regulate seconds for time comparisions later
      prevRegulateSec = currentSecs;

      // Regulate video manifest file
      std::string targetLocation = videoSegmentPrefix.substr(0, videoSegmentPrefix.size() - 15) + "index.m3u";
      regulateManifest(targetLocation);

      // Regulate audio manifest file
      targetLocation = audioSegmentPrefix.substr(0, audioSegmentPrefix.size() - 15) + "index.m3u";
      regulateManifest(targetLocation);
    }
  }

  void OutfMP4::sendNext(){
    // Store the to-be current file name's epoch
    uint64_t currentFileTime = Util::unixMS()/1000;

    // Check for valid data in the packet
    if (!thisPacket.getData()){
      FAIL_MSG("`thisPacket.getData()` is invalid.");
      return;
    }

    // Return if this is not media data
    if (M.getCodec(thisIdx) == "objects"){return;}

    // If manifest files are not updated in last 30 seconds, restart the output
    if (prevFileTime && currentFileTime - prevFileTime >= MAX_DATA_WAIT_SEC*2){
      FAIL_MSG("[RTMPServer] Playlist not updated for %u seconds - exiting", MAX_DATA_WAIT_SEC*2);
      config->is_active = false;
      nukeStream(streamName);
      return;
    }

    // If manifest files are not even initialized in 30 seconds, restart the output
    if (!setFileName && currentFileTime - bootTime >= MAX_DATA_WAIT_SEC*2){
      FAIL_MSG("[RTMPServer] Playlist not initialised for %u seconds - exiting", MAX_DATA_WAIT_SEC*2);
      config->is_active = false;
      nukeStream(streamName);
      return;
    }

    // Store this packet's time
    thisPktTime = thisPacket.getTime();

    // Store this packet's track type
    std::string trackType = M.getType(thisIdx);

    if (trackType == "video"){
      prevVGetTime = Util::bootMS();
    }
    if (trackType == "audio"){
      prevAGetTime = Util::bootMS();
    }
    uint64_t trackDiffAV = (prevVGetTime > prevAGetTime)
                         ? (prevVGetTime - prevAGetTime)
                         : (prevAGetTime - prevVGetTime);
    if (prevVGetTime && prevAGetTime){
      if (((splitTime > 1000) && (trackDiffAV >= (splitTime - 1000))) || (splitTime <= 1000  && (trackDiffAV >= 1000))){
        FAIL_MSG("[RTMPServer] Video and audio received more than %ums apart in FMP4! exiting.", trackDiffAV);
        // Close the input, expecting a restart and clean buffer without errors.
        config->is_active = false;
        nukeStream(streamName);
        return;
      }
    }

    if (!firstKeyPktTime && trackType == "video"){
      // First key time will be the first packet's time (after seek)
      firstKeyPktTime = thisPktTime;
    }

    // Buffer the current data into a Resizable pointer
    bufferData(trackType);
    if (!config->is_active){
      return;
    }

    uint64_t pktInterval = thisPktTime - firstKeyPktTime;
    if ((pktInterval > splitTime) && (trackType == "video") && (thisPacket.getFlag("keyframe"))){
      performPublish(currentFileTime, pktInterval-thisPktTime);
      if (!config->is_active){
        return;
      }
      performRegulate();
    }

    // Store the previous audio packet's time
    if (trackType == "video"){
      prevVideoPktTime = thisPktTime;
    }
    // Store the previous audio packet's time
    if (trackType == "audio"){
      prevAudioPktTime = thisPktTime;
    }
  }

  void OutfMP4::sendHeader(){
    /* This function is called only when the process boots to do an initial seek to the nearest keyframe */
    if (!M.getLive()){
      FAIL_MSG("[RTMPServer] fMP4 output not supported for non-live input!");
      wantRequest = false;
      parseData = false;
      return;
    }
    initialSeek();
    sentHeader = true;
  }

  void OutfMP4::regulateManifest(std::string playlistLoc){
    // Try opening the playlist file
    std::ifstream fileInDesc(playlistLoc.c_str());
    if (!fileInDesc.is_open()){
      INFO_MSG("[RTMPServer] [regulate] Could not open playlist file '%s'!", playlistLoc.c_str());
      return;
    }
    // Read and store all the lines, and close the playlist file
    std::vector<std::string> lines;
    std::string line;
    bool hasSegment = false;
    uint16_t lineSize;
    uint16_t streamNameSize = streamName.size();
    while (std::getline(fileInDesc, line)){
      if (line.empty()){
        continue;
      }
      lineSize = line.size();
      // Check if the line has ".m4s"
      if (!hasSegment && (lineSize > streamNameSize) && (line.substr(lineSize - 4, 4) == ".m4s")){
        hasSegment = true;
      }
      lines.push_back(line);
    }
    fileInDesc.close();
    // Skip regulation if the file was already empty
    if (lines.empty()){
      INFO_MSG("[RTMPServer] [regulate] Skipping regulating an empty manifest '%s'!", playlistLoc.c_str());
      return;
    }
    if (!hasSegment){
      WARN_MSG("[RTMPServer] [regulate] Removing all manifest content which has no segment '%s'!", playlistLoc.c_str());
      // TODO: Flawed here, this does not empty the file :/
      lines.clear();
    }
    // Try to remove old fMP4 lines
    try { removeOldFmp4(lines); } catch (const std::exception &ex){
      ERROR_MSG("[RTMPServer] [regulate] Cannot remove old fMP4 playlist '%s'!", playlistLoc.c_str());
      return;
    }
    // Write the modified manifest lines back to the file
    std::ofstream fileOutDesc(playlistLoc.c_str(), std::ios::trunc);
    if (fileOutDesc.is_open()){
      for (const std::string &line : lines){
          if (line.empty()){continue;}
          fileOutDesc << line << "\n";
      }
      fileOutDesc.close();
    }else{
      FAIL_MSG("[RTMPServer] [regulate] Could not open the playlist to write new content '%s'!", playlistLoc.c_str());
    }
  }

  uint32_t OutfMP4::removeOldFmp4(std::vector<std::string> &playlist){
    if (playlist.size() == 0){
      return 0;
    }
    uint32_t chunkTime = 0;
    size_t trimIdx = 0;

    // +2 because we will keep the discontinuity tag and most recent (previous) init
    if (playlist.size() > indexMaxLines+2){
      WARN_MSG("[RTMPServer] [regulate] Reducing size of overflown m3u playlist");
      size_t linesInPlaylist = std::min(indexMaxLines, static_cast<size_t>(8000));
      // Reverse iterate starting from `linesInPlaylist` and the store the most recent (previous) init
      std::string thisLine, recentInit;
      for (int i = linesInPlaylist-1; i >= 0; --i){
        thisLine = playlist[i];
        if (thisLine.size() >= streamName.size() && thisLine.substr(thisLine.size() - 4, 4) == ".m4s" && thisLine.substr(0, 10) == "#EXT-X-MAP"){
          recentInit = thisLine;
          break;
        }
      }
      playlist.erase(playlist.begin(), playlist.begin() + (playlist.size() - linesInPlaylist));
      playlist.insert(playlist.begin(), TAG_DISCONTINUITY);
      if (recentInit.size()){
        playlist.insert(playlist.begin()+1, recentInit);
      }else{
        // In case of an error in finding init,
        // we anyway have to add another line to prevent infinite recursion
        playlist.insert(playlist.begin()+1, TAG_DISCONTINUITY);
      }
      chunkTime = removeOldFmp4(playlist);
      return chunkTime;
    }

    DEVEL_MSG("[RTMPServer] [regulate] Playlist length before trimming --> '%d'", playlist.size());

    bool hasFmp4 = false;
    std::string thisLine;
    bool foundInit = false;
    for (; trimIdx < playlist.size(); ++trimIdx){
      thisLine = playlist[trimIdx];
      if (thisLine.size() > streamName.size() && thisLine.substr(thisLine.size() - 4, 4) == ".m4s" && thisLine.substr(0, 10) != "#EXT-X-MAP"){
        if (!hasFmp4){
          hasFmp4 = true;
        }
        chunkTime = std::stoi(thisLine.substr(thisLine.rfind("/") + 1, thisLine.size() - thisLine.rfind("/") - 5));
        currentFmp4Epoch = std::stoi(thisFmp4Location.substr(thisFmp4Location.rfind("/") + 1, thisFmp4Location.length() - thisFmp4Location.rfind("/") - 5));
        if ((currentFmp4Epoch - regulationInterval) <= chunkTime){
          break;
        }
      }
      if (thisLine.size() > streamName.size() && thisLine.substr(0, 10) == "#EXT-X-MAP"){
        lastInitLine = thisLine;
        foundInit = true;
      }
    }

    if (!hasFmp4){
      WARN_MSG("[RTMPServer] [regulate] clearing playlist that has no FMP4 files");
      playlist.clear();
      return 0;
    }
    // Also keep the EXTINF along with the first playlist path
    if (trimIdx && (playlist.size() >= trimIdx-1)){ trimIdx -= 1; }
    playlist.erase(playlist.begin(), playlist.begin() + trimIdx);
    // Insert the #EXT-X-DISCONTINUITY-SEQUENCE at beginning to determine at least one starting point of the process
    playlist.insert(playlist.begin(), TAG_SEQ_DISCONTINUITY);
    if (foundInit){
      playlist.insert(playlist.begin()+1, TAG_DISCONTINUITY);
      playlist.insert(playlist.begin()+2, lastInitLine);
    }else{
      WARN_MSG("[RTMPServer] [regulate] Could not find the init for the upcoming segments, this could lead to MP4 corruption");
    }
    DEVEL_MSG("[RTMPServer] [regulate] Playlist length after trimming --> '%d'", playlist.size());
    INFO_MSG("[RTMPServer] [regulate] Regulated playlist size by time");
    return chunkTime;
  }

  void OutfMP4::writeManifest(const std::string &targetLocation, const std::string &line, bool overwrite){
    if (line.empty()){
      ERROR_MSG("[RTMPServer] Attempted to write empty line to playlist!");
      return;
    }
    std::ofstream fileOutDesc;
    if (overwrite){
      INSANE_MSG("[RTMPServer] Opening file: %s in OUT Mode", targetLocation.c_str());
      fileOutDesc.open(targetLocation.c_str(), std::ios::out);
    }else{
      DONTEVEN_MSG("[RTMPServer] Opening file: %s in APPEND Mode", targetLocation.c_str());
      fileOutDesc.open(targetLocation.c_str(), std::ios::app);
    }
    if (fileOutDesc.is_open()){
      HIGH_MSG("[RTMPServer] Writing line of length %d to playlist", line.length());
      VERYHIGH_MSG("[RTMPServer] content -> %s", line.c_str());
      fileOutDesc << line << "\n";
      fileOutDesc.close();
    }else{
      FAIL_MSG("[RTMPServer] Cannot open file at the location '%s'", targetLocation.c_str());
    }
  }

  bool OutfMP4::switchFile(std::string &filePath){
    HIGH_MSG("[RTMPServer] New target path is %s", filePath.c_str());
    setenv("MST_ORIG_TARGET", filePath.c_str(), 1);
    if (!genericWriter(filePath)){
      FAIL_MSG("Failed to open file, aborting: %s", filePath.c_str());
      Util::logExitReason(ER_WRITE_FAILURE, "failed to open file, aborting: %s", filePath.c_str());
      onFinish();
      return false;
    }
    return true;
  }

  bool OutfMP4::onFinish(){
    HIGH_MSG("[RTMPServer] Closing output on finish");
    // clear both A/V buffers
    if (videoMdatBuffer.getSize() || audioMdatBuffer.getSize()){
      // We skip writing the remaining data because we cannot identify a keyframe here
      videoMdatBuffer.clear();
      audioMdatBuffer.clear();
    }
    stats(true);
    myConn.close();
    wantRequest = true;
    return false;
  }
}// namespace Mist