//

#include "sink_pulseaudio.h"
#include "logger.h"

#include <pulse/simple.h>
#include <pulse/error.h>

#include <cassert>
#include <fcntl.h>
#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

sink_pulseaudio::sink_pulseaudio (const nlohmann::json& spec)
{
    // host
    assert (spec.find ("host") != spec.end ());
    assert (spec["host"].is_string ());
    _address  = spec["host"];
    
    // name
    if (spec.find ("name") != spec.end () && spec["name"].is_string ())
        name = spec["name"];
    else
        name = _address;

    // device
    _device = (spec.find ("device") != spec.end () && spec["device"].is_string ()) ? spec["device"] : "";
}

int
sink_pulseaudio::consume (int fd)
{
    syslog (LOG_DEBUG, "[play] address=%s device=\"%s\"", _address.c_str(), _device.c_str());

    // WAV header (http://soundfile.sapp.org/doc/WaveFormat/)
    uint8_t hd[44];
    ssize_t n = read (fd, hd, 44);
    if (n != 44) return (-1);
    if (strncmp ((const char*)hd, "RIFF", 4) != 0) return (-1);
    if (strncmp ((const char*)hd + 8, "WAVE", 4) != 0) return (-1);
    uint16_t* fmt = (uint16_t*)(hd + 20);
    if (*fmt != 1) return (-1);
    uint8_t* nch = (uint8_t*)(hd + 22);
    uint16_t* rate = (uint16_t*)(hd + 24);
    uint16_t* bps = (uint16_t*)(hd + 34);
    if (*bps != 8 && *bps != 16) return (-1);
    const pa_sample_spec ss =
        {
         .format = (*bps == 8) ? PA_SAMPLE_U8 : PA_SAMPLE_S16LE,
         .rate = *rate,
         .channels = *nch
        };

    // call pa_simple_write
    int rslt, err;
    const char* name = nullptr;
    const char* dev = (_device.length () > 0) ? _device.c_str () : nullptr;
    const pa_channel_map* map = nullptr;
    const pa_buffer_attr* attr = nullptr;
    pa_simple* s = pa_simple_new (_address.c_str(), name, PA_STREAM_PLAYBACK, dev, "playback", &ss, map, attr, &err);
    if (!s)
    {
        syslog (LOG_ERR, "[consume] pa_simple_new (server=\"%s\") failed (%d)", _address.c_str(), err);
        return (-1);
    }
    assert (s);
    while (1)
    {
        const size_t max_len = 1024;
        uint8_t buff[max_len];
        ssize_t len = read (fd, buff, max_len);
        if (len <= 0) break;
        rslt = pa_simple_write (s, buff, len, &err);
        if (rslt < 0) goto abort;
    }

    rslt = pa_simple_drain (s, &err);
    if (rslt < 0) goto abort;
    assert (rslt == 0);

 abort:
    if (rslt < 0) syslog (LOG_ERR, "[consume] abort on error: %d", err);
    pa_simple_free (s);

    return (err);
}
