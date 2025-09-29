#include "controller_license.h"
#include "controller_api.h"
#include "controller_storage.h"
#include "controller_streams.h"
#include <mist/auth.h>
#include <mist/procs.h>
#include <mist/stream.h>
#include <mist/timing.h>
#include <mist/nlohmann.h>
#include <dirent.h> //for getMyExec


namespace Controller{
  int maximumCameras = -1;                       ///< Total number of cameras as per as license
  std::string expiryDate;                        ///< ExpiryDate of license
  std::string hardwareId;                        ///< System hardware id
  std::string organization;
  std::string ciphertext;
  JSON::Value data;
  std::time_t expiryDateInEpoch;
  bool hardwareIdCheck = false;
  std::string timezone = "UTC";
  int gmtSec = 0;
  std::string key = "MIIEuwIBADANBgkqhkiG9w0BAQEFAASCBKUwggShAgEAAoIBAQDVqZC5WwAY4+agwrzwnFgxI+x8SrDXklBTfxsjMgmbRny4MwOlkPv59Qs7f2Py1wfyHCFy1N8022sFQfYPZgdicH5eHN3kqkvsK2FVcwr+NAd/WFB7rMQHyXBHd9jMAnO6w2dCjp4B0AAKZCYQA7OOjvKnMz8WazbIDbNMaAAFvaAXHA3dhuRVi/P0gMGZUCnm1dsw50ammm/gKIQFnHdNlA0iL5HnvOGnXWcgrtccup/mFKlFNqLS4Ebbz0wE+KmQo1tUpiHCuuAXipf5rySrTIiIVcQ/wX4d7knzp8C0d3f7P6a6kgtphEYn30Q3bWicuZ+LwxVVH8D4TjRhDuslAgERAoIBABkjAfewWl1H/QParNEDVatto1nqq2SnzTb/5RMzEDBipULY00CnpSx3Ln94ZhyRxLMSXkm+sNkKwUvppHpIPRqj0qGpCwvXzLJfdNzgW6VviGlVrx2b2tO9WIDg7FQ8hhX43vjFfAA2lpfPjAHiUVwQ0UDY2j7fb9tM5+rfDw+7va2P4DFU9bfw0NlR8PhjnpFEU+vt+krzGHQMK2U14cyeuhJK/ziNBL43AEhetcfv2bX4o9fe/wTepriINm9xWLhoADW3iV+EL9WfSSwNiQpG8SGyYbN93hY+D7rOzF8H+YtuUK8BHmwG+FHZqg4lRQbm6dx14DdZd6LhIoLjvvECgYEA7iomGDL9mcx2sFED3LDUARX4/WNO+8AdZEetX5y6YdxJEjSCsjFTj8DoS+cyVKLBfljjgE0BU2qy2HCurh4NmTAb/Ds29humDr4i9IGBmAZF4qmguymUoZG2I7VLXLIihEvew5321QDDfjdD+dxSzh/+MEUpMDhumdxvJy/a28ECgYEA5amvBAc3Ie2WVGxEZKktZP4YFHgxe2lqu74Ts4yYFiYFzVIvZlW6qZWhmNJXeVaCZwJvuE1N177I5UqazzOnnkETXVDXG/sxOsGdtYG6f82rpEBIyqB+/M8ooIp+mtqRKo43NU0Rs9KFiTjhJOLdz8IQq3Sv80Y4rjNlZ9ikOGUCgYBGDGWOpZXh8NedRQEiypi1BnZopLzgoeqG5+eytaA65m/YLY/aDoH9Gp6s6aVkL96Okp1D2mnNPX/lTlF+gU9LLER3XLXP6gOp+6/tj4B4AdhRuWuCV4YRZxdz6gcbQ3OQUozQH2a3Hledl8iy5nK1GHfwFFdoiRF4jCCxLDFPsQKBgCiHWx7UGMnAg/DHz9WHRD7/qeV+n1IDi05OuC6+dTEVxMnhU6ilt4dWpAvo4kKWy7fTQORJ4I98BVWjwPdjd+7AMJf/NQTwF78TG9S8ilLJ8Rz8SRSy6TusFjpyywxEsDSvr2PCTmsHCIGgoDOvkI4TMB5BxLJ1zcRjbD+AlXNdAoGBAJl4PCcJU9TNlirRwtFK/B6FS2aDqPs8SsGBPH++u7DMHZNtWREEnoYGjzBX3P/FsHSgGzIX/MewSeui/qCoseB1tHo9UVIiMbDc03sqxx90kix+AMwzi5rkDIN8l2vtDm83RtOZp6LYkxNwY5LDEp6mXMN8rBF5Ci8IPbts3z7B";
  std::string mode = "ONLINE";
  std::map<std::string,std::string> centralKeys;
  std::string uuid;
  bool debugCentral = false;
  std::string licenseApi = "https://jarvis.live/central-licensing/validate-license/";
  int expiryCheckInterval;
  int failureIgnoreIntervel;
  std::string centralExpiry;

