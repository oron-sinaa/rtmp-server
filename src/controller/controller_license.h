#include <iostream>
#include <stdio.h>
#include <chrono>
#include <utility>
#include <string>
#include <queue>
#include <thread>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <ctime>
#include <fstream>
#include <iostream>
#include <stdio.h>
#include <mist/json.h>
#include <crypto++/rsa.h>
#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include <cryptopp/filters.h>
#include <cryptopp/base64.h>
#include <cryptopp/hex.h>
#include <cryptopp/osrng.h>

#ifndef LICENSE_FILE_PATH
#define LICENSE_FILE_PATH \
  (getenv("CONFIG_DIR") ? std::string(getenv("CONFIG_DIR"))+"/vision.lic" : std::string("/etc/rtmpserver/vision.lic"))
#endif
#ifndef ID_FILE_PATH
#define ID_FILE_PATH \
  (getenv("CONFIG_DIR") ? std::string(getenv("CONFIG_DIR"))+"/id.txt" : std::string("/etc/rtmpserver/id.txt"))
#endif
#ifndef DEFAULT_LICENSE_INTERVAL
#define DEFAULT_LICENSE_INTERVAL 10000
#endif

namespace Controller {
    extern int maximumCameras;                            ///< Total number of cameras as per as license
    extern std::string expiryDate;                        ///< ExpiryDate of license
    extern std::string hardwareId;                        ///< System hardware id
    extern std::string token;
    extern JSON::Value data;
    extern std::time_t expiryDateInEpoch;
    extern std::string organization;
    extern bool hardwareIdCheck;
    extern std::string key;
    extern std::string timezone;
    extern int gmtSec;
    extern std::string mode;
    extern std::map<std::string,std::string> centralKeys;
    extern std::string licenseApi;
    extern int expiryCheckInterval;
    extern int failureIgnoreIntervel;
    extern std::string centralExpiry;
    extern bool debugCentral;

    /**
     * @brief a Base64Decoder, attaches a StringSink to store the decoded output,
     * and decode Base64-encoded input data using the \b Put function.
     * Finally, it signals the end of the message through \b MessageEnd().
     */
    std::string base64Decode(const std::string& encodedKey);

    /**
     * @brief a Base64Encoder, attaches a StringSink to store the encoded output,
     * and encode the input data using the \b Put function.
     */
    std::string base64Encode(const std::string& input);

    /**
     * \brief checking the license file in config
     * \b vision.lic must have to present in config to
     * start the application
     */
    bool checkLicenseFile(void);

    /**
     * decrypt the @param ciphertext present in \b vision.lic
     * with the help of @param privateKeyString
     */
    void decodeLicense(const std::string& privateKeyString, const std::string& ciphertext);

    /**
     * parse the data from JSON form .
     */
    void dataParser(JSON::Value &data);

    /**
     * Separate thread to continuously check if the expiry time has been reached.
     * If the expiry time has arrived, it gracefully exits the application.
     */
    void checkExpiryDate(void *np);

    /** convert the "YYYY-MM-DD hh:mm:ss TZ" to epochTime*/
    std::time_t stringToEpochTime(const std::string &dateTimeStr);

    /**
     * comparing the hardwareid as per as mentioned on license
     */
    bool checkHardwareId(const std::string &id);

    /**
     * convert the timezone for vision panel
     */
    std::string displayTime(const std::string& inputDateTime);

    /** convert the epoch to stringTime @brief (Tue 27 Jun YYYY, hh:mm:ss) */
    std::string epochToString (std::time_t epoch);

    /** checking the hardwareId of machine */
    bool checkSerial(const std::string &ser);

    /**
     * Encrypt the @param plaintext with @param publicKeyString and return
     * encrypted text or ciphertext.
     * \brief load the publicKey instance from publicKeyString and from the \b encryptor
     * instance convert the plaintext to ciphertext.
     */
    std::string encrypt(const std::string& plaintext, const std::string& publicKeyString);

    /**
     * Decrypt the @param ciphertext with @param privateKeyString and return
     * decrypted text or original data.
     * \brief load the privateKey instance from privateKeyString and from the \b decryptor
     * instance convert the ciphertext to original text and store in decryptedtext.
     */
    std::string decrypt(const std::string& ciphertext, const std::string& privateKeyString);

    /**
     * remove rightspaces from the encrypted data
     * @param input is encrypted data
     */
    std::string removeRightSpaces(std::string &input);

    /**
     * @brief function will hit the API and store response in map
     * @param payload : {'id':'XXXXXXX-XXXX','payload':' \b data '}
     * @param licenseApi : API URL
     * @return map of response
     */
    std::map<std::string,std::string> apiChecker(std::string &payload, std::string &licenseApi);

    /**
     * @brief make payload in json format and return in string after dump
     * i.e. {'id':'XXXXXXX-XXXX','payload':' \b data '}
     * \b data - {'expiry_at':'','number_of_cameras':'',''created_at':''}
     * after convert this \b data to encrypted format send to payload in POST request
     */
    std::string makePayload(void);

    /** convert the epoch to stringTime (YYYY-MM-DD hh:mm:ss) */
    std::string epchToStr(std::time_t epoch);

    /**
     * @brief parse the json input in string to map with key,value
     */
    std::map<std::string,std::string> jsonParsor (std::string &input);

    /**
     * seperate thread for central licensing
     * @brief hitting the api at regular intervals (e.g., every 30 minutes)
     */
    void centralLicensing(void *np);

    /** @brief initial check for central licensing */
    void online(void);

    /** @brief intial check for offline licensing */
    void offline(void);

}