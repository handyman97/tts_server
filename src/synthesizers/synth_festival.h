//

#ifndef SYNTH_FESTIVAL_H
#define SYNTH_FESTIVAL_H

#include <list>
#include <nlohmann/json.hpp>

#include "synthesizer.h"

//
class synth_festival: public synthesizer
{
public:
    synth_festival (const nlohmann::json& spec);

public:
    int synthesize (const nlohmann::json& req, const char* outfile) override;
    int synthesize (const nlohmann::json& req, uint8_t*& bytes, size_t& len) override;
    bool synthesizable (const nlohmann::json& req) const override;
};

#endif
