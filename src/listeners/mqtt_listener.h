#ifndef TTS_MQTT_LISTENER_H
#define TTS_MQTT_LISTENER_H

#include "listener.h"

#include <mosquitto.h>
#include <nlohmann/json.hpp>
#include <cassert>

//
class mqtt_listener final : public listener
{
public:
    mqtt_listener ();

public:
    int setup (const nlohmann::json&) override;
    int run (void) override;
    void quit (void) override;

    std::list<std::string>& topics() { return _topics; }

private:
    struct mosquitto* _mosq;
    std::string _address;
    int _port;
    std::list<std::string> _topics;

    static bool _initialized;
};

#endif
