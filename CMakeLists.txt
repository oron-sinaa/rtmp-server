########################################
# Basic Setup                          #
########################################

# Minimum CMake version and project definition
cmake_minimum_required(VERSION 3.10)
project(MistServer)

# Set C++17 as the standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Installation directory
set(CMAKE_INSTALL_PREFIX "/usr/local" CACHE PATH "Installation prefix")

# Prioritize static libraries
set(CMAKE_FIND_LIBRARY_SUFFIXES .a .lib)

# Modern CMake policy settings
if(COMMAND cmake_policy)
  cmake_policy(SET CMP0003 NEW)  # Target properties are preferred over global ones
  cmake_policy(SET CMP0021 NEW)  # Prefer target-based commands over global ones
endif()

# Disable warnings during build
add_compile_options(-w)

# Stop compilation on the first error
add_compile_options(-Wfatal-errors)

# Include directories
include_directories(/usr/local/include)
include_directories(${PROJECT_SOURCE_DIR})
include_directories(${PROJECT_BINARY_DIR} ${PROJECT_BINARY_DIR}/generated)

# Do not build shared libraries (build static)
set(BUILD_SHARED_LIBS OFF)


########################################
# Build Variables - Release            #
########################################

# Determine the release identifier
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  if(DEFINED RELEASE)
    set(RELEASE_RAW "${RELEASE}")
  else()
    # Fallback to a generic identifier based on system bitness
    execute_process(
      COMMAND getconf LONG_BIT
      OUTPUT_VARIABLE RELEASE_RAW
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(RELEASE_RAW "Generic_${RELEASE_RAW}")
  endif()
else()
  # Default RELEASE value for non-Release builds
  set(RELEASE_RAW "dev")
endif()

# Sanitize and finalize the RELEASE variable
string(STRIP "${RELEASE_RAW}" RELEASE)
set(RELEASE "\"${RELEASE}\"" CACHE STRING "Release identifier for the build")

# Add compiler flags for release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_compile_options(
    -O2                       # Balanced optimization for speed and stability (safe for multithreaded applications)
    -flto                      # Link-Time Optimization for better inlining and dead code elimination
    -fno-plt                  # Avoid Procedure Linkage Table for faster function calls
    -fomit-frame-pointer      # Omit frame pointers to reduce stack usage (use with caution for debugging)
    -fno-strict-aliasing      # Disable strict aliasing optimizations to avoid undefined behavior in low-level code
    -fPIC                     # Position Independent Code for shared libraries (important for multiprocessing)
    -fexceptions              # Enable exception handling (important for robust error recovery)
    -fstack-protector-strong  # Add stack protection to prevent buffer overflows
    -funsigned-char           # Treat chars as unsigned by default for consistent behavior
    -pthread                  # Ensure proper threading support using POSIX Threads library
    -Wl,-z,relro              # Enable Relocation Read-Only (RELRO) for improved security
    -Wl,-z,now                # Enable immediate binding for improved security
    -pipe                     # Use pipes instead of temp files during compilation (faster build)
    # -ftree-vectorize          # Enables vectorization of loops using SIMD
  )
endif()


########################################
# Build Variables - Package Version    #
########################################

# Determine the package version from the BUILD_VERSION environment variable or fallback to "Unknown"
if(DEFINED ENV{BUILD_VERSION})
  # Use the BUILD_VERSION environment variable
  set(PACKAGE_VERSION_RAW "$ENV{BUILD_VERSION}")
else()
  # Fallback to "Unknown" if BUILD_VERSION is unavailable
  set(PACKAGE_VERSION_RAW "Unknown")
endif()

# Strip any trailing whitespace (optional, but good practice)
string(STRIP "${PACKAGE_VERSION_RAW}" PACKAGE_VERSION_RAW)

# Set the final package version
set(PACKAGE_VERSION "\"${PACKAGE_VERSION_RAW}\"")
message(STATUS "Package version: ${PACKAGE_VERSION}")

# Sanitize and finalize the PACKAGE_VERSION variable
string(STRIP "${PACKAGE_VERSION_RAW}" PACKAGE_VERSION)
set(PACKAGE_VERSION "\"${PACKAGE_VERSION}\"" CACHE STRING "Package version for the build")


########################################
# Build Variables - Miscellaneous      #
########################################

# Default Debug Level
if(NOT DEBUG)
  set(DEBUG 2)  # Default debug level if not explicitly defined
endif()

# Shared Memory Support
option(NOSHM "Disable shared memory (falling back to shared temporary files)")
if(APPLE)
  set(NOSHM 1)  # Disable shared memory on Apple platforms by default
endif()

if(NOT NOSHM)
  add_definitions(-DSHM_ENABLED=1)  # Enable shared memory support
else()
  message("Shared memory use is turned OFF")  # Notify if shared memory is disabled
endif()

# Secrets and Filler Data
if(FILLER_DATA AND SHARED_SECRET AND SUPER_SECRET)
  add_definitions(
    -DFILLER_DATA="${FILLER_DATA}"
    -DSHARED_SECRET="${SHARED_SECRET}"
    -DSUPER_SECRET="${SUPER_SECRET}"
  )  # Define secrets and filler data if all are provided
endif()

# SSL/TLS Support
option(NOSSL "Disable SSL/TLS support")
if(NOT NOSSL)
  add_definitions(-DSSL=1)  # Enable SSL/TLS support
