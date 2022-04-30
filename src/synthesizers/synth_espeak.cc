// license: GPL v3
// note: a part of the code in this file reuses somebody else's which is licensed under GPL v3.

#include "synth_espeak.h"
#include "logger.h"

#include <nlohmann/json.hpp>
#include <espeak-ng/espeak_ng.h>
#include <espeak-ng/speak_lib.h>
#include <espeak-ng/encoding.h>

#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <ctype.h>
#include <fstream>
#include <iostream>
#include <syslog.h>
#include <thread>

using json = nlohmann::json;

//
static int _init ();
static int _synthesize (const nlohmann::json& req, const char* outfile);

// ctor
synth_espeak::synth_espeak (const nlohmann::json& spec)
{
    if (spec.find("name") != spec.end())
        name = spec["name"];
    else
        name = "espeak";

    engine = "espeak";

    languages.push_back ("en");
    languages.push_back ("es");
    languages.push_back ("fr");

    //espeak_initialize (1, 2000000);
    //espeak_eval_command ("(gc)");
    _init ();
}

//
int
synth_espeak::synthesize (const nlohmann::json& req, uint8_t*& bytes, size_t& len)
{
    assert (0);
    return 0;
}

int
synth_espeak::synthesize (const nlohmann::json& req, const char* outfile)
{
    syslog (LOG_DEBUG, "[synthesize] %s", req.dump().c_str());

    int err = _synthesize (req, outfile);

    return err;
}

//
bool
synth_espeak::synthesizable (const nlohmann::json& req) const
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

// helpers
// based on parts of https://github.com/espeak-ng/espeak-ng/blob/master/src/espeak-ng.c 

// Copyright (C) 2006 to 2013 by Jonathan Duddington
// email: jonsd@users.sourceforge.net
// Copyright (C) 2015-2016 Reece H. Dunn
//
// license: GPL v3
//

static void
Write4Bytes(FILE *f, int value)
{
	// Write 4 bytes to a file, least significant first
	int ix;

	for (ix = 0; ix < 4; ix++) {
		fputc(value & 0xff, f);
		value = value >> 8;
	}
}

static FILE*
OpenWavFile (const char *path, int rate)
{
    if (path == NULL) return nullptr;
    //while (isspace(*path)) path++;

    FILE* f_wavfile = fopen(path, "wb");
    if (!f_wavfile)
    {
        syslog (LOG_ERR, "[OpenWavFile] failure in fopen(\"%s\")", path);
        return nullptr;
    }

    static unsigned char wave_hdr[44] = {
		'R', 'I', 'F', 'F', 0x24, 0xf0, 0xff, 0x7f, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ',
		0x10, 0, 0, 0, 1, 0, 1, 0,  9, 0x3d, 0, 0, 0x12, 0x7a, 0, 0,
		2, 0, 0x10, 0, 'd', 'a', 't', 'a',  0x00, 0xf0, 0xff, 0x7f
    };

    fwrite(wave_hdr, 1, 24, f_wavfile);
    Write4Bytes(f_wavfile, rate);
    Write4Bytes(f_wavfile, rate * 2);
    fwrite(&wave_hdr[32], 1, 12, f_wavfile);

    return f_wavfile;
}

static void
CloseWavFile (FILE* f_wavfile)
{
    if ((f_wavfile == NULL) || (f_wavfile == stdout))
        return;

    fflush(f_wavfile);

    unsigned int pos = ftell(f_wavfile);

    if (fseek(f_wavfile, 4, SEEK_SET) != -1)
        Write4Bytes(f_wavfile, pos - 8);

    if (fseek(f_wavfile, 40, SEEK_SET) != -1)
        Write4Bytes(f_wavfile, pos - 44);

    fclose(f_wavfile);
}

FILE* f_wavfile = nullptr;

