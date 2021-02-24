//


#include <festival/festival.h>
#include "synth_festival.h"
#include "logger.h"

#include <fstream>
#include <iostream>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <syslog.h>
#include <ctime>

synth_festival::synth_festival (const nlohmann::json& spec)
{
    if (spec.find("name") != spec.end())
        name = spec["name"];
    else
        name = "festival";

    engine = "festival";

    languages.push_back ("en");
    languages.push_back ("es");
    languages.push_back ("fr");

    //festival_initialize (1, 2000000);
    //festival_eval_command ("(gc)");
}

// str -> bytes
// ** space for 'bytes' is allocated in this function, and needs to be deallocated by the caller
static int
_synthesize (const char* str, uint8_t*& bytes, size_t& length)
{
    syslog (LOG_DEBUG, "[festival] text=\"%s\"", str);

    length = 0;
    bytes = nullptr;

    // synthesis using festival
    // [ref]
    // - http://www.cstr.ed.ac.uk/projects/festival/manual/festival_28.html
    // - http://www.festvox.org/docs/manual-2.4.0/festival_34.html
    // - https://www.cstr.ed.ac.uk/projects/festival/manual/festival_8.html#SEC23 -- scheme
    festival_initialize (1, 2000000);
    festival_eval_command ("(gc)");

    // text -> wave
    const EST_String text (str);
    EST_Wave wave;  // [ref] http://festvox.org/docs/speech_tools-2.4.0/classEST__Wave.html
    int ok = festival_text_to_wave (text, wave);
    if (!ok)
    {
        syslog (LOG_ERR, "[festival] failure in text->wave (%d)", ok);
        return -1;
    }
    festival_wait_for_spooler ();

    // wave -> bytes
    const int nsample = wave.num_samples ();
    const int size = nsample * 2 + 44; // wave header: 44bytes
    bytes = (uint8_t*)malloc (size);
    FILE* out = fmemopen (bytes, size, "w");
    EST_write_status status = wave.save (out, "riff");
    length = ftell (out);
    assert (length > 0);
    fclose (out);

    syslog (LOG_INFO,
            "[festival] wave: "
            "(nsample=%d, nchan=%d, rate=%d, type=%s, "
            "len=%d, result=%d)\n",
            wave.num_samples (), wave.num_channels (), wave.sample_rate (), wave.sample_type ().str (),
            length, (int)status);

    if (status != write_ok)
    {
        syslog (LOG_ERR, "failure in wave->bytes (%d)", (int)status);
        return -1;
    }
    //std::cerr << "  size:\t\t" << len << "\n";

    // response
    //response->set_audio_content (bytes, len);
    return 0;
}

//
int
synth_festival::synthesize (const nlohmann::json& req, uint8_t*& bytes, size_t& len)
{
    syslog (LOG_DEBUG, "[synthesize] %s", req.dump().c_str());

    // req -> text
    if (req.find("text") == req.end())
    {
        syslog (LOG_ERR, "no text found");
        return -1;
    }
    const std::string text = req["text"];
    const std::string lang = (req.find("language") != req.end()) ? (req["language"]) : "english";

    // text -> bytes
    //uint8_t* bytes = nullptr;
    //int len = 0;
    int err = _synthesize (text.c_str(), bytes, len);
    if (err) return err; // something went wrong
    syslog (LOG_DEBUG, "[synthesize] len=%d", len);
    if (len == 0) return -1;
    if (!bytes)
    {
        syslog (LOG_ERR, "[synthesize] no audio data generated");
        return -1;
    }

    return 0;
}

int
synth_festival::synthesize (const nlohmann::json& req, const char* outfile)
{
    syslog (LOG_DEBUG, "[synthesize] %s", req.dump().c_str());

    // req -> bytes
    //syslog (LOG_DEBUG, "[synthesize] write %dB", length);
    uint8_t* bytes = nullptr;
    size_t len = 0;
    int err = synthesize (req, bytes, len);
    if (err)
    {
        syslog (LOG_ERR, "[synth_festival::synthesize] synthesis failed");
        if (bytes) delete bytes;
        return err;
    }

    // bytes -> outfile
    std::ofstream out (outfile, std::ios::out | std::ios::binary);
    out.write ((const char*)bytes, len);
    out.close ();

    delete bytes;

    return (err);
}

//
bool
synth_festival::synthesizable (const nlohmann::json& req) const
{
    syslog (LOG_DEBUG, "[synthesizable] %s", req.dump().c_str());

    // engine
    if (req.find ("engine") != req.end ())
    {
        const std::string engine = req["engine"];
        if (!engine_compliant (engine.c_str())) return false;
    }

    // language
    const std::string lang
        = (req.find ("language") != req.end ()) ? req["language"] : "english";
    if (!language_supported  (lang.c_str())) return false;

    return true;
}
