#include "output_http.h"

namespace Mist{
  class OutJPG : public HTTPOutput{
  public:
    OutJPG(Socket::Connection &conn);
    static void init(Util::Config *cfg);
    void onHTTP();
    bool isReadyForPlay();

  private:
    void generate();
    void initialSeek();
    void NoFFMPEG(pid_t errorCode);
    std::string cachedir;
    uint64_t cachetime;
    bool HTTP;
    std::stringstream jpg_buffer;
  };
}// namespace Mist

typedef Mist::OutJPG mistOut;