else()
  message("SSL/TLS support is turned OFF")  # Notify if SSL/TLS is disabled
endif()

# Shared Memory Data Size
if(DEFINED DATASIZE)
  add_definitions(-DSHM_DATASIZE=${DATASIZE})  # Define shared memory data size if provided
endif()

# Statistics Cutoff
if(DEFINED STAT_CUTOFF)
  add_definitions(-DSTAT_CUTOFF=${STAT_CUTOFF})  # Define statistics cutoff if provided
endif()

# API Authentication
option(NOAUTH "Disable API authentication entirely (insecure!)")
if(NOAUTH)
  add_definitions(-DNOAUTH=1)  # Disable API authentication if requested
endif()

# UDP API Configuration
if(DEFINED UDP_API_HOST)
  add_definitions(-DUDP_API_HOST=${UDP_API_HOST})  # Define UDP API host if provided
endif()

if(DEFINED UDP_API_PORT)
  add_definitions(-DUDP_API_PORT=${UDP_API_PORT})  # Define UDP API port if provided
endif()

# Application Name
set(APPNAME "MistServer" CACHE STRING "Name of the application, as used in user agent strings and the like")
add_definitions(-DAPPNAME="${APPNAME}")  # Define the application name

# Disk Serial Locking
set(DISKSERIAL "" CACHE STRING "Lock application to a specific disk serial number")
if(DISKSERIAL)
  add_definitions(-DDISKSERIAL="${DISKSERIAL}")  # Define disk serial number if provided
endif()

# Thread Naming
option(WITH_THREADNAMES "Enable fancy names for threads (not supported on all platforms)")
if(WITH_THREADNAMES)
  add_definitions(-DWITH_THREADNAMES=1)  # Enable thread naming if requested
endif()

# Stats Delay
if(DEFINED STATS_DELAY)
  add_definitions(-DSTATS_DELAY=${STATS_DELAY})  # Define stats delay if provided
endif()

# LL-HLS Support
option(NOLLHLS "Disable LL-HLS support")
if(NOLLHLS)
  add_definitions(-DNOLLHLS=1)  # Disable LL-HLS support
  message("Building WITHOUT LL-HLS support")  # Notify if LL-HLS is disabled
else()
  message("Building WITH LL-HLS support")  # Notify if LL-HLS is enabled
endif()


########################################
# Build Variables - Prepare for Build  #
########################################

# Print build configuration details
message("Building release ${RELEASE} for version ${PACKAGE_VERSION} @ debug level ${DEBUG}")

# Add common compiler definitions
add_compile_definitions(
  _LARGEFILE_SOURCE
  _FILE_OFFSET_BITS=64
  DEBUG=${DEBUG}
  PACKAGE_VERSION=${PACKAGE_VERSION}
  RELEASE=${RELEASE}
)

# Add compiler warning flags
add_compile_options(-Wall -Wno-sign-compare -Wparentheses)

# Option to disable native SRT support
option(NOSRT "Disable building native SRT support, regardless of library being present (default: enabled if libraries are installed)")
if(NOT NOSRT)
  find_library(SRT_LIB srt)
  if(SRT_LIB)
    add_compile_definitions(WITH_SRT=1)
    message("Building with SRT support")
  else()
    message("Building without native SRT support (library not found)")
  endif()
else()
  message("Building without native SRT support (disabled by user)")
endif()

# Option to disable RIST support
option(NORIST "Disable building RIST support, regardless of library being present (default: enabled if libraries are installed)")
if(NOT NORIST)
  find_library(RIST_LIB rist)
  if(RIST_LIB)
    add_compile_definitions(WITH_RIST=1)
    message("Building with RIST support")
  else()
    message("Building without RIST support (library not found)")
  endif()
else()
  message("Building without RIST support (disabled by user)")
endif()

# Check for SCTP library (Data Channels support)
find_library(SCTP_LIB usrsctp)
if(SCTP_LIB)
  add_compile_definitions(WITH_DATACHANNELS=1)
  message("Building with SCTP support (Data Channels enabled)")
else()
  message("Building without SCTP support (Data Channels disabled)")
endif()

# Check for SCTP library (Data Channels support)
find_library(LIBCRYPTO cryptopp)
if(NOT LIBCRYPTO)
  message(FATAL_ERROR "Library cryptopp not found")
endif()

find_package(SQLite3)
if(NOT SQLite3_FOUND)
  message(FATAL_ERROR "Package SQLite3 not found")
endif()


########################################
# MistLib - Header Files               #
########################################