  std::string base64Decode(const std::string& encodedKey){
    CryptoPP::Base64Decoder decoder;
    std::string decodedKey;

    decoder.Attach(new CryptoPP::StringSink(decodedKey));
    decoder.Put(reinterpret_cast<const unsigned char*>(encodedKey.c_str()), encodedKey.length());
    decoder.MessageEnd();

    return decodedKey;
  }

  std::string base64Encode(const std::string& input){
    CryptoPP::Base64Encoder encoder;
    std::string encoded;

    encoder.Attach(new CryptoPP::StringSink(encoded));
    encoder.Put(reinterpret_cast<const unsigned char*>(input.c_str()), input.length());
    encoder.MessageEnd();

    return encoded;
  }

  void decodeLicense(const std::string& privateKeyString, const std::string& ciphertext){
    CryptoPP::RSA::PrivateKey privateKey;
    CryptoPP::StringSource privateKeySource(privateKeyString, true);
    privateKey.Load(privateKeySource);

    std::string decryptedtext;
    CryptoPP::RSAES_OAEP_SHA_Decryptor decryptor(privateKey);
    CryptoPP::AutoSeededRandomPool rng;

    CryptoPP::StringSource(ciphertext, true,
                           new CryptoPP::PK_DecryptorFilter(rng, decryptor,
                           new CryptoPP::StringSink(decryptedtext)));

    data = JSON::fromString(decryptedtext);
    dataParser(data);
  }

  std::string epochToString(std::time_t epoch){
    struct tm* timeinfo;
    timeinfo = localtime(&epoch);
    std::ostringstream oss;
    oss << std::put_time(timeinfo, "%a %d %b %Y, %H:%M:%S");
    return oss.str();
  }

  void dataParser(JSON::Value &data){
    std::string temp = data.toPrettyString();
    expiryDate = std::string(data["expiryDate"]);
    hardwareId = std::string(data["hardwareId"]);
    if (std::string(data["totalCameras"]).size() != 0){
      Controller::maximumCameras = std::stoi(data["totalCameras"]);
    }
    organization = std::string(data["organization"]);
    // write to file
    std::ofstream idFileDesc(ID_FILE_PATH, std::ios::trunc);
    if (idFileDesc.is_open()){
      idFileDesc << organization;
    }else{
      FAIL_MSG("Could not open file to write organisation name '%s'", ID_FILE_PATH);
    }
    expiryDateInEpoch = stringToEpochTime(expiryDate.substr(0,19));

    std::size_t found = expiryDate.find("IST");
    if (found != std::string::npos){
      timezone = "IST";
      gmtSec = 19800;
    }
  }

  bool checkLicenseFile(void){
    std::string line;
    std::ifstream myfile(LICENSE_FILE_PATH);
    if (myfile.is_open()){
      while (getline (myfile,line)){
        if (line.substr(0,10) == "LICENSE ID"){ciphertext = line.substr(15);}
      }
      myfile.close();
      if (ciphertext.size() == 0){
        ERROR_MSG("License key is invalid");
        return false;
      }
      /// decode the license
      try {
        Controller::decodeLicense(base64Decode(key), base64Decode(ciphertext));
        return true;
      }
      catch(CryptoPP::Exception& e){
        ERROR_MSG("License is not valid");
      }
    }
    return false;
  }

  std::time_t stringToEpochTime(const std::string& dateTimeStr){
    std::tm timeStruct = {};
    std::istringstream iss(dateTimeStr);
    iss >> std::get_time(&timeStruct, "%Y-%m-%d %H:%M:%S");
    std::chrono::system_clock::time_point timePoint = std::chrono::system_clock::from_time_t(std::mktime(&timeStruct));

    return std::chrono::duration_cast<std::chrono::seconds>(timePoint.time_since_epoch()).count();
  }

