/// \file controller.cpp
/// Contains all code for the controller executable.

#include "controller_api.h"
#include "controller_capabilities.h"
#include "controller_connectors.h"
#include "controller_push.h"
#include "controller_statistics.h"
#include "controller_storage.h"
#include "controller_streams.h"
#include "controller_license.h"
#include <ctime>
#include <fstream> //for ram space check
#include <iostream>
#include <mist/auth.h>
#include <mist/config.h>
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/procs.h>
#include <mist/shared_memory.h>
#include <mist/socket.h>
#include <mist/stream.h>
#include <mist/timing.h>
#include <mist/tinythread.h>
#include <mist/sql.h>
#include <pthread.h>
#include <mist/util.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/statvfs.h> //for shm space check
#include <sys/wait.h>
#include <vector>
#include <dirent.h>
#include <unistd.h>
/*LTS-START*/
#include <mist/triggers.h>
/*LTS-END*/

#ifndef COMPILED_USERNAME
#define COMPILED_USERNAME ""
#define COMPILED_PASSWORD ""
#endif

/// the following function is a simple check if the user wants to proceed to fix (y), ignore (n) or
/// abort on (a) a question
static inline char yna(std::string &user_input){
  switch (user_input[0]){
  case 'y':
  case 'Y': return 'y'; break;
  case 'n':
  case 'N': return 'n'; break;
  case 'a':
  case 'A': return 'a'; break;
  case 't':
  case 'T': return 't'; break;
  default: return 'x'; break;
  }
}

/// createAccount accepts a string in the form of username:account
/// and creates an account.
void createAccount(std::string account){
  if (account.size() > 0){
    size_t colon = account.find(':');
    if (colon != std::string::npos && colon != 0 && colon != account.size()){
      std::string uname = account.substr(0, colon);
      std::string pword = account.substr(colon + 1, std::string::npos);
      Controller::Log("CONF", "Created account " + uname + " through console interface");
      Controller::Storage["account"][uname]["password"] = Secure::md5(pword);
    }
  }
}

/// Status monitoring thread.
/// Will check outputs, inputs and converters after a delay of ten seconds
void statusMonitor(void *np){
#ifdef WITH_THREADNAMES
  pthread_setname_np(pthread_self(), "TrStatusMonitor");
#endif
  // Do not reload connectors on boot
  // Controller::loadActiveConnectors();
  // Rather shutdown connectors
  // (i.e. clear MstCnns shared page that could have been left dangling after a crash)
  // Wait for a few seconds for other threads to get launched
  Controller::prepareActiveConnectorsForShutdown();
  Controller::sleepInSteps(2);
  while (Controller::conf.is_active){
    // checks online protocols, reports changes to status
    if (Controller::CheckProtocols(Controller::Storage["config"]["protocols"], Controller::capabilities)){
      tthread::lock_guard<tthread::mutex> guard(Controller::configMutex);
      Controller::writeProtocols();
    }
    // checks stream statuses, reports changes to status
    Controller::CheckAllStreams(Controller::Storage["streams"]);
    // wait at least 10 seconds and keep listening for controller interrupts
    Controller::sleepInSteps(10);
  }
  Controller::prepareActiveConnectorsForShutdown();
}

void nukeStream(const std::string &streamName){
  std::deque<std::string> command;
  command.push_back(Util::getMyPath() + "MistUtilNuke");
  command.push_back(streamName);
  int stdIn = 0;
  int stdOut = 1;
  int stdErr = 2;
  Util::Procs::StartPiped(command, &stdIn, &stdOut, &stdErr);
}

bool checkStreamHealth(std::string &streamName, std::string &streamSource){
  if (streamSource.substr(0, 7) != "rtsp://"){
    // We do not check for input sources other than rtsp
    return true;
  }
  std::string cmdString = "ps aux | grep " + streamName;
  const char* cmdStringCStr = cmdString.c_str();
  char* cmd[] = {"/bin/bash", "-c", const_cast<char*>(cmdStringCStr), nullptr};
  std::string cmdOutput = Util::Procs::getOutputOf(cmd);
  uint8_t countMistInRTSP = 0;
  uint8_t countMistInBuffer = 0;
  // Split cmdOutput into lines
  std::istringstream iss(cmdOutput);
  std::string line;
  std::string rtspMatchString = "MistInRTSP -s " + streamName;
  std::string bufferMatchString = "MistInBuffer -s " + streamName;
  while (std::getline(iss, line)){
    DEVEL_MSG("[RTMPServer] %s", line.c_str());
    if (line.find(rtspMatchString) != std::string::npos){
      countMistInRTSP++;
    } else if (line.find(bufferMatchString) != std::string::npos){
      countMistInBuffer++;
    }
  }
  return (countMistInRTSP == 2 && countMistInBuffer == 2);
}

