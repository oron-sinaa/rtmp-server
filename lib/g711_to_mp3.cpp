#include "g711_to_mp3.h"

namespace MP3Converter{
    MP3Converter::MP3Converter(uint32_t inSampleRate, uint16_t inChannels, uint16_t outBitrate){
      isInitialized = true;
      if ((lame = lame_init()) == NULL){
        isInitialized = false;
        FAIL_MSG("[LAME] Failed to intialize the encoder");
      }
      if (isInitialized){
        // Input parameters
        lame_set_in_samplerate(lame, inSampleRate);
        lame_set_num_channels(lame, inChannels);
        // Output parameters
        lame_set_mode(lame, (inChannels == 1) ? MONO : STEREO);
        lame_set_brate(lame, 128);
        lame_set_quality(lame, LAME_QUALITY);
        // General shix
        if (lame_init_params(lame) < 0){
          isInitialized = false;
          FAIL_MSG("[LAME] Failed to set the encoder's parameters");
        }
        if (isInitialized){
          INFO_MSG("[LAME] LAME initialized with sample rate: %u, channels: %u, bitrate: %u", lame_get_in_samplerate(lame), lame_get_num_channels(lame), lame_get_brate(lame));
        }
      }
    }

    MP3Converter::~MP3Converter(){
      lame_close(lame);
      VERYHIGH_MSG("[RTMPServer] LAME closed");
    }

    static int16_t alawToPcm(uint8_t alaw){
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
      static const int16_t ulawToLinearTable[256] = {
        -32124, -31100, -30076, -29052, -28028, -27004, -25980, -24956,
        -23932, -22908, -21884, -20860, -19836, -18812, -17788, -16764,
        -15740, -14716, -13692, -12668, -11644, -10620,  -9596,  -8572,
        -7548,  -6524,  -5500,  -4476,  -3452,  -2428,  -1404,   -380,
        -32124, -31100, -30076, -29052, -28028, -27004, -25980, -24956,
        -23932, -22908, -21884, -20860, -19836, -18812, -17788, -16764,
        -15740, -14716, -13692, -12668, -11644, -10620,  -9596,  -8572,
        -7548,  -6524,  -5500,  -4476,  -3452,  -2428,  -1404,   -380,
        32124,  31100,  30076,  29052,  28028,  27004,  25980,  24956,
        23932,  22908,  21884,  20860,  19836,  18812,  17788,  16764,
        15740,  14716,  13692,  12668,  11644,  10620,   9596,   8572,
        7548,   6524,   5500,   4476,   3452,   2428,   1404,    380,
        32124,  31100,  30076,  29052,  28028,  27004,  25980,  24956,
        23932,  22908,  21884,  20860,  19836,  18812,  17788,  16764,
        15740,  14716,  13692,  12668,  11644,  10620,   9596,   8572,
        7548,   6524,   5500,   4476,   3452,   2428,   1404,    380,
        -32124, -31100, -30076, -29052, -28028, -27004, -25980, -24956,
        -23932, -22908, -21884, -20860, -19836, -18812, -17788, -16764,
        -15740, -14716, -13692, -12668, -11644, -10620,  -9596,  -8572,
        -7548,  -6524,  -5500,  -4476,  -3452,  -2428,  -1404,   -380,
        -32124, -31100, -30076, -29052, -28028, -27004, -25980, -24956,
        -23932, -22908, -21884, -20860, -19836, -18812, -17788, -16764,
        -15740, -14716, -13692, -12668, -11644, -10620,  -9596,  -8572,
        -7548,  -6524,  -5500,  -4476,  -3452,  -2428,  -1404,   -380,
        32124,  31100,  30076,  29052,  28028,  27004,  25980,  24956,
        23932,  22908,  21884,  20860,  19836,  18812,  17788,  16764,
        15740,  14716,  13692,  12668,  11644,  10620,   9596,   8572,
        7548,   6524,   5500,   4476,   3452,   2428,   1404,    380,
        32124,  31100,  30076,  29052,  28028,  27004,  25980,  24956,
        23932,  22908,  21884,  20860,  19836,  18812,  17788,  16764,
        15740,  14716,  13692,  12668,  11644,  10620,   9596,   8572,
        7548,   6524,   5500,   4476,   3452,   2428,   1404,    380
      };

      return ulawToLinearTable[ulaw ^ 0xFF];
    }

    char* MP3Converter::toMemory(std::string filePath, size_t *fileSize){
      // Open the file
      std::ifstream file(filePath, std::ios::binary | std::ios::ate);
      if (!file.is_open()){
        WARN_MSG("[RTMPServer] Failed to open file: %s", filePath.c_str());
        throw std::runtime_error("[RTMPServer] Unable to open file: " + filePath);
      }

      // Get the file size
      std::streamsize size = file.tellg();
      file.seekg(0, std::ios::beg);

      // Allocate memory
      char* buffer = new char[size];  // +1 for null terminator

      // Read the file
      if (!file.read(buffer, size)) {
        delete[] buffer;
        FAIL_MSG("[RTMPServer] Error reading file: %s", filePath.c_str());
        throw std::runtime_error("Error reading file: " + filePath);
      }

      *fileSize = size;
      return buffer;
    }

