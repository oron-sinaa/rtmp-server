#ifndef SQL_H
#define SQL_H
/// Data base handeler class to handle transaction

#include "defines.h"
#include "timing.h"
#include <sqlite3.h>
#include <mutex>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <utility>
#include <queue>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <string.h>
#include <ctime>
#include <fstream>
#include <iostream>
#include <stdio.h>
#include <mutex>
#include <map>
#include <mist/json.h>
#include <mist/nlohmann.h>

#ifndef DEFAULT_DATABASE_PATH
#define DEFAULT_DATABASE_PATH \
  (getenv("DATABASE_DIR") ? std::string(getenv("DATABASE_DIR"))+"/vision.db" : std::string("/etc/rtmpserver/vision.db"))
#endif


namespace Database {
  extern std::mutex mtx;
  extern std::string query;
  extern int req;

  struct TrafficStats {
    std::string feedName;
    std::string userId;
    std::string date;
    double hls;
    double ws;
    double ndvr;
  };


  /// @brief Callback function for SQLite3_exec function.
  /// This function is used as a callback for the SQLite3_exec function.
  /// It is called for each row of the result set returned by the SQL query.
  ///
  /// @param data A pointer to user data passed to the SQLite3_exec function.
  /// @param argc The number of columns in the result set.
  /// @param argv An array of strings containing the column values for the current row.
  /// @param azColName An array of strings containing the column names.
  /// @return Always returns 0 {default}
  static int callback(void *data, int argc, char **argv, char **azColName);

  /// @brief Check if a stream exists in the database.
  /// This function checks if a stream with the given name exists in the database.
  /// @param db A pointer to the SQLite database, @param streamname The name of the stream to check.
  /// @return True if the stream exists, false otherwise.
  bool streamExists (sqlite3* &db, const std::string &streamname);

  /// @brief Create the trafficConsumption table in the database.
  /// This function creates the trafficConsumption table in the database if it does not already exist.
  /// \b createTable is @deprecated
  void createTable (sqlite3* &db);
  void createTable2 (sqlite3* &db);

  /// @brief Open the database {traffic consumption}.
  bool loadDatabase (sqlite3* &db);

  /// @brief Close database {traffic consumption}, close the connection.
  int closeDatabase (sqlite3* &db);

  std::map<std::string, std::string> getColumn (sqlite3* &db, const std::string &streamname);
  void injectInDB (sqlite3* &db, const std::string &streamname, const std::string &trafficData);
  void insertIfNotExists (sqlite3* &db, const std::string &streamname);

  void injectInDB2 (sqlite3* &db, const std::string& feedName, const std::string& userId, const std::string& date,
                        double hls, double ndvr, double ws);

  std::map<std::string, std::vector<std::string>> getDatabaseDump (sqlite3* &db, const std::string &streamName);

  std::set<std::string> getUsersFromDB (sqlite3* &db);
  JSON::Value getColumn2 (sqlite3* &db, const std::string &feedName);
  bool runQuery (sqlite3* &db, const std::string &query);

}// namespace Database

#endif // SQL_H