message(STATUS "Collecting core library headers...")
set(libHeaders
  lib/adts.h
  lib/amf.h
  lib/auth.h
  lib/encode.h
  lib/bitfields.h
  lib/bitstream.h
  lib/certificate.h
  lib/checksum.h
  lib/cmaf.h
  lib/comms.h
  lib/config.h
  lib/defines.h
  lib/dtls_srtp_handshake.h
  lib/dtsc.h
  lib/encryption.h
  lib/flv_tag.h
  lib/h264.h
  lib/h265.h
  lib/hls_support.h
  lib/http_parser.h
  lib/downloader.h
  lib/id3.h
  lib/json.h
  lib/langcodes.h
  lib/mp4_adobe.h
  lib/mp4_dash.h
  lib/mp4_encryption.h
  lib/mp4_generic.h
  lib/mp4.h
  lib/mp4_ms.h
  lib/mpeg.h
  lib/nal.h
  lib/nlohmann.h
  lib/ogg.h
  lib/procs.h
  lib/rtmpchunks.h
  lib/rtp_fec.h
  lib/rtp.h
  lib/sdp.h
  lib/sdp_media.h
  lib/shared_memory.h
  lib/socket.h
  lib/sql.h
  lib/stream.h
  lib/stun.h
  lib/theora.h
  lib/timing.h
  lib/tinythread.h
  lib/ts_packet.h
  lib/ts_stream.h
  lib/util.h
  lib/vorbis.h
  lib/triggers.h
  lib/opus.h
  lib/riff.h
  lib/ebml.h
  lib/ebml_socketglue.h
  lib/websocket.h
  lib/url.h
  lib/urireader.h
  lib/ptvtmp.h
  lib/g711_to_aac.h
)

# Conditionally add SRT header if SRT library is available
if(SRT_LIB)
  message(STATUS "SRT library detected - adding SRT-specific header: lib/socket_srt.h")
  list(APPEND libHeaders lib/socket_srt.h)
endif()


########################################
# MistLib - Build                      #
########################################

message(STATUS "Building the core MistServer library 'mist' with all required source files...")
add_library(mist
  ${libHeaders}
  lib/adts.cpp
  lib/amf.cpp
  lib/auth.cpp
  lib/encode.cpp
  lib/bitfields.cpp
  lib/bitstream.cpp
  lib/cmaf.cpp
  lib/comms.cpp
  lib/certificate.cpp
  lib/config.cpp
  lib/dtls_srtp_handshake.cpp
  lib/dtsc.cpp
  lib/encryption.cpp
  lib/flv_tag.cpp
  lib/h264.cpp
  lib/h265.cpp
  lib/hls_support.cpp
  lib/http_parser.cpp
  lib/downloader.cpp
  lib/id3.cpp
  lib/json.cpp
  lib/langcodes.cpp
  lib/mp4_adobe.cpp
  lib/mp4.cpp
  lib/mp4_dash.cpp
  lib/mp4_encryption.cpp
  lib/mp4_generic.cpp
  lib/mp4_ms.cpp
  lib/mpeg.cpp
  lib/nal.cpp
  lib/ogg.cpp
  lib/procs.cpp
  lib/rtmpchunks.cpp
  lib/rtp_fec.cpp
  lib/rtp.cpp
  lib/sdp.cpp
  lib/sdp_media.cpp
  lib/shared_memory.cpp
  lib/socket.cpp
  lib/sql.cpp
  lib/stream.cpp
  lib/stun.cpp
  lib/theora.cpp
  lib/timing.cpp
  lib/tinythread.cpp
  lib/ts_packet.cpp
  lib/ts_stream.cpp
  lib/util.cpp
  lib/vorbis.cpp
  lib/triggers.cpp
  lib/opus.cpp
  lib/riff.cpp
  lib/ebml.cpp
  lib/ebml_socketglue.cpp
  lib/websocket.cpp
  lib/url.cpp
  lib/urireader.cpp
  lib/ptvtmp.cpp
  lib/g711_to_aac.cpp
)

# Link FAAC library for RTP ALAW/ULAW to AAC audio transcoder
find_library(FAAC_LIB faac)
if(FAAC_LIB)
  message(STATUS "Found FAAC library: ${FAAC_LIB}")
  target_link_libraries(mist ${FAAC_LIB})
else()
  message(FATAL_ERROR "FAAC library not found - required for RTP ALAW/ULAW to AAC audio transcoding")
endif()

# TCMalloc support for optimized malloc() operations
find_library(TCMALLOC_LIB tcmalloc)
if(TCMALLOC_LIB)
  message(STATUS "Found TCMalloc library: ${TCMALLOC_LIB}")
  target_link_libraries(mist ${TCMALLOC_LIB})
else()
  message(WARNING "TCMalloc library not found - continuing without TCMalloc support")
endif()

# Link system libraries
if(NOT APPLE)
  set(LIBRT -lrt)
  message(STATUS "Adding LIBRT (-lrt) for non-Apple systems")
endif()
message(STATUS "Linking system libraries: pthread and LIBRT (${LIBRT})")
target_link_libraries(mist
  -lpthread
  ${LIBRT}
)

