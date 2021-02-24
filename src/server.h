//

#ifndef TTS_SERVER_H
#define TTS_SERVER_H

#include <nlohmann/json.hpp>

#include "listener.h"
#include "synthesizer.h"
#include "sink.h"

namespace tts_server {

// service
int setup (const nlohmann::json& conf);
int setup (const char* conf_file = nullptr);

int run (void);
void quit (int sig);

// helper (for listners)
int req_enqueue (nlohmann::json*);

}

#endif