  void checkExpiryDate(void *np){
    if (Controller::mode != "OFFLINE") return;
    while (Controller::conf.is_active){
      if (expiryDate.size() && Util::epoch() > (expiryDateInEpoch - gmtSec)){
        FAIL_MSG("LICENSE EXPIRED");
        Controller::conf.is_active = false;
      }
      /* Simulate segmented sleep for avoiding unjoinability */
      Controller::sleepInSteps(DEFAULT_LICENSE_INTERVAL/1000);
    }
  }

  bool checkHardwareId(const std::string &id){
    if (id.size() == 0){return true;}
    return Controller::checkSerial(id);
  }

  bool checkSerial(const std::string &ser){
    bool ret = false;
    char serFile[300];
    struct stat statbuf;
    char serial[300];
    DIR *d = opendir("/sys/block");
    struct dirent *dp;
    if (d){
      do{
        errno = 0;
        if ((dp = readdir(d))){
          if (strncmp(dp->d_name, "loop", 4) != 0 && dp->d_name[0] != '.'){
            snprintf(serFile, 300, "/sys/block/%s/device/serial", dp->d_name);
            if (!stat(serFile, &statbuf)){
              FILE * fd = fopen(serFile, "r");
              int len = fread(serial, 1, 300, fd);
              if (len && len >= ser.size()){
                if (!strncmp(ser.data(), serial, ser.size())){
                  ret = true;
                  fclose(fd);
                  break;
                }
              }
              fclose(fd);
            }
            snprintf(serFile, 300, "/sys/block/%s/device/wwid", dp->d_name);
            if (!stat(serFile, &statbuf)){
              FILE * fd = fopen(serFile, "r");
              int len = fread(serial, 1, 300, fd);
              if (len && len >= ser.size()){
                std::string fullLine(serial, len);
                while (fullLine.size() && fullLine[fullLine.size()-1] < 33){fullLine.erase(fullLine.size()-1);}
                size_t lSpace = fullLine.rfind(' ');
                if (lSpace != std::string::npos){
                  std::string curSer = fullLine.substr(lSpace+1);
                  if (curSer.size() > ser.size()){curSer = curSer.substr(0, ser.size());}
                  //INFO_MSG("Comparing with: %s", curSer.c_str());
                  if (ser == curSer){
                    ret = true;
                    fclose(fd);
                    break;
                  }
                }else{
                  if (ser == fullLine){
                    ret = true;
                    fclose(fd);
                    break;
                  }
                }
              }
              fclose(fd);
            }
          }
        }
      }while (dp != NULL);
      closedir(d);
    }
    if (ret){return true;}
    d = opendir("/dev/disk/by-id");
    if (d){
      do{
        errno = 0;
        if ((dp = readdir(d))){
          std::string fn = dp->d_name;
          if (fn.size() >= ser.size() && fn.substr(fn.size() - ser.size()) == ser){
            ret = true;
            break;
          }
        }
      }while (dp != NULL);
      closedir(d);
    }
    return ret;
  }

  std::string displayTime(const std::string& inputDateTime){
    std::tm t;
    std::istringstream ss(inputDateTime);
    ss >> std::get_time(&t, "%Y-%m-%d %H:%M:%S");
    std::time_t time = std::mktime(&t);
    t = *std::localtime(&time);
    const int ISTOffset = 19800;
    time += ISTOffset;
    t = *std::gmtime(&time);
    std::ostringstream oss;
    oss << std::put_time(&t, "%a %d %b %Y, %H:%M:%S");
    return oss.str();
  }

  std::string epchToStr(std::time_t epoch){
    struct tm* timeinfo;
    timeinfo = localtime(&epoch);
    std::ostringstream oss;
    oss << std::put_time(timeinfo, "%Y-%m-%d %H:%M:%S");
    return oss.str();
  }

  std::string encrypt(const std::string& plaintext, const std::string& publicKeyString){
    CryptoPP::RSA::PublicKey publicKey;
    CryptoPP::StringSource publicKeySource(publicKeyString, true);
    publicKey.Load(publicKeySource);

    std::string ciphertext;
    CryptoPP::RSAES_OAEP_SHA_Encryptor encryptor(publicKey);
    CryptoPP::AutoSeededRandomPool rng;

    CryptoPP::StringSource(plaintext, true,
                           new CryptoPP::PK_EncryptorFilter(rng, encryptor,
                           new CryptoPP::StringSink(ciphertext)));

    return ciphertext;
  }

