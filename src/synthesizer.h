//

#ifndef TTS_SYNTHESIZER_H
#define TTS_SYNTHESIZER_H

#include <cstdint>
#include <list>
#include <nlohmann/json.hpp>

// tts
class synthesizer
{
public:
    virtual int synthesize (const nlohmann::json& req, const char* outfile) = 0;
    virtual int synthesize (const nlohmann::json& req, uint8_t*& bytes, size_t& len) = 0;
    virtual bool synthesizable (const nlohmann::json& req) const = 0;

public:
    std::string name;
    std::string engine;
    std::list<std::string> languages;

public:
    bool engine_compliant (const char*) const;
    bool language_supported (const char*) const;
};

#endif