void streamHealth(void *np){
  #ifdef WITH_THREADNAMES
    pthread_setname_np(pthread_self(), "streamHealthMon");
  #endif
  /*
    A thread which runs every 20 minutes,
      - checks if there are not exactly (i.e. invalid state) 2 MistInRTSP and 2 MistInBuffer running for every always on stream in the config.
      - maintains a memory of streams that were not running during the previous check as well.
      - nukes those repeatedly invalid streams one-by-one.
  */
  while (Controller::conf.is_active){
    // Check every ~20 minutes
    Controller::sleepInSteps(1800);
    static std::unordered_set<std::string> reCheckList;
    static std::vector<std::string> nukeList;
    JSON::Value allStreams = Controller::Storage["streams"];
    jsonForEach(allStreams, jit){
      if (!Controller::conf.is_active){return;}
      // Check if this stream has an RTSP source
      if (!(*jit).isMember("source") ||  ((*jit).isMember("source") && (!((*jit)["source"].asString().substr(0, 7) == "rtsp://")))){
        continue;
      }
      // Check if this stream has always_on set to true
      if (!(*jit).isMember("always_on") || ((*jit).isMember("always_on") && !(*jit)["always_on"].asBool())){
        continue;
      }
      std::string streamName = jit.key();
      std::string streamSource = std::string((*jit)["source"]);
      bool isStreamHealthy = checkStreamHealth(streamName, streamSource);
      if (!isStreamHealthy){
        if (reCheckList.find(streamName) == reCheckList.end()){ // 'streamName' not found
          reCheckList.insert(streamName);
        }else{ // If found, remove from 'reCheckList' and append in 'nukeList'
          nukeList.push_back(streamName);
          reCheckList.erase(streamName);
        }
      }
    }

    for (size_t i = 0; i < nukeList.size(); ++i){
      if (!Controller::conf.is_active){return;}
      ERROR_MSG("[RTMPServer] Restarting unhealthy stream '%s'", nukeList[i].c_str());
      nukeStream(nukeList[i]);
      Util::sleep(10);
    }
    nukeList.clear();
  }
}

static unsigned long mix(unsigned long a, unsigned long b, unsigned long c){
  a = a - b;
  a = a - c;
  a = a ^ (c >> 13);
  b = b - c;
  b = b - a;
  b = b ^ (a << 8);
  c = c - a;
  c = c - b;
  c = c ^ (b >> 13);
  a = a - b;
  a = a - c;
  a = a ^ (c >> 12);
  b = b - c;
  b = b - a;
  b = b ^ (a << 16);
  c = c - a;
  c = c - b;
  c = c ^ (b >> 5);
  a = a - b;
  a = a - c;
  a = a ^ (c >> 3);
  b = b - c;
  b = b - a;
  b = b ^ (a << 10);
  c = c - a;
  c = c - b;
  c = c ^ (b >> 15);
  return c;
}

static void killProcessesOnPorts(const std::vector<int>& ports){
  for (int port : ports){
    std::stringstream cmd;
    cmd << "lsof -t -i :" << port;
    FILE* pipe = popen(cmd.str().c_str(), "r");

    char buffer[128];
    std::string result = "";
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr){
      result += buffer;
    }

    pclose(pipe);

    if (!result.empty()){
      std::stringstream killCmd;
      killCmd << "echo \"" << result << "\" | xargs kill -9";
      if (system(killCmd.str().c_str()) == 0){
        std::cout << "\x1b[33m[RTMPServer] [Recovery] Killed processes on reserved port " << port << "\x1b[0m\n";
      }
    }
  }
}