    /**
     * @brief Convert a chunk of A-law/U-law data to MP3.
     *
     * @param inCodec String identifying input codec A-law/U-law.
     * @param inData  Pointer to the input A-law/U-law data.
     * @param inSize  Size of the input A-law/U-law data in bytes.
     *
     * @return  vector of 'char' containing fully encoded MP3 data.
     *
     * @note  The A-law/U-law samples each are of 1 byte. Therefore, number of samples = size of A-law/U-law data.
     *        The PCM samples each are of 2 byte. But, conversion to PCM from A-law/U-law does not change the number of samples.
     *        Hence, number_of_samples(A-law/U-law) == number_of_samples(PCM) == size of A-law/U-law data
     *
     * @note  The size of the MP3 buffer should be at least 1.25 times the size of the PCM data plus 7200 bytes.
     *        This ensures enough space for the worst-case scenario.
     *        Formula: mp3BufferSize >= (alawSize * 1.25) + 7200
    **/
    std::vector<char> MP3Converter::transcode(std::string inCodec, const char* inData, int inSize){
      if (!isInitialized){
        FAIL_MSG("[LAME] Not initialised");
        return {};
      }
      if (inCodec != "ALAW" && inCodec != "ULAW"){
        FAIL_MSG("[LAME] Unsupported codec %s provided", inCodec.c_str());
        return {};
      }
      if (!inData || !inSize){
        FAIL_MSG("[LAME] No %s input data provided", inCodec.c_str());
        return {};
      }
      HIGH_MSG("[LAME] Transcoder called with inCodec %s and inSize %d", inCodec.c_str(), inSize);
      // Ensure pcmBuffer is large enough
      std::vector<int16_t> pcmBuffer(inSize);
      if (inCodec == "ALAW"){
        for (uint16_t i = 0; i < inSize; ++i){
          pcmBuffer[i] = alawToPcm(static_cast<uint8_t>(inData[i]));
        }
      }
      if (inCodec == "ULAW"){
        for (uint16_t i = 0; i < inSize; ++i){
          pcmBuffer[i] = ulawToPcm(static_cast<uint8_t>(inData[i]));
        }
      }
      HIGH_MSG("[LAME] Converted %zu bytes of %s data to PCM", pcmBuffer.size(), inCodec.c_str());
      // Encode PCM to MP3
      int mp3BufferSize = (inSize * 1.25) + 7200;
      mp3Buffer.resize(mp3BufferSize);
      VERYHIGH_MSG("[LAME] mp3BufferSize is %d bytes", mp3BufferSize);
      int bytesWritten = lame_encode_buffer(
        lame,                                               /* global context handle         */
        pcmBuffer.data(),                                   /* PCM data for left channel     */
        NULL,                                               /* PCM data for right channel    */
        inSize,                                             /* number of samples per channel */
        reinterpret_cast<unsigned char*>(mp3Buffer.data()), /* pointer to encoded MP3 stream */
        mp3BufferSize                                       /* number of valid octets in this stream */
      );
      if (bytesWritten < 0){
        ERROR_MSG("[LAME] encoding failed:");
        switch (bytesWritten){
          case -1: ERROR_MSG("[LAME] MP3 buffer is not big enough");   break;
          case -2: ERROR_MSG("[LAME] malloc() problem");               break;
          case -3: ERROR_MSG("[LAME] lame_init_params() not called");  break;
          case -4: ERROR_MSG("[LAME] Psycho acoustic problems");       break;
          default: ERROR_MSG("[LAME] Unknown error in MP3 encoding");  break;
        }
      }else if (bytesWritten == 0){
        HIGH_MSG("[RTMPServer] 0 bytes encoded. May be an error");
      }else{
        HIGH_MSG("[RTMPServer] LAME encoded %d %s bytes to %d MP3 bytes", inSize, inCodec.c_str(), bytesWritten);
        std::vector<char> encodedMP3(mp3Buffer.begin(), mp3Buffer.begin() + bytesWritten);
        mp3Buffer.erase(mp3Buffer.begin(), mp3Buffer.begin() + bytesWritten);
        return encodedMP3;
      }
      return {};
    }

    /**
     * @brief Flushes the intenal PCM buffers, padding with 0's to make sure the final frame is complete,
     *        and then flush the internal MP3 buffers, and thus may return a final few mp3 frames.
     *
     * @param finalBuffer pointer to encoded MP3 stream
     * @param bufferSize  number of valid octets in this stream
     *
     * @return CODE = number of bytes output to mp3buffer. Can be 0
     *
     * @note  mp3buffer should be at least 7200 bytes long to hold all possible emitted data.
     * @note  Will also write id3v1 tags (if any) into the bitstream
     */
    int MP3Converter::finalize(char* finalBuffer, int bufferSize){
      return lame_encode_flush(lame, reinterpret_cast<unsigned char*>(finalBuffer), bufferSize);
    }

    /**
     * @return Size (bytes) of mp3 data buffered, but not yet encoded.
     * @note   lame_encode_flush() will return more bytes than this because,
     *         it will encode the reamining buffered PCM samples before flushing the mp3 buffers.
     */
    int MP3Converter::getRemainingBytes(){
      return lame_get_size_mp3buffer(lame);
    }
}