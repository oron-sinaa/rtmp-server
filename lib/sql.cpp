/// Database provide functionality to update bandwidth consumption with respect
/// to protocol and streamname.
///                              DUMMY-SCHEMA
///  _________________________________________________________________________
/// |   StreamName  ||    HLS          ||    WebSocket   ||     DateTime      |
/// |-------------------------------------------------------------------------|
/// |               |                  |                 |                    |
/// |               |                  |                 |                    |
/// |               |                  |                 |                    |
/// |_________________________________________________________________________|
///
///////////////////////////////////////////////////////////////////////////////

#include "sql.h"

namespace Database{
  std::mutex mtx;
  std::string query;
  int req;

  static int callback(void *data, int argc, char **argv, char **azColName){return 0;}

  bool streamExists(sqlite3* &db, const std::string &streamname){
    if (db == nullptr){
      FAIL_MSG("SQL error, database connection does not exist");
      return false;
    }
    char *errMsg = nullptr;
    const std::string sql = "SELECT COUNT(*) FROM trafficConsumption WHERE streamname = '" + streamname + "';";
    int count = 0, rc;
    {
      std::lock_guard<std::mutex> lock(Database::mtx);
      rc = sqlite3_exec(db, sql.c_str(), [](void *data, int argc, char **argv, char **azColName) -> int {
        if (argc == 1){*static_cast<int*>(data) = std::stoi(argv[0]);}
        return false;
      }, &count, &errMsg);
    }
    if (rc != SQLITE_OK){
      FAIL_MSG("SQL error, unable to get streamInfo, error - %s", errMsg);
      sqlite3_free(errMsg);
      return false;
    }
    return count > 0;
  }

  void injectInDB2(sqlite3* &db, const std::string& feedName, const std::string& userId, const std::string& date,
                    double hls, double ndvr, double ws){
    if (db == nullptr){
      FAIL_MSG("SQL error, database connection does not exist");
      return;
    }
    const std::string query = "INSERT INTO trafficConsumption (feed_name, user_id, date, hls, ws, ndvr) VALUES (?, ?, ?, ?, ?, ?) ON CONFLICT(feed_name, user_id, date) DO UPDATE SET hls = ?, ws = ?, ndvr = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc;
    {
      std::lock_guard<std::mutex> lock(Database::mtx);
      rc = sqlite3_prepare_v2(db, query.c_str(), query.length(), &stmt, nullptr);
    }
    if (rc != SQLITE_OK){
        FAIL_MSG("SQL error, unable to prepare statement, %s", sqlite3_errmsg(db));
        return;
    }

    sqlite3_bind_text(stmt, 1, feedName.c_str(), feedName.length(), SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, userId.c_str(), userId.length(), SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, date.c_str(), date.length(), SQLITE_STATIC);
    sqlite3_bind_double(stmt, 4, hls);
    sqlite3_bind_double(stmt, 5, ws);
    sqlite3_bind_double(stmt, 6, ndvr);
    sqlite3_bind_double(stmt, 7, hls);
    sqlite3_bind_double(stmt, 8, ws);
    sqlite3_bind_double(stmt, 9, ndvr);

    {
      std::lock_guard<std::mutex> lock(Database::mtx);
      rc = sqlite3_step(stmt);
    }
    if (rc != SQLITE_DONE){FAIL_MSG("SQL error, unable to insert/update data, %s", sqlite3_errmsg(db));}
    if ((rc = sqlite3_finalize(stmt)) != SQLITE_OK){WARN_MSG("SQLite error, cannot finalize prepared statement");}
  }

  void createTable(sqlite3* &db){
    if (db == nullptr){
      FAIL_MSG("SQL error, database connection does not exist");
      return;
    }
    char *errMsg = nullptr;
    const std::string query = "CREATE TABLE IF NOT EXISTS trafficConsumption("
                              "streamname TEXT PRIMARY KEY,"
                              "traffic TEXT,"
                              "dateTime TEXT);";

    {
      std::lock_guard<std::mutex> lock(Database::mtx);
      req = sqlite3_exec(db, query.c_str(), callback, nullptr, &errMsg);
    }
    if (req != SQLITE_OK){
      FAIL_MSG("SQL error, unable to create table, %s", errMsg);
      sqlite3_free(errMsg);
    }
  }

  void createTable2(sqlite3* &db){
    if (db == nullptr){
      FAIL_MSG("SQL error, database connection does not exist");
      return;
    }
    char *errMsg = nullptr;
    const std::string query = "CREATE TABLE IF NOT EXISTS trafficConsumption("
                              "id INTEGER PRIMARY KEY,"
                              "feed_name TEXT NOT NULL,"
                              "user_id TEXT NOT NULL,"
                              "date DATE NOT NULL,"
                              "hls REAL,"
                              "ndvr REAL,"
                              "ws REAL,"
                              "UNIQUE(feed_name, user_id, date) ON CONFLICT REPLACE);";

    {
      std::lock_guard<std::mutex> lock(Database::mtx);
      req = sqlite3_exec(db, query.c_str(), callback, nullptr, &errMsg);
    }
    if (req != SQLITE_OK){
      FAIL_MSG("SQL error, unable to create table, %s", errMsg);
      sqlite3_free(errMsg);
    }
  }

