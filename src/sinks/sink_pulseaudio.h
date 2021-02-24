//

#ifndef TTS_SINK_PULSEAUDIO_H
#define TTS_SINK_PULSEAUDIO_H

#include "sink.h"
#include <nlohmann/json.hpp>

// pulseaudio sink
class sink_pulseaudio final : public sink
{
public:
    sink_pulseaudio (const nlohmann::json& spec);

public:
    int consume (int fd) override;

private:
    std::string _address;	// ip addr
    //int _port;		// pulseaudio port
    std::string _device;	// pulseaudio device
};

#endif
