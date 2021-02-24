//

#ifndef TTS_SINK_SFTP_H
#define TTS_SINK_SFTP_H

#include "sink.h"
#include <nlohmann/json.hpp>

// sftp sink
class sink_sftp final : public sink
{
public:
    sink_sftp (const nlohmann::json& spec);

public:
    int consume (int fd) override;

private:
    std::string _address;	// ip addr
    int _port;			// tcp port (22)

    // authentication info
    std::string _username;
    std::string _publickey_path;
    std::string _privatekey_path;
    std::string _password;

    static bool _initialized;
};

#endif
