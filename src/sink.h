//

#ifndef TTS_SINK_H
#define TTS_SINK_H

#include <cstdint>
#include <cstddef>
#include <string>

// sink of speech data stream
class sink
{
public:
    virtual int consume (int fd) = 0;
    virtual int consume (const char* wavfile);
    virtual int consume (const uint8_t* wav, size_t len);

public:
    std::string name;
};

#endif