///\brief The main loop for the controller.
int main_loop(int argc, char **argv){
  Controller::Storage = JSON::fromFile("config.json");
  JSON::Value stored_port =
      JSON::fromString("{\"long\":\"port\", \"short\":\"p\", \"arg\":\"integer\", \"help\":\"TCP "
                       "port to listen on.\"}");
  stored_port["default"] = Controller::Storage["config"]["controller"]["port"];
  if (!stored_port["default"]){stored_port["default"] = 4242;}
  JSON::Value stored_interface = JSON::fromString(
      "{\"long\":\"interface\", \"short\":\"i\", \"arg\":\"string\", \"help\":\"Interface address "
      "to listen on, or 0.0.0.0 for all available interfaces.\"}");
  stored_interface["default"] = Controller::Storage["config"]["controller"]["interface"];
  if (!stored_interface["default"]){stored_interface["default"] = "0.0.0.0";}
  JSON::Value stored_user =
      JSON::fromString("{\"long\":\"username\", \"short\":\"u\", \"arg\":\"string\", "
                       "\"help\":\"Username to transfer privileges to, default is root.\"}");
  stored_user["default"] = Controller::Storage["config"]["controller"]["username"];
  if (!stored_user["default"]){stored_user["default"] = "root";}
  Controller::conf.addOption("port", stored_port);
  Controller::conf.addOption("interface", stored_interface);
  Controller::conf.addOption("username", stored_user);
  Controller::conf.addOption(
      "maxconnsperip",
      JSON::fromString("{\"long\":\"maxconnsperip\", \"short\":\"M\", \"arg\":\"integer\" "
                       "\"default\":0, \"help\":\"Max simultaneous sessions per unique IP address. "
                       "Only enforced if the USER_NEW trigger is in use.\"}"));
  Controller::conf.addOption(
      "account", JSON::fromString("{\"long\":\"account\", \"short\":\"a\", \"arg\":\"string\" "
                                  "\"default\":\"\", \"help\":\"A username:password string to "
                                  "create a new account with.\"}"));
  Controller::conf.addOption(
      "logfile", JSON::fromString("{\"long\":\"logfile\", \"short\":\"L\", \"arg\":\"string\" "
                                  "\"default\":\"\",\"help\":\"Redirect all standard output to a "
                                  "log file, provided with an argument\"}"));
  Controller::conf.addOption(
      "accesslog", JSON::fromString("{\"long\":\"accesslog\", \"short\":\"A\", \"arg\":\"string\" "
                                    "\"default\":\"LOG\",\"help\":\"Where to write the access log. "
                                    "If set to 'LOG' (the default), writes to wherever the log is "
                                    "written to. If empty, access logging is turned off. "
                                    "Otherwise, writes to the given filename.\"}"));
  Controller::conf.addOption(
      "configFile", JSON::fromString("{\"long\":\"config\", \"short\":\"c\", \"arg\":\"string\" "
                                     "\"default\":\"config.json\", \"help\":\"Specify a config "
                                     "file other than default.\"}"));
  Controller::conf.addOption(
      "prometheus",
      JSON::fromString("{\"long\":\"prometheus\", \"short\":\"S\", \"arg\":\"string\" "
                       "\"default\":\"\", \"help\":\"If set, allows collecting of Prometheus-style "
                       "stats on the given path over the API port.\"}"));
  Controller::conf.parseArgs(argc, argv);
  if (Controller::conf.getString("logfile") != ""){
    // open logfile, dup stdout to logfile
    int output = open(Controller::conf.getString("logfile").c_str(), O_APPEND | O_CREAT | O_WRONLY, S_IRWXU);
    if (output < 0){
      DEBUG_MSG(DLVL_ERROR, "Could not redirect output to %s: %s",
                Controller::conf.getString("logfile").c_str(), strerror(errno));
      return 7;
    }else{
      Controller::isTerminal = Controller::isColorized = false;
      dup2(output, STDOUT_FILENO);
      dup2(output, STDERR_FILENO);
      time_t rawtime;
      struct tm *timeinfo;
      struct tm tmptime;
      char buffer[25];
      time(&rawtime);
      timeinfo = localtime_r(&rawtime, &tmptime);
      strftime(buffer, 25, "%c", timeinfo);
      std::cerr << std::endl
                << std::endl
                << "!----" APPNAME " Started at " << buffer << " ----!\n";
    }
  }

  // We need to do this before we start the log reader, since the log reader might parse messages
  // from pushes, which block if this list is not read yet.
  Controller::readPushList();
  // reload config from config file
  Controller::Storage = JSON::fromFile(Controller::conf.getString("configFile"));

  {// free reserved ports by killing dangling connector processes in case when forked controller was uncleanly killed
    std::vector<int> portsToKill;
    portsToKill.push_back(stored_port["default"].asInt());
    portsToKill.push_back(UDP_API_PORT);
    jsonForEach(Controller::Storage["config"]["protocols"], jit){
      if ((*jit).isMember("port")){
        portsToKill.push_back((*jit)["port"].asInt());
      }
      if (((*jit)["connector"].asStringRef() == "HTTP") && (!(*jit).isMember("port"))){
        portsToKill.push_back(8080);
      }
      if (((*jit)["connector"].asStringRef() == "HLS") && (!(*jit).isMember("port"))){
        portsToKill.push_back(8081);
      }
      if (((*jit)["connector"].asStringRef() == "RTSP") && (!(*jit).isMember("port"))){
        portsToKill.push_back(5554);
      }
      if (((*jit)["connector"].asStringRef() == "RTMP") && (!(*jit).isMember("port"))){
        portsToKill.push_back(1935);
      }
      if (((*jit)["connector"].asStringRef() == "DTSC") && (!(*jit).isMember("port"))){
        portsToKill.push_back(4200);
      }
    }
    if (portsToKill.size()){
      killProcessesOnPorts(portsToKill);
    }
  }

  {// spawn thread that reads stderr of process
    std::string logPipe = Util::getTmpFolder() + "MstLog";
    if (mkfifo(logPipe.c_str(), S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH) != 0){
      if (errno != EEXIST){
        ERROR_MSG("Could not create log message pipe %s: %s", logPipe.c_str(), strerror(errno));
      }
    }
    int inFD = -1;
    if ((inFD = open(logPipe.c_str(), O_RDONLY | O_NONBLOCK)) == -1){
      ERROR_MSG("Could not open log message pipe %s: %s; falling back to unnamed pipe",
                logPipe.c_str(), strerror(errno));
      int pipeErr[2];
      if (pipe(pipeErr) >= 0){
        dup2(pipeErr[1], STDERR_FILENO); // cause stderr to write to the pipe
        close(pipeErr[1]);               // close the unneeded pipe file descriptor
        // Start reading log messages from the unnamed pipe
        Util::Procs::socketList.insert(pipeErr[0]); // Mark this FD as needing to be closed before forking
        tthread::thread msghandler(Controller::handleMsg, (void *)(((char *)0) + pipeErr[0]));
        msghandler.detach();
      }
    }else{
      // Set the read end to blocking mode
      int inFDflags = fcntl(inFD, F_GETFL, 0);
      fcntl(inFD, F_SETFL, inFDflags & (~O_NONBLOCK));
      // Start reading log messages from the named pipe
      Util::Procs::socketList.insert(inFD); // Mark this FD as needing to be closed before forking
      tthread::thread msghandler(Controller::handleMsg, (void *)(((char *)0) + inFD));
      msghandler.detach();
      // Attempt to open and redirect log messages to named pipe
      int outFD = -1;
      if (getenv("MIST_NO_PRETTY_LOGGING")){
        WARN_MSG(
            "MIST_NO_PRETTY_LOGGING is active, printing lots of pipes");
      }
      else if ((outFD = open(logPipe.c_str(), O_WRONLY)) == -1){
        ERROR_MSG(
            "Could not open log message pipe %s for writing! %s; falling back to standard error",
            logPipe.c_str(), strerror(errno));
      }else{
        dup2(outFD, STDERR_FILENO); // cause stderr to write to the pipe
        close(outFD);               // close the unneeded pipe file descriptor
      }
    }
    setenv("MIST_CONTROL", "1", 0); // Signal in the environment that the controller handles all children
  }

  if (Controller::Storage.isMember("config_split")){
    jsonForEach(Controller::Storage["config_split"], cs){
      if (cs->isString()){
        JSON::Value tmpConf = JSON::fromFile(cs->asStringRef());
        if (tmpConf.isMember(cs.key())){
          INFO_MSG("Loading '%s' section of config from file %s", cs.key().c_str(), cs->asStringRef().c_str());
          Controller::Storage[cs.key()] = tmpConf[cs.key()];
        }else{
          WARN_MSG("There is no '%s' section in file %s; skipping load", cs.key().c_str(), cs->asStringRef().c_str());
        }
      }
    }
  }

  if (Controller::conf.getOption("debug", true).size() > 1){
    Controller::Storage["config"]["debug"] = Controller::conf.getInteger("debug");
  }
  if (Controller::Storage.isMember("config") && Controller::Storage["config"].isMember("debug") &&
      Controller::Storage["config"]["debug"].isInt()){
    Util::printDebugLevel = Controller::Storage["config"]["debug"].asInt();
  }
  // check for port, interface and username in arguments
  // if they are not there, take them from config file, if there
  if (Controller::Storage["config"]["controller"]["port"]){
    Controller::conf.getOption("port", true)[0u] =
        Controller::Storage["config"]["controller"]["port"];
  }
  if (Controller::Storage["config"]["controller"]["interface"]){
    Controller::conf.getOption("interface", true)[0u] = Controller::Storage["config"]["controller"]["interface"];
  }
  if (Controller::Storage["config"]["controller"]["username"]){
    Controller::conf.getOption("username", true)[0u] = Controller::Storage["config"]["controller"]["username"];
  }
  if (Controller::Storage["config"]["controller"].isMember("prometheus")){
    if (Controller::Storage["config"]["controller"]["prometheus"]){
      Controller::Storage["config"]["prometheus"] =
          Controller::Storage["config"]["controller"]["prometheus"];
    }
    Controller::Storage["config"]["controller"].removeMember("prometheus");
  }
  if (Controller::Storage["config"]["prometheus"]){
    Controller::conf.getOption("prometheus", true)[0u] =
        Controller::Storage["config"]["prometheus"];
  }
  if (Controller::Storage["config"].isMember("accesslog")){
    Controller::conf.getOption("accesslog", true)[0u] = Controller::Storage["config"]["accesslog"];
  }
  Controller::Storage["config"]["prometheus"] = Controller::conf.getString("prometheus");
  Controller::Storage["config"]["accesslog"] = Controller::conf.getString("accesslog");
  Controller::normalizeTrustedProxies(Controller::Storage["config"]["trustedproxy"]);
  if (!Controller::Storage["config"]["sessionViewerMode"]){
    Controller::Storage["config"]["sessionViewerMode"] = SESS_BUNDLE_DEFAULT_VIEWER;
  }
  if (!Controller::Storage["config"]["sessionInputMode"]){
    Controller::Storage["config"]["sessionInputMode"] = SESS_BUNDLE_DEFAULT_OTHER;
  }
  if (!Controller::Storage["config"]["sessionOutputMode"]){
    Controller::Storage["config"]["sessionOutputMode"] = SESS_BUNDLE_DEFAULT_OTHER;
  }
  if (!Controller::Storage["config"]["sessionUnspecifiedMode"]){
    Controller::Storage["config"]["sessionUnspecifiedMode"] = 0;
  }
  if (!Controller::Storage["config"]["sessionStreamInfoMode"]){
    Controller::Storage["config"]["sessionStreamInfoMode"] = SESS_DEFAULT_STREAM_INFO_MODE;
  }
  if (!Controller::Storage["config"].isMember("tknMode")){
    Controller::Storage["config"]["tknMode"] = SESS_TKN_DEFAULT_MODE;
  }
  Controller::prometheus = Controller::Storage["config"]["prometheus"].asStringRef();
  Controller::accesslog = Controller::Storage["config"]["accesslog"].asStringRef();
  Controller::writeConfig();
  if (!Controller::conf.is_active){
    std::cout << "\x1b[31mReceived signal interrupt while setting up forked MistController process!\x1b[0m";
    return 0;
  }
  Controller::checkAvailProtocols();
  Controller::checkAvailTriggers();
  Controller::writeCapabilities();
  Controller::updateBandwidthConfig();
  createAccount(Controller::conf.getString("account"));
  Controller::conf.activate(); // activate early, so threads aren't killed.

#if !defined(__CYGWIN__) && !defined(_WIN32)
  {
    uint64_t mem_total = 0, mem_free = 0, mem_bufcache = 0, shm_total = 0, shm_free = 0;
    std::ifstream meminfo("/proc/meminfo");
    if (meminfo){
      char line[300];
      while (meminfo.good()){
        meminfo.getline(line, 300);
        if (meminfo.fail()){
          // empty lines? ignore them, clear flags, continue
          if (!meminfo.eof()){
            meminfo.ignore();
            meminfo.clear();
          }
          continue;
        }
        long long int i;
        if (sscanf(line, "MemTotal : %lli kB", &i) == 1){mem_total = i;}
        if (sscanf(line, "MemFree : %lli kB", &i) == 1){mem_free = i;}
        if (sscanf(line, "Buffers : %lli kB", &i) == 1){mem_bufcache += i;}
        if (sscanf(line, "Cached : %lli kB", &i) == 1){mem_bufcache += i;}
      }
    }
    struct statvfs shmd;
    IPC::sharedPage tmpCapa(SHM_CAPA, DEFAULT_CONF_PAGE_SIZE, false, false);
    if (tmpCapa.mapped && tmpCapa.handle){
      fstatvfs(tmpCapa.handle, &shmd);
      shm_free = (shmd.f_bfree * shmd.f_frsize) / 1024;
      shm_total = (shmd.f_blocks * shmd.f_frsize) / 1024;
    }

    if (mem_free + mem_bufcache < 1024 * 1024){
      WARN_MSG("You have very little free RAM available (%" PRIu64
               " MiB). While Mist will run just fine with this amount, do note that random crashes "
               "may occur should you ever run out of free RAM. Please be pro-active and keep an "
               "eye on the RAM usage!", (mem_free + mem_bufcache)/1024);
    }
    if (shm_free < 1024 * 1024 && mem_total > 1024 * 1024 * 1.12){
      WARN_MSG("You have very little shared memory available (%" PRIu64
               " MiB). Mist heavily relies on shared memory: please ensure your shared memory is "
               "set to a high value, preferably ~95%% of your total available RAM.",
               shm_free / 1024);
      if (shm_total == 65536){
        WARN_MSG("Tip: If you are using docker, e.g. add the `--shm-size=%" PRIu64
                 "m` parameter to your `docker run` command to fix this.",
                 (uint64_t)(mem_total * 0.95 / 1024));
      }else{
        WARN_MSG("Tip: In most cases, you can change the shared memory size by running `mount -o "
                 "remount,size=%" PRIu64
                 "m /dev/shm` as root. Doing this automatically every boot depends on your "
                 "distribution: please check your distro's documentation for instructions.",
                 (uint64_t)(mem_total * 0.95 / 1024));
      }
    }else if (shm_total <= mem_total / 2){
      WARN_MSG("Your shared memory is half or less of your RAM (%" PRIu64 " / %" PRIu64
               " MiB). Mist heavily relies on shared memory: please ensure your shared memory is "
               "set to a high value, preferably ~95%% of your total available RAM.",
               shm_total / 1024, mem_total / 1024);
      if (shm_total == 65536){
        WARN_MSG("Tip: If you are using docker, e.g. add the `--shm-size=%" PRIu64
                 "m` parameter to your `docker run` command to fix this.",
                 (uint64_t)(mem_total * 0.95 / 1024));
      }else{
        WARN_MSG("Tip: In most cases, you can change the shared memory size by running `mount -o "
                 "remount,size=%" PRIu64
                 "m /dev/shm` as root. Doing this automatically every boot depends on your "
                 "distribution: please check your distro's documentation for instructions.",
                 (uint64_t)(mem_total * 0.95 / 1024));
      }
    }
  }
#endif

  // if a terminal is connected and we're not logging to file
  if (Controller::isTerminal){
    // check for username
    if (!Controller::Storage.isMember("account") || Controller::Storage["account"].size() < 1){
      std::string in_string = "";
      while (yna(in_string) == 'x' && Controller::conf.is_active){
        std::cout << "Account not set, do you want to create an account? (y)es, (n)o, (a)bort: ";
        std::cout.flush();
        std::getline(std::cin, in_string);
        switch (yna(in_string)){
        case 'y':{
          // create account
          std::string usr_string = "";
          while (!(Controller::Storage.isMember("account") && Controller::Storage["account"].size() > 0) &&
                 Controller::conf.is_active){
            std::cout << "Please type in the username, a colon and a password in the following "
                         "format; username:password"
                      << std::endl
                      << ": ";
            std::cout.flush();
            std::getline(std::cin, usr_string);
            createAccount(usr_string);
          }
        }break;
        case 'a': return 0; // abort bootup
        case 't':{
          createAccount("test:test");
          if ((Controller::capabilities["connectors"].size()) &&
              (!Controller::Storage.isMember("config") || !Controller::Storage["config"].isMember("protocols") ||
               Controller::Storage["config"]["protocols"].size() < 1)){
            // create protocols
            jsonForEach(Controller::capabilities["connectors"], it){
              if (!it->isMember("required")){
                JSON::Value newProtocol;
                newProtocol["connector"] = it.key();
                Controller::Storage["config"]["protocols"].append(newProtocol);
              }
            }
          }
        }break;
        }
      }
    }
    // check for protocols
    if ((Controller::capabilities["connectors"].size()) &&
        (!Controller::Storage.isMember("config") || !Controller::Storage["config"].isMember("protocols") ||
         Controller::Storage["config"]["protocols"].size() < 1)){
      std::string in_string = "";
      while (yna(in_string) == 'x' && Controller::conf.is_active){
        std::cout << "Protocols not set, do you want to enable default protocols? (y)es, (n)o, "
                     "(a)bort: ";
        std::cout.flush();
        std::getline(std::cin, in_string);
        if (yna(in_string) == 'y'){
          // create protocols
          jsonForEach(Controller::capabilities["connectors"], it){
            if (!it->isMember("required")){
              JSON::Value newProtocol;
              newProtocol["connector"] = it.key();
              Controller::Storage["config"]["protocols"].append(newProtocol);
            }
          }
        }else if (yna(in_string) == 'a'){
          // abort controller startup
          return 0;
        }
      }
    }
  }

  /// [RTMPServer] Check the Configured streams is less then total number of cameras provided as per as license
  // if (Controller::mode == "ONLINE"){
  //   Controller::online();
  // }
  // int onboardedStreams = Controller::Storage["streams"].size();
  // if ((onboardedStreams > (Controller::maximumCameras)) && Controller::maximumCameras != -1){
  //     FAIL_MSG("[EXIT] [RTMPServer] Based on your licensing agreement, you have added a substantial number of streams");
  //     Controller::conf.is_active = false;
  // }

  /// [RTMPServer] Load the database for traffic statistics and intialize the sharedPage for traffic stats (only if \b TRAFFIC_CONSUMPTION = ON)
  sqlite3 *db = nullptr;

  if (Controller::conf.trafficConsumption){
    bool isDbOpen = Database::loadDatabase(db);
    if (!isDbOpen){
      ERROR_MSG("[RTMPServer] Could not open SQL DB while initializing controller");
      Controller::conf.trafficConsumption = false;
    }
    if (isDbOpen){
      Database::createTable2(db);
      Database::closeDatabase(db);
    }
  }

  {// Check if we have a usable server, if not, print messages with helpful hints
    std::string web_port = JSON::Value(Controller::conf.getInteger("port")).asString();
    // check for username
    if (!Controller::Storage.isMember("account") || Controller::Storage["account"].size() < 1){
      Controller::Log("CONF",
                      "No login configured. To create one, attempt to login through the web "
                      "interface on port " +
                          web_port + " and follow the instructions.");
    }
    // check for protocols
    if (!Controller::Storage.isMember("config") || !Controller::Storage["config"].isMember("protocols") ||
        Controller::Storage["config"]["protocols"].size() < 1){
      Controller::Log("CONF", "No protocols enabled, remember to set them up through the web "
                              "interface on port " +
                                  web_port + " or API.");
    }
    // check for streams - regardless of logfile setting
    if (!Controller::Storage.isMember("streams") || Controller::Storage["streams"].size() < 1){
      Controller::Log("CONF", "No streams configured, remember to set up streams through the web "
                              "interface on port " +
                                  web_port + " or API.");
    }
  }

  {// Upgrade old configurations
    bool foundCMAF = false;
    bool edit = false;
    JSON::Value newVal;
    jsonForEach(Controller::Storage["config"]["protocols"], it){
      if ((*it)["connector"].asStringRef() == "HSS"){
        edit = true;
        continue;
      }
      if ((*it)["connector"].asStringRef() == "DASH"){
        edit = true;
        continue;
      }

      if ((*it)["connector"].asStringRef() == "CMAF"){foundCMAF = true;}
      newVal.append(*it);
    }
    if (edit && !foundCMAF){newVal.append(JSON::fromString("{\"connector\":\"CMAF\"}"));}
    if (edit){
      Controller::Storage["config"]["protocols"] = newVal;
      Controller::Log("CONF", "Translated protocols to new versions");
    }
  }

  // Generate instanceId once per boot.
  if (Controller::instanceId == ""){
    srand(mix(clock(), time(0), getpid()));
    do{
      Controller::instanceId += (char)(64 + rand() % 62);
    }while (Controller::instanceId.size() < 16);
  }

  Controller::Log("CONF", "Launching Controller threads");
  // start offline licensing thread
  // tthread::thread licenseThread(Controller::checkExpiryDate, 0);
  // start central licensing thread
  // tthread::thread CentralLicenseThread(Controller::centralLicensing, 0);
  // start UDP API thread
  tthread::thread UDPAPIThread(Controller::handleUDPAPI, 0);
  // start monitoring thread
  tthread::thread monitorThread(statusMonitor, 0);
  // start push checking thread
  tthread::thread pushThread(Controller::pushCheckLoop, 0);
  // start stats thread
  tthread::thread statsThread(Controller::SharedMemStats, &Controller::conf);
  // start stream health check thread
  tthread::thread streamHealthThread(streamHealth, 0);
  // start traffic statistics thread only if TRAFFIC_CONSUMPTION = ON
  tthread::thread *trafficStatisticsThread = nullptr;
  if (Controller::conf.trafficConsumption){
    trafficStatisticsThread = new tthread::thread(Controller::trafficThread2, 0);
  }
  Controller::Log("CONF", "All Controller threads launched");

  // start main loop
  while (Controller::conf.is_active){
    // Serve TCP API requests via threaded socket, which is already capable of concurrency
    Controller::conf.serveThreadedSocket(Controller::handleAPIConnection);
    // print shutdown reason
    std::string shutdown_reason;
    if (!Controller::conf.is_active){
      shutdown_reason = "user request (received shutdown signal)";
    }else{
      shutdown_reason = "socket problem (API port closed)";
    }
    /*LTS-START*/
    if (Triggers::shouldTrigger("SYSTEM_STOP")){
      if (!Triggers::doTrigger("SYSTEM_STOP", shutdown_reason)){
        Controller::conf.is_active = true;
        Util::sleep(1000);
      }else{
        Controller::Log("EXIT", "\x1b[31mController shutting down because of " + shutdown_reason + "\x1b[0m");
        Controller::conf.is_active = false;
      }
    }else{
      /*LTS-END*/
      Controller::conf.is_active = false;
      Controller::Log("EXIT", "\x1b[31mController shutting down because of " + shutdown_reason + "\x1b[0m");
      /*LTS-START*/
    }
    // Closing the database if trafficConsumption is ON
    if (Controller::conf.trafficConsumption && db != nullptr){
      int errCode = 0;
      uint8_t maxRetries = 2;
      errCode = Database::closeDatabase(db);
      if (errCode != 0){
        WARN_MSG("Initial attempt to close database failed");
      }
      while (maxRetries-- && errCode != 0){
        errCode = Database::closeDatabase(db);
        if (errCode == 0){
          INFO_MSG("Database closed successfully");
        }else{
          WARN_MSG("Attempt %u to close database failed", 2 - maxRetries);
          Util::sleep(1);
        }
      }
      if (errCode != 0){FAIL_MSG("Database connection is still open after 3 attempts");}
    }
    /*LTS-END*/
  }
  // First write config to disk
  Controller::Log("EXIT", "\x1b[31m[RTMPServer] First writing config to disk...\x1b[0m");
  tthread::lock_guard<tthread::mutex> guard(Controller::configMutex);
  Controller::writeConfigToDisk();
  Controller::Log("EXIT", "\x1b[32m[RTMPServer] Written config to disk.\x1b[0m");
  // We cannot try waiting for lock over logMutex, just stop MistController cleanly asap!
  // tthread::lock_guard<tthread::mutex> guard2(Controller::logMutex);
  // join all joinable threads
  Controller::Log("EXIT", "\x1b[31m[RTMPServer] Joining Central license thread...\x1b[0m");
  // CentralLicenseThread.join();
  Controller::Log("EXIT", "\x1b[31m[RTMPServer] Joining license thread...\x1b[0m");
  // licenseThread.join();
  Controller::Log("EXIT", "\x1b[31m[RTMPServer] Joining monitor thread...\x1b[0m");
  monitorThread.join();
  Controller::Log("EXIT", "\x1b[31m[RTMPServer] Joining UDP API thread...\x1b[0m");
  UDPAPIThread.join();
  Controller::Log("EXIT", "\x1b[31m[RTMPServer] Joining push thread...\x1b[0m");
  pushThread.join();
  Controller::Log("EXIT", "\x1b[31m[RTMPServer] Joining stats thread...\x1b[0m");
  statsThread.join();
  Controller::Log("EXIT", "\x1b[31m[RTMPServer] Joining stream health check thread...\x1b[0m");
  streamHealthThread.join();
  if (trafficStatisticsThread && Controller::conf.trafficConsumption){
    Controller::Log("EXIT", "\x1b[31m[RTMPServer] Joining traffic statistics thread...\x1b[0m");
    trafficStatisticsThread->join();
  }
  Controller::Log("EXIT", "\x1b[31m[RTMPServer] Stopping all child controller processes...\x1b[0m");
  Util::Procs::StopAll();
  // close stderr to make the stderr reading thread exit
  close(STDERR_FILENO);
  std::cout << "\x1b[31m[EXIT] [RTMPServer] main_loop returned (0)\x1b[0m\n";
  return 0;
}