  std::map<std::string, std::string> getColumn(sqlite3* &db, const std::string &streamname){
    if (db == nullptr){
      FAIL_MSG("SQL error, database connection does not exist");
      std::map<std::string, std::string> temp;
      return temp;
    }
    char *errMsg = nullptr;
    // Using map for storing unique users
    std::map<std::string, std::string> columnData;
    query = "SELECT * FROM trafficConsumption WHERE streamname = '" + streamname + "';";
    int rc;
    {
      std::lock_guard<std::mutex> lock(Database::mtx);
      rc = sqlite3_exec(db, query.c_str(), [](void *data, int argc, char **argv, char **azColName) -> int {
        // @ask ??? Why auto ?
        std::map<std::string, std::string>* columnDataPtr = static_cast<std::map<std::string, std::string>*>(data);
        if (argc == 3){
          for (int i = 0; i < argc; ++i){(*columnDataPtr)[azColName[i]] = argv[i] ? argv[i] : "NULL";}
        }
        return 0;
      }, &columnData, &errMsg);
    }
    if (rc != SQLITE_OK){
      sqlite3_free(errMsg);
    }
    return columnData;
  }

  JSON::Value getColumn2(sqlite3* &db, const std::string &feedName){
    if (db == nullptr){
      FAIL_MSG("SQL error, database connection does not exist");
      JSON::Value temp;
      return temp;
    }
    JSON::Value resultData;
    const std::string query = "SELECT feed_name, user_id, date, hls, ws, ndvr FROM trafficConsumption WHERE feed_name = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc;
    {
      std::lock_guard<std::mutex> lock(Database::mtx);
      rc = sqlite3_prepare_v2(db, query.c_str(), query.length(), &stmt, nullptr);
    }
    if (rc != SQLITE_OK){
      FAIL_MSG("SQL error, unable to prepare statement, %s", sqlite3_errmsg(db));
      return resultData;
    }

    sqlite3_bind_text(stmt, 1, feedName.c_str(), feedName.length(), SQLITE_STATIC);
    std::lock_guard<std::mutex> lock(Database::mtx);
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW){
      Database::TrafficStats row;
      row.feedName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
      row.userId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
      row.date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
      row.hls = sqlite3_column_double(stmt, 3);
      row.ws = sqlite3_column_double(stmt, 4);
      row.ndvr = sqlite3_column_double(stmt, 5);
      JSON::Value data;
      data["hls"] = (row.hls);
      data["ws"] = (row.ws);
      data["ndvr"] = (row.ndvr);
      resultData[row.date][row.userId] = data;
    }
    return resultData;
  }

  void injectInDB(sqlite3* &db, const std::string &streamname, const std::string &trafficData){
    if (db == nullptr){
      WARN_MSG("SQL error, database connection does not exist");
      return;
    }
    char *errMsg = nullptr;
    std::string currentTime = Util::getDateString(Util::epoch());
    if (Database::streamExists(db, streamname)){
      query = "INSERT OR REPLACE INTO trafficConsumption(streamname, traffic, dateTime) "
              "VALUES('" + streamname + "', '" + trafficData + "', '" + currentTime + "');";
    }else{
      query = "INSERT OR REPLACE INTO trafficConsumption(streamname, traffic, dateTime) "
              "VALUES('" + streamname + "', '" + trafficData + "', '" + currentTime + "');";
    }

    {
      std::lock_guard<std::mutex> lock(Database::mtx);
      req = sqlite3_exec(db, query.c_str(), callback, nullptr, &errMsg);
    }
    if (req != SQLITE_OK){
      FAIL_MSG("SQL error, Unable to push in DB, error - %s", errMsg);
      sqlite3_free(errMsg);
    }
  }

  bool loadDatabase(sqlite3* &db){
    int req = sqlite3_open(DEFAULT_DATABASE_PATH.c_str(), &db);
    if (req != SQLITE_OK){
      ERROR_MSG("Unable to open database: %s", sqlite3_errmsg(db));
      sqlite3_close(db); // Ensure the database connection is properly closed
      return false;
    }else{
      INFO_MSG("Database loaded successfully");
      return true;
    }
  }

  std::set<std::string> getUsersFromDB(sqlite3* &db){
    if (db == nullptr){
      FAIL_MSG("SQL error, database connection does not exist");
      std::set<std::string> temp;
      return temp;
    }
    const std::string query = "SELECT feed_name, user_id FROM trafficConsumption";
    sqlite3_stmt* stmt = nullptr;
    std::set<std::string> users;
    int rc;
    {
      std::lock_guard<std::mutex> lock(Database::mtx);
      rc = sqlite3_prepare_v2(db, query.c_str(), query.length(), &stmt, nullptr);
      if (rc != SQLITE_OK){
        FAIL_MSG("SQL error, unable to prepare statement, %s", sqlite3_errmsg(db));
        return users;
      }
      while ((rc = sqlite3_step(stmt)) == SQLITE_ROW){
        std::string userId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        users.insert(userId);
      }
      if (rc != SQLITE_DONE && rc != SQLITE_OK){
        FAIL_MSG("SQL error, unable to fetch data, %s", sqlite3_errmsg(db));
      }
    }
    if ((rc = sqlite3_finalize(stmt)) != SQLITE_OK){
      FAIL_MSG("SQL error, unable to finalize prepared statement");
    }
    return users;
  }

  std::map<std::string, std::vector<std::string>> getDatabaseDump(sqlite3* &db, const std::string &feedName){
    if (db == nullptr){
      FAIL_MSG("SQL error, database connection does not exist");
      std::map<std::string, std::vector<std::string>> temp;
      return temp;
    }
    std::map<std::string, std::vector<std::string>> result;
    const std::string date = Util::getDateOnlyString();
    const std::string query = "SELECT feed_name, user_id, date, hls, ws, ndvr FROM trafficConsumption WHERE feed_name = ? AND date = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc;
    {
      std::lock_guard<std::mutex> lock(Database::mtx);
      rc = sqlite3_prepare_v2(db, query.c_str(), query.length(), &stmt, nullptr);
      if (rc != SQLITE_OK){
        FAIL_MSG("SQL error, unable to prepare statement, %s", sqlite3_errmsg(db));
        return result;
      }
      sqlite3_bind_text(stmt, 1, feedName.c_str(), feedName.length(), SQLITE_STATIC);
      sqlite3_bind_text(stmt, 2, date.c_str(), date.length(), SQLITE_STATIC);
      while ((rc = sqlite3_step(stmt)) == SQLITE_ROW){
        std::vector<std::string> userData;
        Database::TrafficStats row;
        row.feedName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        row.userId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        row.date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        row.hls = sqlite3_column_double(stmt, 3);
        row.ws = sqlite3_column_double(stmt, 4);
        row.ndvr = sqlite3_column_double(stmt, 5);
        userData.insert(userData.end(), {std::to_string(row.hls), std::to_string(row.ws), std::to_string(row.ndvr)});
        result[row.userId] = userData;
      }
      if (rc != SQLITE_DONE && rc != SQLITE_OK){
        FAIL_MSG("SQL error, unable to fetch data, %s", sqlite3_errmsg(db));
      }
    }
    if ((rc = sqlite3_finalize(stmt)) != SQLITE_OK){
      FAIL_MSG("SQL error, unable to finalize prepared statement");
    }
    return result;
  }

  int closeDatabase(sqlite3* &db){
    if (db == nullptr){
      FAIL_MSG("SQL error, database connection does not exist");
      return 0;
    }
    int ret;
    std::lock_guard<std::mutex> lock(Database::mtx);
    if ((ret = sqlite3_close(db)) != SQLITE_BUSY){
      INFO_MSG("Database closes successfully");
      return 0;
    }else if (ret == SQLITE_BUSY){
      FAIL_MSG("SQL error, pending unfinalized prepared statements, Database connection still open");
      return 1;
    }
  }

  void insertIfNotExists(sqlite3* &db, const std::string &streamname){
    if (db == nullptr){
      WARN_MSG("SQL error, database connection does not exist");
      return;
    }
    if(!streamExists(db, streamname)){
      nlohmann::json traffic;
      nlohmann::json data;
      data["ws"] = 0.0;
      data["hls"] = 0.0;
      data["ndvr"] = 0.0;
      traffic["staqu"] = data;
      traffic["client"] = data;
      std::string trafficData = traffic.dump();
      injectInDB(db, streamname, trafficData);
    }
  }

  bool runQuery(sqlite3* &db, const std::string &query){
    if (db == nullptr){
      WARN_MSG("SQL error, database connection does not exist");
      return false;
    }
    char *errMsg = nullptr;
    {
      std::lock_guard<std::mutex> lock(Database::mtx);
      req = sqlite3_exec(db, query.c_str(), callback, nullptr, &errMsg);
    }
    if (req != SQLITE_OK){
      FAIL_MSG("SQL error, Unable to push in DB, error - %s", errMsg);
      sqlite3_free(errMsg);
      return false;
    }
    return true;
  }
}// namespace Database