/// \file url.h
/// Holds all headers for the HTTP::URL class.

#pragma once
#include <stdlib.h>
#include <string>
#include <inttypes.h>

/// Holds all HTTP processing related code.
namespace HTTP{

  /// URL parsing class. Parses full URL into its subcomponents
  class URL{
  public:
    URL(const std::string &url = "");
    uint16_t getPort() const;
    uint16_t getDefaultPort() const;
    std::string getExt() const;
    std::string getUrl() const;
    std::string getRawUrl() const;
    std::string getFilePath() const;
    std::string getBareUrl() const;
    std::string getProxyUrl() const;
    bool isLocalPath() const;
    std::string host;     ///< Hostname or IP address of URL
    std::string protocol; ///< Protocol of URL
    std::string port;     ///< Port of URL
    std::string path; ///< Path after the first slash (not inclusive) but before any question mark
    std::string args; ///< Everything after the question mark in the path, if it was present
    std::string frag; ///< Everything after the # in the path, if it was present
    std::string user; ///< Username, if it was present
    std::string pass; ///< Password, if it was present
    URL link(const std::string &l) const;
    bool IPv6Addr;
  };

}// namespace HTTP