# SSL/TLS support
if(NOT NOSSL)
  message(STATUS "Configuring SSL/TLS support...")
  find_library(MBEDTLS_LIB mbedtls)
  if(MBEDTLS_LIB)
    message(STATUS "Found mbedtls library: ${MBEDTLS_LIB}")
  else()
    message(WARNING "mbedtls library not found - SSL/TLS support may be incomplete")
  endif()
  find_library(MBEDX509_LIB mbedx509)
  if(MBEDX509_LIB)
    message(STATUS "Found mbedx509 library: ${MBEDX509_LIB}")
  else()
    message(WARNING "mbedx509 library not found - SSL/TLS support may be incomplete")
  endif()
  find_library(MBEDCRYPTO_LIB mbedcrypto)
  if(MBEDCRYPTO_LIB)
    message(STATUS "Found mbedcrypto library: ${MBEDCRYPTO_LIB}")
  else()
    message(WARNING "mbedcrypto library not found - SSL/TLS support may be incomplete")
  endif()
  find_library(SRTP2_LIB srtp2)
  if(SRTP2_LIB)
    message(STATUS "Found srtp2 library: ${SRTP2_LIB}")
  else()
    message(WARNING "srtp2 library not found - SSL/TLS support may be incomplete")
  endif()
  target_link_libraries(mist
    ${MBEDTLS_LIB}
    ${MBEDX509_LIB}
    ${MBEDCRYPTO_LIB}
  )
  find_path(MBEDTLS_HEADER_DIR "mbedtls/net.h")
  if(MBEDTLS_HEADER_DIR)
    message(STATUS "Found mbedtls headers in: ${MBEDTLS_HEADER_DIR}")
  else()
    message(WARNING "mbedtls headers not found - SSL/TLS support may be incomplete")
  endif()
  find_path(SRTP2_HEADER_DIR "srtp2/srtp.h")
  if(SRTP2_HEADER_DIR)
    message(STATUS "Found srtp2 headers in: ${SRTP2_HEADER_DIR}")
  else()
    message(WARNING "srtp2 headers not found - SSL/TLS support may be incomplete")
  endif()
  include_directories(${MBEDTLS_HEADER_DIR} ${SRTP2_HEADER_DIR})
  message(STATUS "SSL/TLS support configured.")
else()
  message(STATUS "SSL/TLS support is disabled (NOSSL is set)")
endif()

# SRT library support
if(SRT_LIB)
  message(STATUS "Configuring SRT library support...")
  add_library(mist_srt
    lib/socket_srt.h
    lib/socket_srt.cpp
  )
  target_link_libraries(mist_srt
    mist
    ${SRT_LIB}
  )
  find_path(SRT_HEADER_DIR "srt/srt.h")
  if(SRT_HEADER_DIR)
    message(STATUS "Found SRT headers in: ${SRT_HEADER_DIR}")
  else()
    message(WARNING "SRT headers not found - SRT support may be incomplete")
  endif()
  include_directories(${SRT_HEADER_DIR})
  install(
    TARGETS mist_srt
    DESTINATION lib
  )
  message(STATUS "SRT library support configured.")
else()
  message(STATUS "SRT library support is disabled (SRT_LIB is not set)")
endif()

# Install headers and library
message(STATUS "Installing MistServer headers to include/mist")
install(
  FILES ${libHeaders}
  DESTINATION include/mist
)
message(STATUS "Installing MistServer library to lib")
install(
  TARGETS mist
  DESTINATION lib
)

file(COPY ${libHeaders} DESTINATION ${PROJECT_BINARY_DIR}/mist)

########################################
# MistServer - Analysers               #
########################################

macro(makeAnalyser analyserName format)
  message(STATUS "Making MistAnalyser${analyserName}")
  add_executable(MistAnalyser${analyserName}
    src/analysers/mist_analyse.cpp
    src/analysers/analyser.cpp
    src/analysers/analyser_${format}.cpp
  )
  set_target_properties(MistAnalyser${analyserName}
    PROPERTIES COMPILE_DEFINITIONS "ANALYSERHEADER=\"analyser_${format}.h\"; ANALYSERTYPE=Analyser${analyserName}"
  )
  target_link_libraries(MistAnalyser${analyserName}
    mist
  )
  install(
    TARGETS MistAnalyser${analyserName}
    DESTINATION bin
  )
endmacro()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  # makeAnalyser(RTMP rtmp)
  # makeAnalyser(FLV flv)
  # makeAnalyser(DTSC dtsc)
  # makeAnalyser(OGG ogg)
  # makeAnalyser(EBML ebml)
  # makeAnalyser(TS ts)
  # makeAnalyser(MP4 mp4)
  # makeAnalyser(H264 h264)
  # makeAnalyser(HLS hls)
  # makeAnalyser(RIFF riff)
  # makeAnalyser(RTSP rtsp)
  # makeAnalyser(RTSP rtsp) # broken.
else()
  message(STATUS "Skipping MistAnalyser creation because CMAKE_BUILD_TYPE is not 'Release'")
endif()


########################################
# MistServer - Utilities               #
########################################

macro(makeUtil utilName utilFile)
  message(STATUS "Making MistUtil${utilName}")
  add_executable(MistUtil${utilName}
    src/utils/util_${utilFile}.cpp
  )
  target_link_libraries(MistUtil${utilName}
    mist
  )
  install(
    TARGETS MistUtil${utilName}
    DESTINATION bin
  )
endmacro()

# makeUtil(META meta)
# makeUtil(RAX rax)
# makeUtil(AMF amf)
# makeUtil(Certbot certbot)
makeUtil(Nuke nuke)
# makeUtil(Stats stats)
option(LOAD_BALANCE "Build the load balancer")
if (LOAD_BALANCE)
  # makeUtil(Load load)
endif()

# add_executable(MistTranslateH264
#   src/analysers/h264_translate.cpp
# )
# target_link_libraries(MistTranslateH264
#   mist
# )


########################################
# MistServer - Inputs                  #
########################################

