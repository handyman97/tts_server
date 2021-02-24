//

#ifndef SYNTH_ESPEAK_H
#define SYNTH_ESPEAK_H

#include <list>
#include <nlohmann/json.hpp>

#include "synthesizer.h"

//
class synth_espeak final : public synthesizer
{
public:
    synth_espeak (const nlohmann::json& spec);

public:
    int synthesize (const nlohmann::json& req, const char* outfile) override;
    int synthesize (const nlohmann::json& req, uint8_t*& bytes, size_t& len) override;
    bool synthesizable (const nlohmann::json& req) const override;
};

#endif
