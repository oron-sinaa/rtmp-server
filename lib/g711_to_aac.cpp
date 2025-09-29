#include "g711_to_aac.h"

namespace AACConverter{
  // This only expects the following input configuration -
  // pcm_mulaw, 8000 Hz, mono, s16, 64 kb/s
  AACConverter::AACConverter(uint32_t inSampleRate, uint16_t inChannels, uint16_t outBitrate, uint32_t outBandwidth){
    isInitialized = true;
    // Initialize FAAC encoder
    faac = faacEncOpen(inSampleRate, inChannels, &numInputSamples, &maxOutAacBytes);
    INFO_MSG("[FAAC] Needs %d input PCM samples and will fill a max of %d", numInputSamples, maxOutAacBytes);
    if (faac == NULL){
      isInitialized = false;
      FAIL_MSG("[FAAC] Failed to initialize the encoder");
    }
    if (isInitialized){
      pcmBufferSize = 0;
      frameMsTime = 0;
      first = true;
      pcmBuffer = new int16_t[numInputSamples*8];
      // Set encoding parameters
      // https://github.com/knik0/faac/blob/master/docs/libfaac.pdf
      faacEncConfigurationPtr config = faacEncGetCurrentConfiguration(faac);
      config->inputFormat = FAAC_INPUT_16BIT;
      config->outputFormat = 0; // 0 Raw 1 ADTS
      config->useTns = 0;
      config->useLfe = 0;
      config->aacObjectType = LOW;
      config->shortctl = SHORTCTL_NOSHORT;
      config->mpegVersion = MPEG4;
      config->allowMidside = 0;
      config->bitRate = outBitrate*1000;
      config->bandWidth = outBandwidth;
      if (!faacEncSetConfiguration(faac, config)){
        isInitialized = false;
        FAIL_MSG("[FAAC] Failed to configure the encoder");
      }else{
        frameDuration = (double(numInputSamples) / (config->bandWidth * 2)) * 1000;
        INFO_MSG("[FAAC] FAAC output initialized with bitrate %llu and bandwidth %llu and frameDuration %llu", config->bitRate, config->bandWidth, frameDuration);
      }
    }
	}

  AACConverter::~AACConverter(){
    if (isInitialized){
      faacEncClose(faac);
      VERYHIGH_MSG("[FAAC] closed");
      delete[] pcmBuffer;
    }
  }

  static int16_t alawToPcm(uint8_t alaw){
    // https://github.com/GStreamer/gst-plugins-good/blob/master/gst/law/alaw-decode.c
    static const int16_t alawToLinearTable[256] = {
    -5504, -5248, -6016, -5760, -4480, -4224, -4992, -4736,
    -7552, -7296, -8064, -7808, -6528, -6272, -7040, -6784,
    -2752, -2624, -3008, -2880, -2240, -2112, -2496, -2368,
    -3776, -3648, -4032, -3904, -3264, -3136, -3520, -3392,
    -22016, -20992, -24064, -23040, -17920, -16896, -19968, -18944,
    -30208, -29184, -32256, -31232, -26112, -25088, -28160, -27136,
    -11008, -10496, -12032, -11520, -8960, -8448, -9984, -9472,
    -15104, -14592, -16128, -15616, -13056, -12544, -14080, -13568,
    -344, -328, -376, -360, -280, -264, -312, -296,
    -472, -456, -504, -488, -408, -392, -440, -424,
    -88, -72, -120, -104, -24, -8, -56, -40,
    -216, -200, -248, -232, -152, -136, -184, -168,
    -1376, -1312, -1504, -1440, -1120, -1056, -1248, -1184,
    -1888, -1824, -2016, -1952, -1632, -1568, -1760, -1696,
    -688, -656, -752, -720, -560, -528, -624, -592,
    -944, -912, -1008, -976, -816, -784, -880, -848,
    5504, 5248, 6016, 5760, 4480, 4224, 4992, 4736,
    7552, 7296, 8064, 7808, 6528, 6272, 7040, 6784,
    2752, 2624, 3008, 2880, 2240, 2112, 2496, 2368,
    3776, 3648, 4032, 3904, 3264, 3136, 3520, 3392,
    22016, 20992, 24064, 23040, 17920, 16896, 19968, 18944,
    30208, 29184, 32256, 31232, 26112, 25088, 28160, 27136,
    11008, 10496, 12032, 11520, 8960, 8448, 9984, 9472,
    15104, 14592, 16128, 15616, 13056, 12544, 14080, 13568,
    344, 328, 376, 360, 280, 264, 312, 296,
    472, 456, 504, 488, 408, 392, 440, 424,
    88, 72, 120, 104, 24, 8, 56, 40,
    216, 200, 248, 232, 152, 136, 184, 168,
    1376, 1312, 1504, 1440, 1120, 1056, 1248, 1184,
    1888, 1824, 2016, 1952, 1632, 1568, 1760, 1696,
    688, 656, 752, 720, 560, 528, 624, 592,
    944, 912, 1008, 976, 816, 784, 880, 848
    };

    return alawToLinearTable[alaw];
  }

