//

#ifndef TTS_LISTENER_H
#define TTS_LISTENER_H

#include <nlohmann/json.hpp>

//
class listener
{
public:
    virtual int setup (const nlohmann::json& conf) = 0;
    virtual int run (void) = 0;
    virtual void quit (void) = 0;

public:
    std::string name;
};

#endif