  std::string decrypt(const std::string& ciphertext, const std::string& privateKeyString){
    CryptoPP::RSA::PrivateKey privateKey;
    CryptoPP::StringSource privateKeySource(privateKeyString, true);
    privateKey.Load(privateKeySource);

    std::string decryptedtext;
    CryptoPP::RSAES_OAEP_SHA_Decryptor decryptor(privateKey);
    CryptoPP::AutoSeededRandomPool rng;

    CryptoPP::StringSource(ciphertext, true,
                           new CryptoPP::PK_DecryptorFilter(rng, decryptor,
                           new CryptoPP::StringSink(decryptedtext)));

    return decryptedtext;
  }

  std::string removeRightSpaces(std::string &input){
    size_t pos = input.find('\n');
    while (pos != std::string::npos){
      input.erase(pos, 1);
      pos = input.find('\n', pos);
    }
    return input;
  }

  void centralLicensing(void *np){
    if (Controller::mode == "OFFLINE"){return;}
    int counter = 0;
    while (Controller::conf.is_active){
      std::map<std::string,std::string> responseMap;
      std::string payload = Controller::makePayload();

      try {
        responseMap = apiChecker (payload, licenseApi);
      }catch (...){
        Controller::Log("LICENSE", "[ERROR] License server unreachable! Retrying...");
        if (payload.size() == 0){ Controller::Log("LICENSE", "[ERROR] Inaccurate payload!"); }
        counter += Controller::expiryCheckInterval;
        if (counter > Controller::failureIgnoreIntervel){
          Controller::Log("LICENSE", "[INFO] License expired! Exiting...");
          Controller::conf.is_active = false;
        }
      }

      if (responseMap["validation"] == "\"Failure\""){
        Controller::Log("LICENSE", "[INFO] License expired! Exiting...");
        Controller::conf.is_active = false;
        continue;
      }else{
        if (responseMap["validation"] == "\"Success\""){
          counter = 0;
          Controller::Log("LICENSE", "[INFO] Last server check at " + responseMap["created_at"]);
        }
      }
      /* Simulate segmented sleep for avoiding unjoinability */
      Controller::sleepInSteps(Controller::expiryCheckInterval);
    }
  }