  static int16_t ulawToPcm(uint8_t ulaw){
    // https://github.com/dpwe/dpwelib/blob/master/ulaw.c
    static const int16_t ulawToLinearTable[256] = {
    -32124, -31100, -30076, -29052, -28028, -27004, -25980, -24956,
    -23932, -22908, -21884, -20860, -19836, -18812, -17788, -16764,
    -15996, -15484, -14972, -14460, -13948, -13436, -12924, -12412,
    -11900, -11388, -10876, -10364, -9852, -9340, -8828, -8316,
    -7932, -7676, -7420, -7164, -6908, -6652, -6396, -6140,
    -5884, -5628, -5372, -5116, -4860, -4604, -4348, -4092,
    -3900, -3772, -3644, -3516, -3388, -3260, -3132, -3004,
    -2876, -2748, -2620, -2492, -2364, -2236, -2108, -1980,
    -1884, -1820, -1756, -1692, -1628, -1564, -1500, -1436,
    -1372, -1308, -1244, -1180, -1116, -1052, -988, -924,
    -876, -844, -812, -780, -748, -716, -684, -652,
    -620, -588, -556, -524, -492, -460, -428, -396,
    -372, -356, -340, -324, -308, -292, -276, -260,
    -244, -228, -212, -196, -180, -164, -148, -132,
    -120, -112, -104, -96, -88, -80, -72, -64,
    -56, -48, -40, -32, -24, -16, -8, 0,
    32124, 31100, 30076, 29052, 28028, 27004, 25980, 24956,
    23932, 22908, 21884, 20860, 19836, 18812, 17788, 16764,
    15996, 15484, 14972, 14460, 13948, 13436, 12924, 12412,
    11900, 11388, 10876, 10364, 9852, 9340, 8828, 8316,
    7932, 7676, 7420, 7164, 6908, 6652, 6396, 6140,
    5884, 5628, 5372, 5116, 4860, 4604, 4348, 4092,
    3900, 3772, 3644, 3516, 3388, 3260, 3132, 3004,
    2876, 2748, 2620, 2492, 2364, 2236, 2108, 1980,
    1884, 1820, 1756, 1692, 1628, 1564, 1500, 1436,
    1372, 1308, 1244, 1180, 1116, 1052, 988, 924,
    876, 844, 812, 780, 748, 716, 684, 652,
    620, 588, 556, 524, 492, 460, 428, 396,
    372, 356, 340, 324, 308, 292, 276, 260,
    244, 228, 212, 196, 180, 164, 148, 132,
    120, 112, 104, 96, 88, 80, 72, 64,
    56, 48, 40, 32, 24, 16, 8, 0
    };

    return ulawToLinearTable[ulaw];
  }

  void AACConverter::transcode(std::string inCodec, const char* inData, uint32_t inSize, unsigned char* &outAac, int32_t &outAacSize){
    /* --- Pre-validations start */
    outAacSize = -1;
    if (!isInitialized){
      FAIL_MSG("[FAAC] Not initialised");
      return;
    }
    if (inCodec != "ALAW" && inCodec != "ULAW"){
      FAIL_MSG("[FAAC] Unsupported codec %s provided", inCodec.c_str());
      return;
    }
    if (!inData || !inSize){
      FAIL_MSG("[FAAC] No %s input data provided", inCodec.c_str());
      return;
    }
    /* Pre-validations end --- */

    // Convert ALAW/ULAW to PCM using standard lookup table from ITU-T G.711
    int16_t* pcmData = new int16_t[inSize];
    if (inCodec == "ALAW"){
      for (int i = 0; i < inSize; ++i){
        pcmData[i] = alawToPcm(static_cast<uint8_t>(inData[i]));
      }
    }
    if (inCodec == "ULAW"){
      for (int i = 0; i < inSize; ++i){
        pcmData[i] = ulawToPcm(static_cast<uint8_t>(inData[i]));
      }
    }
    std::memcpy(pcmBuffer+(pcmBufferSize / sizeof(int16_t)), pcmData, inSize*sizeof(int16_t));
    delete[] pcmData;
    pcmBufferSize += inSize*sizeof(int16_t);
    VERYHIGH_MSG("[FAAC] Current size of pcmBuffer is %ub", pcmBufferSize);
    if (pcmBufferSize < numInputSamples*sizeof(int16_t)){
      return;
    }
    if (pcmBufferSize > numInputSamples*7){
      pcmBufferSize = 0;
      WARN_MSG("[FAAC] Cleared PCM buffer (%u) to prevent memory leak", pcmBufferSize);
      return;
    }
    HIGH_MSG("[FAAC] Sending %u PCM bytes for AAC conversion", numInputSamples*sizeof(int16_t));

    // Encode PCM data to AAC
    int bytesEncoded = faacEncEncode(faac, (int32_t*)pcmBuffer, numInputSamples, outAac, maxOutAacBytes);
    /* --- Conversion handling starts */
    if (bytesEncoded < 0){
      ERROR_MSG("[FAAC] Encoding failed with code %d", bytesEncoded);
      pcmBufferSize = 0;
      if (!first){
        WARN_MSG("[FAAC] Encoding failed, still incrementing frameMsTime");
        frameMsTime += frameDuration;
      }
      return;
    }else if (bytesEncoded == 0){
      WARN_MSG("[FAAC] Encoded %d PCM samples to ZERO AAC bytes", numInputSamples);
      outAacSize = 0;
      return;
    }else{
      if (first){
        HIGH_MSG("[FAAC] Set the first timestamp to %llu", frameMsTime);
        first = false;
      }else{
        frameMsTime += frameDuration;
      }
      memmove(pcmBuffer, pcmBuffer+numInputSamples, pcmBufferSize-numInputSamples*sizeof(int16_t));
      pcmBufferSize -= numInputSamples*sizeof(int16_t);
      outAacSize = bytesEncoded;
      INFO_MSG("[FAAC] (%ums) Encoded %d PCM samples to %d AAC bytes", frameMsTime, numInputSamples, bytesEncoded);
    }
    /* Conversion handling ends --- */

    VERYHIGH_MSG("[FAAC] Remaining PCM data after conversion is %ub", pcmBufferSize);
  }
}