macro(makeInput inputName format)
  message(STATUS "Making MistIn${inputName}")
  add_executable(MistIn${inputName}
    src/input/mist_in.cpp
    src/input/input.cpp
    src/input/input_${format}.cpp
    src/io.cpp
  )
  if (";${ARGN};" MATCHES ";with_srt;")
    target_link_libraries(MistIn${inputName} mist_srt)
  endif()
  if (";${ARGN};" MATCHES ";with_rist;")
    target_link_libraries(MistIn${inputName} rist cjson)
  endif()

  #Set compile definitions
  unset(my_definitions)
  list(APPEND my_definitions "INPUTTYPE=\"input_${format}.h\"")

  set_target_properties(MistIn${inputName}
    PROPERTIES COMPILE_DEFINITIONS "${my_definitions}"
  )

  target_link_libraries(MistIn${inputName} mist)
  install(
    TARGETS MistIn${inputName}
    DESTINATION bin
  )
endmacro()

# makeInput(RTSP rtsp)
makeInput(Buffer buffer)
makeInput(DTSC dtsc)
# makeInput(MP4 mp4)
# makeInput(AAC aac)
# makeInput(SDP sdp)
# makeInput(OGG ogg)
# makeInput(H264 h264)
# makeInput(EBML ebml)
# makeInput(ISMV ismv)
# makeInput(TS ts)
# makeInput(Folder folder)
# makeInput(SRT srt)
# makeInput(Playlist playlist)
# makeInput(Balancer balancer)
# makeInput(HLS hls)
# makeInput(MP3 mp3)
# makeInput(FLV flv)
option(WITH_AV "Build a generic libav-based input (not distributable!)")
if (WITH_AV)
  makeInput(AV av)
  target_link_libraries(MistInAV avformat avcodec avutil)
endif()

if(SRT_LIB)
  makeInput(TSSRT tssrt with_srt)
endif()
if(RIST_LIB)
  makeInput(TSRIST tsrist with_rist)
endif()


########################################
# MistServer - Outputs                 #
########################################

macro(makeOutput outputName format)
  message(STATUS "Making MistOut${outputName}")
  # Parse all extra arguments, for http and ts flags
  SET (tsBaseClass Output)
  SET (outBaseFile src/output/mist_out.cpp)
  if (";${ARGN};" MATCHES ";http;")
    SET(httpOutput src/output/output_http.cpp)
    if (";${ARGN};" MATCHES ";ts;")
      SET(tsBaseClass HTTPOutput)
    endif()
  endif()
  if (";${ARGN};" MATCHES ";debased;")
    SET(outBaseFile "")
  endif()
  if (";${ARGN};" MATCHES ";ts;")
    SET(tsOutput src/output/output_ts_base.cpp)
  endif()
  if (";${ARGN};" MATCHES ";jpg;")
    SET(tsOutput)
  endif()
  if (";${ARGN};" MATCHES ";srtp;")
    SET(tsOutput src/output/output_webrtc_srtp.h src/output/output_webrtc_srtp.cpp)
  endif()
  add_executable(MistOut${outputName}
    ${outBaseFile}
    src/output/output.cpp
    src/output/output_${format}.cpp
    src/io.cpp
    ${httpOutput}
    ${tsOutput}
    ${mp4Output}
  )
  set_target_properties(MistOut${outputName}
    PROPERTIES COMPILE_DEFINITIONS "OUTPUTTYPE=\"output_${format}.h\";TS_BASECLASS=${tsBaseClass}"
  )
  if (";${ARGN};" MATCHES ";with_srt;")
    target_link_libraries(MistOut${outputName} mist_srt)
  endif()
  if (";${ARGN};" MATCHES ";with_rist;")
    target_link_libraries(MistOut${outputName} rist cjson)
  endif()
  if (";${ARGN};" MATCHES ";srtp;")
    target_link_libraries(MistOut${outputName} ${SRTP2_LIB})
  endif()
  if (";${ARGN};" MATCHES ";usrsctp;")
    target_link_libraries(MistOut${outputName} ${SCTP_LIB})
  endif()
  target_link_libraries(MistOut${outputName} mist)
  install(
    TARGETS MistOut${outputName}
    DESTINATION bin
  )
endmacro()

# makeOutput(RTSP rtsp)
makeOutput(RTMP rtmp)
makeOutput(DTSC dtsc)
makeOutput(TS ts                    ts)
makeOutput(CMAF cmaf             http)
makeOutput(HLS hls               http ts)
makeOutput(JPG jpg               http jpg)
# makeOutput(HTTPTS httpts       http ts)
# makeOutput(H264 h264           http)
# makeOutput(MP4 mp4             http)
# makeOutput(FMP4 fmp4           http)
# makeOutput(AAC aac             http)
# makeOutput(SDP sdp             http)
# makeOutput(OGG ogg             http)
# makeOutput(FLV flv             http)
# makeOutput(MP3 mp3             http)
# makeOutput(HDS hds             http)
# makeOutput(SRT srt             http)
# makeOutput(JSON json           http)
# makeOutput(HTTPMinimalServer   http_minimalserver http)
# makeOutput(EBML ebml)
# makeOutput(WAV wav)


if(SRT_LIB)
  makeOutput(TSSRT tssrt              ts debased with_srt)
endif()

if(RIST_LIB)
  makeOutput(TSRIST tsrist              ts debased with_rist)
endif()