///\brief The controller angel process.
/// Starts a forked main_loop in a loop. Yes, you read that right.
int main(int argc, char **argv){
  Util::Procs::setHandler(); // set child handler

  Controller::conf = Util::Config(argv[0]);
  Util::Config::binaryType = Util::CONTROLLER;

  // Set the required Controller envs first
  char* trafficConsumptionKey = getenv("TRAFFIC_CONSUMPTION");
  if (trafficConsumptionKey){
    Controller::conf.trafficConsumption = (std::string(trafficConsumptionKey) == "ON") ? true : false;
    if (Controller::conf.trafficConsumption){
      // std::cout << "RTMPServer | TRAFFIC_CONSUMPTION feature is enabled\n";
    }else{
      // std::cout << "RTMPServer | TRAFFIC_CONSUMPTION feature is disabled\n";
    }
  }else{
    // std::cout << "RTMPServer | TRAFFIC_CONSUMPTION key not found - keeping disabled\n";
  }

  char* modeKey = getenv("MODE");
  if (modeKey){
    Controller::mode = std::string(modeKey);
    if (Controller::mode == "ONLINE"){
      // std::cout << "RTMPServer | License MODE found and set to ONLINE\n";
    }else if (Controller::mode == "OFFLINE"){
      // std::cout << "RTMPServer | License MODE found and set to OFFLINE\n";
      Controller::mode = "OFFLINE";
    }else{
      // std::cout << "RTMPServer | License MODE found invalid - using ONLINE\n";
    }
  }else{
    // std::cout << "[RTMPServer] License MODE key not found - using ONLINE by default\n";
  }

  char* debugCentralKey = getenv("DEBUGCENTRAL");
  if (debugCentralKey){
    Controller::debugCentral = (std::string(debugCentralKey) == "ON") ? true : false;
    if (Controller::debugCentral){
      // std::cout << "RTMPServer | DEBUGCENTRAL key found - setting ON\n";
    }else{
      // std::cout << "RTMPServer | DEBUGCENTRAL key found - setting OFF\n";
    }
  }else{
    // std::cout << "RTMPServer | DEBUGCENTRAL key could not be found - keeping OFF\n";
  }

  // Activate the config in either ONLINE or OFFLINE license
  // if (Controller::mode == "OFFLINE"){
  //   Controller::offline();
  // }else{
  Controller::conf.activate();
  // }

  std::string recordExt = Controller::initializeRecordExt();
  // std::cout << "RTMPServer | Would record in " << recordExt << " format\n";

  Controller::setAgentMode();
  // std::cout << "RTMPServer | Running in " << (Controller::isAgentMode?"Agent":"Master") << " mode\n";

  if (getenv("ATHEIST")){return main_loop(argc, argv);}
  uint64_t reTimer = 0;
  while (Controller::conf.is_active){
    Controller::lastBootTime = Util::epoch();
    Util::Procs::fork_prepare();
    pid_t pid = fork();
    if (pid == 0){
      Util::Procs::fork_complete();
      return main_loop(argc, argv);
    }
    Util::Procs::fork_complete();
    if (pid == -1){
      FAIL_MSG("Unable to spawn controller process!");
      return 2;
    }
    // wait for the process to exit
    int status;
    while (waitpid(pid, &status, 0) != pid){
      if (errno == EINTR){
        if (!Controller::conf.is_active){
          std::cout << "\x1b[31m[EXIT] [RTMPServer] Shutting down controller because of signal interrupt...\x1b[0m\n";
          Util::Procs::Stop(pid);
        }
      }else{
        std::cout << "\x1b[31m[EXIT] [RTMPServer] Controller received singal " << errno << "...\x1b[0m\n";
      }
      continue;
    }
    // if the exit was clean, don't restart it
    if (WIFEXITED(status)){
      if (WEXITSTATUS(status) == 0){
        std::cout << "\x1b[31m[EXIT] [RTMPServer] Controller shut down cleanly\x1b[0m\n";
        break;
      }else{
        std::cout << "\x1b[31m[EXIT] [RTMPServer] Controller uncleanly shut down with exit status " << WEXITSTATUS(status) << \
        "and error code " << errno << "\x1b[0m\n";
      }
    }else{
      std::cout << "\x1b[31m[EXIT] [RTMPServer] Controller uncleanly shut down without a set exit status!\x1b[0m\n";
    }
    std::cout << "\x1b[31m[EXIT] [RTMPServer] Restarting controller in " << std::to_string(reTimer) << "ms..." << "\x1b[0m\n";
    Util::wait(reTimer);
    reTimer += 1000;
  }
  return 0;
}