static int
SynthCallback(short *wav, int numsamples, espeak_EVENT *events)
{
    syslog (LOG_DEBUG, "[SynthCallback] #sample=%d", numsamples);

    if (!wav) return 0;

    /*
      while (events->type != 0) {
      if (events->type == espeakEVENT_SAMPLERATE) {
      samplerate = events->id.number;
      samples_split = samples_split_seconds * samplerate;
      } else if (events->type == espeakEVENT_SENTENCE) {
      // start a new WAV file when the limit is reached, at this sentence boundary
      if ((samples_split > 0) && (samples_total > samples_split)) {
      CloseWavFile();
      samples_total = 0;
      wavefile_count++;
      }
      }
      events++;
      }

      if (f_wavfile == NULL) {
      if (samples_split > 0) {
      char fname[210];
      sprintf(fname, "%s_%.2d%s", wavefile, wavefile_count+1, filetype);
      if (OpenWavFile(fname, samplerate) != 0)
      return 1;
      } else if (OpenWavFile(wavefile, samplerate) != 0)
      return 1;
      }
    */

    // wav -> f_wavefile
    //samples_total += numsamples;
    if (!f_wavfile) return 1;

    if (numsamples > 0)
        fwrite(wav, numsamples*2, 1, f_wavfile);

    return 0;
}

static int
_init (void)
{
    espeak_ng_InitializePath (nullptr);
    espeak_ng_ERROR_CONTEXT context = NULL;
    espeak_ng_STATUS result = espeak_ng_Initialize(&context);
    if (result != ENS_OK)
    {
        //espeak_ng_PrintStatusCodeMessage(result, stderr, context);
        char error[512];
        espeak_ng_GetStatusCodeMessage(result, error, sizeof(error));
        syslog (LOG_DEBUG, "[espeak] error in espeak_ng_Initialize (%d): %s", result, error);
        espeak_ng_ClearErrorContext(&context);
        return -1;
    }
    return 0;
}