add_executable(MistSession
  src/session.cpp
)
install(
  TARGETS MistSession
  DESTINATION bin
)
target_link_libraries(MistSession mist ${SQLite3_LIBRARIES})

# add_executable(MistProcFFMPEG
#   src/process/process_ffmpeg.cpp
#   src/output/output_ebml.cpp
#   src/input/input_ebml.cpp
#   src/input/input.cpp
#   src/output/output_http.cpp
#   src/output/output.cpp
#   src/io.cpp
# )
# target_link_libraries(MistProcFFMPEG mist)
# install(
#   TARGETS MistProcFFMPEG
#   DESTINATION bin
# )

if (NOT NOSSL)
  makeOutput(HTTPS https)
  # makeOutput(WebRTC webrtc http srtp usrsctp)
endif()

option(WITH_SANITY "Enable MistOutSanityCheck output for testing purposes")
if (WITH_SANITY)
  makeOutput(SanityCheck sanitycheck)
endif()

add_executable(MistOutHTTP
  src/output/mist_out.cpp
  src/output/output.cpp
  src/output/output_http.cpp
  src/output/output_http_internal.cpp
  src/io.cpp
  generated/player.js.h
  generated/html5.js.h
  generated/flash_strobe.js.h
  generated/dashjs.js.h
  generated/videojs.js.h
  generated/webrtc.js.h
  generated/mews.js.h
  generated/flv.js.h
  generated/hlsjs.js.h
  generated/rawws.js.h
  generated/player_dash.js.h
  generated/player_dash_lic.js.h
  generated/player_video.js.h
  generated/player_webrtc.js.h
  generated/player_flv.js.h
  generated/player_hlsjs.js.h
  generated/player_libde265.js.h
  generated/skin_default.css.h
  generated/skin_dev.css.h
  generated/skin_videojs.css.h
)
set_target_properties(MistOutHTTP
  PROPERTIES COMPILE_DEFINITIONS "OUTPUTTYPE=\"output_http_internal.h\""
)
target_link_libraries(MistOutHTTP mist ${SQLite3_LIBRARIES})
install(
  TARGETS MistOutHTTP
  DESTINATION bin
)


########################################
# Documentation                        #
########################################
find_package(Doxygen)
if(DOXYGEN_FOUND)
  set(DOXY_LAYOUT "${PROJECT_SOURCE_DIR}/DoxygenLayout.xml")
  set(INPUT_DIRS "${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/lib")
  configure_file(${PROJECT_SOURCE_DIR}/Doxyfile.in ${PROJECT_BINARY_DIR}/Doxyfile @ONLY)
  add_custom_target(docs
    ${DOXYGEN_EXECUTABLE} ${PROJECT_BINARY_DIR}/Doxyfile
    COMMAND rm -rf ${PROJECT_BINARY_DIR}/docs
    COMMAND mv ${PROJECT_SOURCE_DIR}/docs ${PROJECT_BINARY_DIR}
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Generating API documentation with Doxygen"
    VERBATIM
  )
endif(DOXYGEN_FOUND)


########################################
# Sourcery                             #
########################################
# If cross compiling, assume failure.
if (CMAKE_CROSSCOMPILING)
  set(RUNA "1")
else()
  try_run(RUNA RUNB ${PROJECT_BINARY_DIR}/CMakeTmp ${PROJECT_SOURCE_DIR}/src/sourcery.cpp)
endif()

if("${RUNA}" EQUAL "42")
  message("Not cross compiling - building sourcery")
  add_executable(sourcery
    src/sourcery.cpp
  )
else()
  message("Detected cross compiling")
  if (EXISTS "${PROJECT_SOURCE_DIR}/sourcery")
    if (EXISTS "${PROJECT_BINARY_DIR}/sourcery")
      add_custom_target(sourcery)
    else()
      add_custom_target(sourcery
        COMMAND cp ${PROJECT_SOURCE_DIR}/sourcery ${PROJECT_BINARY_DIR}/sourcery
      )
    endif()
  else()
    message(FATAL_ERROR "Cross compiling and no usable sourcery binary found - please supply it")
  endif()
endif()


########################################
# Embed Code                           #
########################################

# main
add_custom_command(OUTPUT generated/player.js.h
  COMMAND ./sourcery ${PROJECT_SOURCE_DIR}/embed/min/player.js player_js generated/player.js.h
  DEPENDS sourcery ${PROJECT_SOURCE_DIR}/embed/min/player.js
)

