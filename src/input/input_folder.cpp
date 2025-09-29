#include <mist/defines.h>
#include <mist/stream.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "input_folder.h"

namespace Mist{
  inputFolder::inputFolder(Util::Config *cfg) : Input(cfg){
    capa["name"] = "Folder";
    capa["desc"] =
        "The folder input will make available all supported files in the given folder as streams "
        "under this stream name, in the format STREAMNAME+FILENAME. For example, if your stream is "
        "called 'files' and you have a file called 'movie.flv', you could access this file "
        "streamed as 'files+movie.flv'. This input does not support subdirectories. To support "
        "more complex libraries, look into the documentation for the STREAM_SOURCE trigger.";
    capa["source_match"] = "/*/";
    capa["source_file"] = "$source/$wildcard";
    capa["priority"] = 9;
    capa["morphic"] = 1;
  }

  int inputFolder::boot(int argc, char *argv[]){
    if (!config->parseArgs(argc, argv)){return 1;}
    if (config->getBool("json")){return Input::boot(argc, argv);}

    streamName = config->getString("streamname");
    if (streamName.find_first_of("+ ") == std::string::npos){
      FAIL_MSG("Folder input requires a + or space in the stream name.");
      return 1;
    }

    std::string folder = config->getString("input");
    if (folder[folder.size() - 1] != '/'){
      FAIL_MSG("Input path must end in a forward slash.");
      return 1;
    }

    std::string folder_noslash = folder.substr(0, folder.size() - 1);
    struct stat fileCheck;
    if (stat(folder_noslash.c_str(), &fileCheck) != 0 || !S_ISDIR(fileCheck.st_mode)){
      FAIL_MSG("Folder input requires a folder as input.");
      return 1;
    }

    std::string path = folder + streamName.substr(streamName.find_first_of("+ ") + 1);
    if (stat(path.c_str(), &fileCheck) != 0 || S_ISDIR(fileCheck.st_mode)){
      FAIL_MSG("File not found: %s", path.c_str());
      return 1;
    }

    Util::startInput(streamName, path, false);
    return 1;
  }

}// namespace Mist