// text -> wave
// ** space for 'bytes' is allocated in this function, and needs to be deallocated by the caller
// cf. https://github.com/espeak-ng/espeak-ng/blob/master/src/espeak-ng.c
static int
_synthesize (const json& req, const char* outfile)
{
    //syslog (LOG_DEBUG, "[espeak] text=\"%s\"", text);

    // req -> text
    if (req.find("text") == req.end())
    {
        syslog (LOG_ERR, "[synthesize] no text found");
        return -1;
    }
    const std::string text = req["text"];

    // lang/gender -> voicename
    const char* lang = "en";
    if (req.find("language") != req.end()) lang = req["language"].get<std::string>().substr(0, 2).c_str();
    bool male = true;
    if (req.find("gender") != req.end()) male = (tolower (*req["gender"].get<std::string>().c_str()) == 'm');
    syslog (LOG_DEBUG, "[synthesize] text=\"%s\" language=%s gender=%s", text.c_str(), lang, (male ? "male" : "female"));
    const char* voicename = nullptr;
    if (!strcmp (lang, "en"))
        voicename = male ? "gmw/en-US" : "mb/mb-us1";
    else if (!strcmp (lang, "ja"))
        voicename = "jpx/ja";
    else
    {
        syslog (LOG_ERR, "[synthesize] unknown language: %s", lang);
        return -1;
    }

    // ----------------------------------------
    // init
    // ----------------------------------------

    espeak_ng_STATUS result = ENS_OK;

    /*
    espeak_ng_InitializePath (nullptr);
    espeak_ng_ERROR_CONTEXT context = NULL;
    espeak_ng_STATUS result = espeak_ng_Initialize(&context);
    if (result != ENS_OK)
    {
        //espeak_ng_PrintStatusCodeMessage(result, stderr, context);
        char error[512];
        espeak_ng_GetStatusCodeMessage(result, error, sizeof(error));
        syslog (LOG_DEBUG, "[espeak] error in espeak_ng_Initialize (%d): %s", result, error);
        espeak_ng_ClearErrorContext(&context);
        return -1;
    }
    */

    // ----------------------------------------
    // setups
    // ----------------------------------------

    // espeak_ng_InitializeOutput
    // espeak_SetSynthCallback
    result = espeak_ng_InitializeOutput(ENOUTPUT_MODE_SYNCHRONOUS, 0, NULL);

    int samplerate = espeak_ng_GetSampleRate();
    f_wavfile = OpenWavFile (outfile, samplerate);
    espeak_SetSynthCallback(SynthCallback);

    if (result != ENS_OK)
    {
        //espeak_ng_PrintStatusCodeMessage(result, stderr, NULL);
        char error[512];
        espeak_ng_GetStatusCodeMessage(result, error, sizeof(error));
        syslog (LOG_DEBUG, "[espeak] error in espeak_SetSynthCallback (%d): %s", result, error);
        //exit(EXIT_FAILURE);
        return -1;
    }

    // espeak_ng_SetVoiceByName
    if (!voicename) voicename = ESPEAKNG_DEFAULT_VOICE;	// ESPEAKNG_DEFAULT_VOICE = "en"
    result = espeak_ng_SetVoiceByName(voicename);

    if (result != ENS_OK)
    {
        char error[512];
        espeak_ng_GetStatusCodeMessage(result, error, sizeof(error));
        syslog (LOG_ERR, "[espeak] error in espeak_SetVoiceByName (err=%d, voice=\"%s\"): %s", result, voicename, error);

        // fallback
        espeak_VOICE voice_select;
        memset(&voice_select, 0, sizeof(voice_select));
        voice_select.languages = voicename;
        result = espeak_ng_SetVoiceByProperties(&voice_select);
        if (result != ENS_OK) {
            //espeak_ng_PrintStatusCodeMessage(result, stderr, NULL);
            //exit(EXIT_FAILURE);
            return -1;
        }
    }

    //
    espeak_SetParameter (espeakRATE, 130, 0);			// default:175
    //espeak_SetParameter (espeakVOLUME, volume, 0);
    espeak_SetParameter(espeakPITCH, 50, 0);			// default:50
    //espeak_SetParameter(espeakCAPITALS, option_capitals, 0);
    //espeak_SetParameter(espeakPUNCTUATION, option_punctuation, 0);
    //espeak_SetParameter(espeakWORDGAP, wordgap, 0);
    //espeak_SetParameter(espeakLINELENGTH, option_linelength, 0);
    //espeak_SetPunctuationList(option_punctlist);

    // ----------------------------------------
    // synth & sync
    // ----------------------------------------

    // espeak_Synth
    // espeak_ng_Synchronize
    int synth_flags = espeakCHARS_AUTO | espeakPHONEMES | espeakENDPAUSE;
    const char* str = text.c_str();
    int size = strlen (str);
    espeak_Synth (str, size+1, 0, POS_CHARACTER, 0, synth_flags, NULL, NULL);

    //result = espeak_ng_Synchronize();
    result = ENS_OK;
    int count = 0;
    while (espeak_IsPlaying ())
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (100));
        if (++count == 50)
        {
            // timed out
            result = espeak_ng_Cancel ();

            syslog (LOG_DEBUG, "[espeak] canceled (%d)", result);
            //result = espeak_ng_Synchronize();
            CloseWavFile (f_wavfile);
            return -1;
        }
    }

    if (result != ENS_OK)
    {
        char error[512];
        espeak_ng_GetStatusCodeMessage(result, error, sizeof(error));
        syslog (LOG_DEBUG, "[espeak] error in espeak_ng_Synchronize (%d): %s", result, error);

        //espeak_ng_PrintStatusCodeMessage(result, stderr, NULL);
        //exit(EXIT_FAILURE);
        return -1;
    }

    // ----------------------------------------
    // cleanup
    // ----------------------------------------

    CloseWavFile (f_wavfile);
    //espeak_ng_Terminate();

    return 0;
}