# wrappers
add_custom_command(OUTPUT generated/html5.js.h
  COMMAND ./sourcery ${PROJECT_SOURCE_DIR}/embed/min/wrappers/html5.js html5_js generated/html5.js.h
  DEPENDS sourcery ${PROJECT_SOURCE_DIR}/embed/min/wrappers/html5.js
)
add_custom_command(OUTPUT generated/flash_strobe.js.h
  COMMAND ./sourcery ${PROJECT_SOURCE_DIR}/embed/min/wrappers/flash_strobe.js flash_strobe_js generated/flash_strobe.js.h
  DEPENDS sourcery ${PROJECT_SOURCE_DIR}/embed/min/wrappers/flash_strobe.js
)
add_custom_command(OUTPUT generated/dashjs.js.h
  COMMAND ./sourcery ${PROJECT_SOURCE_DIR}/embed/min/wrappers/dashjs.js dash_js generated/dashjs.js.h
  DEPENDS sourcery ${PROJECT_SOURCE_DIR}/embed/min/wrappers/dashjs.js
)
add_custom_command(OUTPUT generated/videojs.js.h
  COMMAND ./sourcery ${PROJECT_SOURCE_DIR}/embed/min/wrappers/videojs.js video_js generated/videojs.js.h
  DEPENDS sourcery ${PROJECT_SOURCE_DIR}/embed/min/wrappers/videojs.js
)
add_custom_command(OUTPUT generated/webrtc.js.h
  COMMAND ./sourcery ${PROJECT_SOURCE_DIR}/embed/min/wrappers/webrtc.js webrtc_js generated/webrtc.js.h
  DEPENDS sourcery ${PROJECT_SOURCE_DIR}/embed/min/wrappers/webrtc.js
)
add_custom_command(OUTPUT generated/mews.js.h
  COMMAND ./sourcery ${PROJECT_SOURCE_DIR}/embed/min/wrappers/mews.js mews_js generated/mews.js.h
  DEPENDS sourcery ${PROJECT_SOURCE_DIR}/embed/min/wrappers/mews.js
)
add_custom_command(OUTPUT generated/flv.js.h
  COMMAND ./sourcery ${PROJECT_SOURCE_DIR}/embed/min/wrappers/flv.js flv_js generated/flv.js.h
  DEPENDS sourcery ${PROJECT_SOURCE_DIR}/embed/min/wrappers/flv.js
)
add_custom_command(OUTPUT generated/hlsjs.js.h
  COMMAND ./sourcery ${PROJECT_SOURCE_DIR}/embed/min/wrappers/hlsjs.js hlsjs_js generated/hlsjs.js.h
  DEPENDS sourcery ${PROJECT_SOURCE_DIR}/embed/min/wrappers/hlsjs.js
)
add_custom_command(OUTPUT generated/rawws.js.h
  COMMAND ./sourcery ${PROJECT_SOURCE_DIR}/embed/min/wrappers/rawws.js rawws_js generated/rawws.js.h
  DEPENDS sourcery ${PROJECT_SOURCE_DIR}/embed/min/wrappers/rawws.js
)

# players
add_custom_command(OUTPUT generated/player_dash_lic.js.h
  COMMAND ./sourcery ${PROJECT_SOURCE_DIR}/embed/players/dash.js.license.js player_dash_lic_js generated/player_dash_lic.js.h
  DEPENDS sourcery ${PROJECT_SOURCE_DIR}/embed/players/dash.js.license.js
)
add_custom_command(OUTPUT generated/player_dash.js.h
  COMMAND ./sourcery ${PROJECT_SOURCE_DIR}/embed/players/dash.all.min.js player_dash_js generated/player_dash.js.h
  DEPENDS sourcery ${PROJECT_SOURCE_DIR}/embed/players/dash.all.min.js
)
add_custom_command(OUTPUT generated/player_video.js.h
  COMMAND ./sourcery ${PROJECT_SOURCE_DIR}/embed/players/video.min.js player_video_js generated/player_video.js.h
  DEPENDS sourcery ${PROJECT_SOURCE_DIR}/embed/players/video.min.js
)
add_custom_command(OUTPUT generated/player_webrtc.js.h
  COMMAND ./sourcery ${PROJECT_SOURCE_DIR}/embed/players/webrtc.js player_webrtc_js generated/player_webrtc.js.h
  DEPENDS sourcery ${PROJECT_SOURCE_DIR}/embed/players/webrtc.js
)
add_custom_command(OUTPUT generated/player_flv.js.h
  COMMAND ./sourcery ${PROJECT_SOURCE_DIR}/embed/players/flv.min.js player_flv_js generated/player_flv.js.h
  DEPENDS sourcery ${PROJECT_SOURCE_DIR}/embed/players/flv.min.js
)
add_custom_command(OUTPUT generated/player_hlsjs.js.h
  COMMAND ./sourcery ${PROJECT_SOURCE_DIR}/embed/players/hls.js player_hlsjs_js generated/player_hlsjs.js.h
  DEPENDS sourcery ${PROJECT_SOURCE_DIR}/embed/players/hls.js
)
add_custom_command(OUTPUT generated/player_libde265.js.h
  COMMAND ./sourcery ${PROJECT_SOURCE_DIR}/embed/players/libde265.min.js player_libde265_js generated/player_libde265.js.h
  DEPENDS sourcery ${PROJECT_SOURCE_DIR}/embed/players/libde265.min.js
)

# css
add_custom_command(OUTPUT generated/skin_default.css.h
  COMMAND ./sourcery ${PROJECT_SOURCE_DIR}/embed/min/skins/default.css skin_default_css generated/skin_default.css.h
  DEPENDS sourcery ${PROJECT_SOURCE_DIR}/embed/min/skins/default.css
)
add_custom_command(OUTPUT generated/skin_dev.css.h
  COMMAND ./sourcery ${PROJECT_SOURCE_DIR}/embed/min/skins/dev.css skin_dev_css generated/skin_dev.css.h
  DEPENDS sourcery ${PROJECT_SOURCE_DIR}/embed/min/skins/dev.css
)
add_custom_command(OUTPUT generated/skin_videojs.css.h
  COMMAND ./sourcery ${PROJECT_SOURCE_DIR}/embed/skins/video-js.css skin_videojs_css generated/skin_videojs.css.h
  DEPENDS sourcery ${PROJECT_SOURCE_DIR}/embed/skins/video-js.css
)