  std::string exec(const std::string& cmd){
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe){
      INFO_MSG("Pipe open failed");
      return "";
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get())){result += buffer.data();}
    return result;
  }

  std::map<std::string,std::string> jsonParsor(std::string &input){
    nlohmann::json json = nlohmann::json::parse(input);
    std::map<std::string,std::string> response;

    if (json.is_object()){
      for (const auto& [key, value] : json.items()){
        if (value.is_number()){
          response[key] = std::to_string(value.get<int>());
        }else{
          response[key] = value.dump();
        }
      }
    }
    return response;
  }

  std::map<std::string,std::string> apiChecker(std::string &payload, std::string &licenseApi){
    std::map<std::string,std::string> responseMap;
    std::stringstream commandStream;
    commandStream << "curl -s -X POST -d '" << payload << "' " << licenseApi;
    std::string response = exec(commandStream.str());
    responseMap = Controller::jsonParsor(response);
    std::string resEncData = responseMap["result"];
    response = Controller::decrypt(
      Controller::base64Decode(resEncData),
      Controller::base64Decode(centralKeys["privateKey"])
    );
    responseMap = Controller::jsonParsor(response);
    expiryCheckInterval = std::stoi(responseMap["expiry_check_interval"].substr(1,responseMap["expiry_check_interval"].size()-2));
    failureIgnoreIntervel = std::stoi(responseMap["failure_ignore_interval"].substr(1,responseMap["failure_ignore_interval"].size()-2));
    Controller::maximumCameras = std::stoi(responseMap["number_of_cameras"].substr(1,responseMap["number_of_cameras"].size()-2));
    Controller::centralExpiry = Controller::displayTime(responseMap["expiry_at"].substr(1,responseMap["expiry_at"].size()-2));
    Controller::hardwareId = (responseMap["hardware_id"] == "null" ? "" : (responseMap["hardware_id"].substr(1,responseMap["hardware_id"].size()-2)));
    Controller::organization = responseMap["name"].substr(1,responseMap["name"].size()-2);
    return responseMap;
  }

  std::string makePayload(void){
    std::string curTime = Controller::epchToStr(Util::epoch());
    std::string id = Controller::uuid;
    int onboardedStreams = Controller::Storage["streams"].size();
    std::string parameters = std::string(std::string("{'expiry_at':'") + std::string(curTime) +
                             std::string("','number_of_cameras':") + std::to_string(onboardedStreams) +
                             std::string(",'hw_id':'") + std::string(Controller::hardwareId) +
                             std::string("','created_at':'") + std::string(curTime) + std::string("'}"));
    if (Controller::debugCentral){
      INSANE_MSG("Payload Parameters : %s", parameters.c_str());
    }
    try {
      parameters = Controller::encrypt(parameters, Controller::base64Decode(Controller::centralKeys["publicKey"]));
    }
    catch (CryptoPP::Exception& e){
      INSANE_MSG("Made wrong payload");
      return "";
    }
    std::string encData = Controller::base64Encode(parameters);
    encData = Controller::removeRightSpaces(encData);
    nlohmann::json jsonTemp;
    jsonTemp["id"] = id;
    jsonTemp["payload"] = encData;
    return jsonTemp.dump();
  }

  void online(void){
    Controller::uuid = std::string(getenv("UUID"));
    Controller::centralKeys["privateKey"] = std::string(getenv("PRIVATEKEY"));
    Controller::centralKeys["publicKey"] = std::string(getenv("PUBLICKEY"));

    try {Controller::licenseApi = std::string(getenv("URL"));}
    catch (const std::exception &ex){
      if (Controller::debugCentral){FAIL_MSG("[RTMPServer] Central licensing API %s", Controller::licenseApi);}
    }

    /** if DEBUG set in ENV*/
    if (Controller::debugCentral){
      HIGH_MSG("Inputs - {UUID : %s}, {PrivateKey : %s}, {PublicKey : %s}, {HardwareId : %s}, {API : %s}",
                Controller::uuid.c_str(), Controller::centralKeys["privateKey"].c_str(), Controller::centralKeys["publicKey"].c_str(),
                Controller::hardwareId.c_str(), Controller::licenseApi.c_str());
    }

    /**
     * Initial check of License
     */
    std::map<std::string,std::string> responseMap;
    std::string payload = Controller::makePayload();
    if (Controller::debugCentral){
      responseMap = Controller::apiChecker (payload, Controller::licenseApi);
      std::string responseString = "";
      for (auto &it:responseMap){responseString += "{" + it.first + " : " + it.second + "}, ";}
      HIGH_MSG("[LICENSE] Payload : %s", payload.c_str());
      HIGH_MSG("[LICENSE] Response : %s", responseString.c_str());
    }

    try {
      responseMap = Controller::apiChecker (payload, Controller::licenseApi);
    }
    catch (...){
      Controller::Log("LICENSE", "[ERROR] License server unreachable! Retrying...");
      if (payload.size() == 0){Controller::Log("LICENSE", "[ERROR] Inaccurate payload!");}
      Controller::conf.is_active = false;
    }
    if (responseMap["validation"] == "\"Failure\"" || responseMap.size() == 0){
      Controller::Log("LICENSE", "[INFO] License expired! Exiting...");
      Controller::conf.is_active = false;
    }

    /** checking hardware id */
    if (Controller::hardwareId.size() > 0){
      if (!(Controller::checkHardwareId(Controller::hardwareId))){
        FAIL_MSG("[RTMPServer] [License] Hardware ID changed! exiting...");
        Controller::conf.is_active = false;
      }
    }
  }

  void offline(void){
    bool exit_ = false;
    // [RTMPServer] Checking license file
    if (Controller::checkLicenseFile()){
      Controller::conf.activate();
    }else{
      std::cerr << "[RTMPServer] [License] File is missing!\n";
      exit_ = true;
    }

    // [RTMPServer] Checking epiry of license
    if (Controller::expiryDate.size() > 0 && (Util::epoch() > (Controller::expiryDateInEpoch - Controller::gmtSec))){
      std::cerr << "[RTMPServer] [License] Expired!\n";
      exit_ = true;
    }

    // [RTMPServer] Checking hardwareId of license
    if ((Controller::hardwareId.size() > 0) && !(Controller::checkHardwareId(Controller::hardwareId))){
      std::cerr << "[RTMPServer]  [License] Hardware ID changed!\n";
      exit_ = true;
    }
    if (exit_){
      Controller::sleepInSteps(60);
      Controller::conf.is_active = false;
    }
  }
}