########################################
# Local Settings Page                  #
########################################

set(lspSOURCES
  ${PROJECT_SOURCE_DIR}/lsp/plugins/jquery.js
  ${PROJECT_SOURCE_DIR}/lsp/plugins/jquery.flot.min.js
  ${PROJECT_SOURCE_DIR}/lsp/plugins/jquery.flot.time.min.js
  ${PROJECT_SOURCE_DIR}/lsp/plugins/jquery.qrcode.min.js
  ${PROJECT_SOURCE_DIR}/lsp/minified.js
)

add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/generated/server.html
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR="${PROJECT_SOURCE_DIR}" -DlspSOURCES="${lspSOURCES}" -P ${PROJECT_SOURCE_DIR}/CMakeServHtml.txt
  DEPENDS ${lspSOURCES} ${PROJECT_SOURCE_DIR}/CMakeServHtml.txt ${PROJECT_SOURCE_DIR}/lsp/main.css ${PROJECT_SOURCE_DIR}/lsp/header.html ${PROJECT_SOURCE_DIR}/lsp/footer.html
)
add_custom_command(OUTPUT generated/server.html.h
  COMMAND ./sourcery generated/server.html server_html generated/server.html.h
  DEPENDS sourcery ${PROJECT_BINARY_DIR}/generated/server.html
)


########################################
# MistController - Build               #
########################################

add_executable(MistController
  src/controller/controller_api.h
  src/controller/controller_statistics.h
  src/controller/controller_connectors.h
  src/controller/controller_storage.h
  src/controller/controller_capabilities.h
  src/controller/controller_streams.h
  src/controller/controller_push.h
  src/controller/controller_license.h
  src/controller/controller.cpp
  src/controller/controller_streams.cpp
  src/controller/controller_storage.cpp
  src/controller/controller_connectors.cpp
  src/controller/controller_statistics.cpp
  src/controller/controller_capabilities.cpp
  src/controller/controller_uplink.cpp
  src/controller/controller_api.cpp
  src/controller/controller_push.cpp
  src/controller/controller_license.cpp
  generated/server.html.h
)
set_target_properties(MistController
  PROPERTIES COMPILE_DEFINITIONS RELEASE=${RELEASE}
)
target_link_libraries(MistController
  mist ${LIBCRYPTO} ${SQLite3_LIBRARIES}
)
install(
  TARGETS MistController
  DESTINATION bin
)
# Needed to make parallel builds work well
add_dependencies(MistOutHTTP MistController)


########################################
# Make Clean                           #
########################################

add_custom_target(clean-all
  COMMAND ${CMAKE_BUILD_TOOL} clean
  COMMAND rm -rf ${PROJECT_BINARY_DIR}/CMakeCache.txt
  COMMAND rm -rf ${PROJECT_BINARY_DIR}/cmake_install.cmake
  COMMAND rm -rf ${PROJECT_BINARY_DIR}/Makefile
  COMMAND rm -rf ${PROJECT_BINARY_DIR}/CMakeFiles
  COMMAND rm -rf ${PROJECT_BINARY_DIR}/Testing
  COMMAND rm -rf ${PROJECT_BINARY_DIR}/CTestTestfile.cmake
  COMMAND rm -rf ${PROJECT_BINARY_DIR}/DartConfiguration.tcl
  COMMAND rm -rf ${PROJECT_BINARY_DIR}/mist
)


########################################
# Tests                                #
########################################

if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
  add_executable(urltest test/url.cpp)
  target_link_libraries(urltest mist)
  add_test(URLTest COMMAND urltest)
  add_executable(logtest test/log.cpp)
  target_link_libraries(logtest mist)
  add_test(LOGTest COMMAND logtest)
  add_executable(logconvertertest test/converter.cpp)
  target_link_libraries(logconvertertest mist)
  add_test(LOGConverterTest COMMAND logconvertertest)
  add_executable(downloadertest test/downloader.cpp)
  target_link_libraries(downloadertest mist)
  add_test(DownloaderTest COMMAND downloadertest)
  add_executable(urireadertest test/urireader.cpp)
  target_link_libraries(urireadertest mist)
  add_test(URIReaderTest COMMAND urireadertest)
  add_executable(jsontest test/json.cpp)
  target_link_libraries(jsontest mist)
  add_test(JSONTest COMMAND jsontest)
  add_executable(resolvetest test/resolve.cpp)
  target_link_libraries(resolvetest mist)
  add_executable(bitwritertest test/bitwriter.cpp)
  target_link_libraries(bitwritertest mist)
  add_test(BitWriterTest COMMAND bitwritertest)
  add_executable(streamstatustest test/status.cpp)
  target_link_libraries(streamstatustest mist)
  add_executable(websockettest test/websocket.cpp)
  target_link_libraries(websockettest mist)
  add_executable(dtsc_sizing_test test/dtsc_sizing.cpp)
  target_link_libraries(dtsc_sizing_test mist)
else()
  message(STATUS "Skipping tests because CMAKE_BUILD_TYPE is 'Release'")
endif()
