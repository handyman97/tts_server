//

#include "synth_gcloud.h"
#include "logger.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctype.h>
#include <fstream>

// grpc
#include <grpc++/grpc++.h>
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

// cloud_tts
#if 1
// cloud_tts v1
#include "google/cloud/texttospeech/v1/cloud_tts.grpc.pb.h"
using google::cloud::texttospeech::v1::AudioConfig;
using google::cloud::texttospeech::v1::AudioConfigDefaultTypeInternal;
using google::cloud::texttospeech::v1::AudioEncoding;
using google::cloud::texttospeech::v1::ListVoicesRequest;
using google::cloud::texttospeech::v1::ListVoicesRequestDefaultTypeInternal;
using google::cloud::texttospeech::v1::ListVoicesResponse;
using google::cloud::texttospeech::v1::ListVoicesResponseDefaultTypeInternal;
using google::cloud::texttospeech::v1::SsmlVoiceGender;
using google::cloud::texttospeech::v1::SynthesisInput;
using google::cloud::texttospeech::v1::SynthesisInputDefaultTypeInternal;
using google::cloud::texttospeech::v1::SynthesizeSpeechRequest;
using google::cloud::texttospeech::v1::SynthesizeSpeechRequestDefaultTypeInternal;
using google::cloud::texttospeech::v1::SynthesizeSpeechResponse;
using google::cloud::texttospeech::v1::SynthesizeSpeechResponseDefaultTypeInternal;
using google::cloud::texttospeech::v1::Voice;
using google::cloud::texttospeech::v1::VoiceDefaultTypeInternal;
using google::cloud::texttospeech::v1::VoiceSelectionParams;
using google::cloud::texttospeech::v1::VoiceSelectionParamsDefaultTypeInternal;
using google::cloud::texttospeech::v1::TextToSpeech;
#else
// cloud_tts v1beta1
#include "google/cloud/texttospeech/v1beta1/cloud_tts.grpc.pb.h"
using google::cloud::texttospeech::v1beta1::AudioConfig;
using google::cloud::texttospeech::v1beta1::AudioConfigDefaultTypeInternal;
using google::cloud::texttospeech::v1beta1::AudioEncoding;
using google::cloud::texttospeech::v1beta1::ListVoicesRequest;
using google::cloud::texttospeech::v1beta1::ListVoicesRequestDefaultTypeInternal;
using google::cloud::texttospeech::v1beta1::ListVoicesResponse;
using google::cloud::texttospeech::v1beta1::ListVoicesResponseDefaultTypeInternal;
using google::cloud::texttospeech::v1beta1::SynthesisInput;
using google::cloud::texttospeech::v1beta1::SynthesisInputDefaultTypeInternal;
using google::cloud::texttospeech::v1beta1::SynthesizeSpeechRequest;
using google::cloud::texttospeech::v1beta1::SynthesizeSpeechRequestDefaultTypeInternal;
using google::cloud::texttospeech::v1beta1::SynthesizeSpeechResponse;
using google::cloud::texttospeech::v1beta1::SynthesizeSpeechResponseDefaultTypeInternal;
using google::cloud::texttospeech::v1beta1::Voice;
using google::cloud::texttospeech::v1beta1::VoiceDefaultTypeInternal;
using google::cloud::texttospeech::v1beta1::VoiceSelectionParams;
using google::cloud::texttospeech::v1beta1::VoiceSelectionParamsDefaultTypeInternal;
using google::cloud::texttospeech::v1beta1::TextToSpeech;
#endif

#ifndef PREFIX
#define PREFIX "/usr/local"
#endif

// ctor
synth_gcloud::synth_gcloud (const nlohmann::json& spec)
{
    syslog (LOG_DEBUG, "[synth_gcloud] %s", spec.dump().c_str());

    // engine
    assert (spec.find ("engine") != spec.end ());
    const nlohmann::json eng = spec["engine"];
    assert (eng.is_string ());
    engine = eng;

    // languages
    assert (spec.find ("languages") != spec.end ());
    assert (spec["languages"].is_array ());
    const std::vector<std::string> langs = spec["languages"];
    for (const std::string lang : langs) languages.push_back (lang);

    // name
    if (spec.find("name") != spec.end())
        name = spec["name"];
    else
        name = engine;

    // api
    assert (spec.find ("api") != spec.end ());
    const nlohmann::json api = spec["api"];
    assert (api.is_string ());
    if (api.get<std::string>().compare("google::cloud::texttospeech::v1") != 0)
    {
        syslog (LOG_ERR, "[synth_gcloud] unknown api: %s", api.get<std::string>().c_str());
        return;
    }

    // host & port
    assert (spec.find ("host") != spec.end ());
    const nlohmann::json host = spec["host"];
    assert (host.is_string ());
    _host = host.get<std::string>();
    syslog (LOG_DEBUG, "[synth_gcloud] host=%s", _host.c_str());
    size_t pos = _host.find (':');
    if (pos != std::string::npos)
    {
        _port = _host.substr (pos + 1);
        _host = _host.substr (0, pos);
    }

    // credentials
    std::shared_ptr<grpc::ChannelCredentials> creds;
    if (spec.find ("credentials") != spec.end ())
    {
        const nlohmann::json creds_path = spec["credentials"];
        syslog (LOG_DEBUG, "[synth_gcloud] credentials=%s", creds_path.dump().c_str());
        if (creds_path.is_string())
        {
            //const char* path = creds_path.get<std::string>().c_str(); // this fails, why?
            const std::string str = creds_path.get<std::string>();
            const char* path = str.c_str();
            syslog (LOG_DEBUG, "[synth_gcloud] credentials=\"%s\"", path);
            if (!path || strlen(path) == 0)
            {
                syslog (LOG_ERR, "[synth_gcloud] invalid credentials: %s", path);
                return;
            }
            if (*path == '/')
            {
                syslog (LOG_DEBUG, "[synth_gcloud] credentials: %s", path);
                setenv ("GOOGLE_APPLICATION_CREDENTIALS", path, 1);
            }
            else
            {
                char abs_path[100];
                snprintf(abs_path, 100, "%s/share/tts_server/%s", PREFIX, path);
                syslog (LOG_DEBUG, "[synth_gcloud] credentials: %s", abs_path);
                setenv ("GOOGLE_APPLICATION_CREDENTIALS", abs_path, 1);
            }
            creds = grpc::GoogleDefaultCredentials(); // GOOGLE_APPLICATION_CREDENTIALS
        }
        else if (creds_path.is_null())
        {
            syslog (LOG_DEBUG, "[synth_gcloud] insecure channel created");
            creds = grpc::InsecureChannelCredentials ();
        }
    }
    else
        creds = grpc::InsecureChannelCredentials ();
    if (!creds)
    {
        syslog (LOG_ERR, "[synth_gcloud] invalid credentials");
        return;
    }
    syslog (LOG_DEBUG, "[synth_gcloud] credentials created");

    // channel
    std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(host, creds);
    if (!channel)
    {
        syslog (LOG_DEBUG, "[synth_gcloud] channel creation failure");
        return;
    }
    syslog (LOG_DEBUG, "[synth_gcloud] channel created: host=%s", host.dump().c_str());

    // stub
    _stub = TextToSpeech::NewStub (channel);
    if (!_stub)
    {
        syslog (LOG_ERR, "[synth_gcloud] stub creation failure");
        return;
    }
    syslog (LOG_DEBUG, "[synth_gcloud] stub created");

}

bool
synth_gcloud::synthesizable (const nlohmann::json& req) const
{
    syslog (LOG_DEBUG, "[synthesizable] %s", req.dump().c_str());

    // name
    std::string name_;
    if (req.find("synthesizer") != req.end()) name_ = req["synthesizer"];
    if (!name_.empty() && name_.compare(name)) return false;

    // host
    if (req.find ("host") != req.end ())
    {
        std::string host = req["host"];
        if (strncmp (host.c_str(), _host.c_str(), _host.length ())) return false;
        size_t pos = host.find (':');
        if (pos != std::string::npos && host.substr(pos + 1).compare(_port) != 0) return false;
    }

    // engine
    if (req.find ("engine") != req.end ())
    {
        const std::string engine = req["engine"];
        if (!engine_compliant (engine.c_str())) return false;
    }

    // language
    const std::string lang
        = (req.find ("language") != req.end ()) ? req["language"] : "en-US";
    if (!language_supported  (lang.c_str())) return false;

    return true;
}

int
synth_gcloud::synthesize (const nlohmann::json& req, const char* outfile)
{
    uint8_t* bytes;
    size_t len;
    int err = synthesize (req, bytes, len);
    if (err) return err;

    std::ofstream out (outfile, std::ios::out | std::ios::binary);
    out.write ((const char*)bytes, len);
    delete bytes;
    out.close ();

    return 0;
}


// 'bytes' are newly allocated in this function
int
synth_gcloud::synthesize (const nlohmann::json& req, uint8_t*& bytes, size_t& len)
{
    syslog (LOG_DEBUG, "[synthesize] %s", req.dump().c_str());

    if (!_stub)
    {
        syslog (LOG_ERR, "[synthesize] no stub attached");
        return -1;
    }

    // input: text, ssml
    std::string text;
    bool in_ssml = false;
    if (req.find("input") != req.end())
    {
        const nlohmann::json input = req["input"];
        if (req.find("input") != req.end())
            text = req["text"];
        else if (req.find("ssml") != req.end())
        {
            text = req["ssml"];
            in_ssml = true;
        }
    }
    if (text.empty() && req.find("text") != req.end())
    {
        text = req["text"];
        in_ssml = false;
    }
    if (text.empty())
    {
        syslog (LOG_ERR, "[synth_gcloud::synthesize] input text not specified");
        return -1;
    }

    // voice: language_code, name, ssml_gender
    std::string lang;
    std::string gender_str;
    std::string voicename;
    if (req.find("voice") != req.end())
    {
        const nlohmann::json voice = req["voice"];
        if (voice.is_string())
            voicename = voice.get<std::string>();
        else if (voice.is_object())
        {
            if (voice.find("languageCode") != voice.end())
                lang = voice["languageCode"];
            if (voice.find("name") != voice.end())
                voicename = voice["name"];
            if (voice.find("ssmlGender") != voice.end())
                gender_str = voice["ssmlGender"];
        }
    }
    if (lang.empty())
        lang = (req.find("language") != req.end()) ? req["language"].get<std::string>() : "en-US";
    if (lang.length() < 2)
        lang = "en-US";
    else if (lang.length() == 2 || lang[2] != '-')
    {
        if (!lang.compare(0, 2, "ja"))
            lang = "ja-JP";
        else 
            lang = "en-US";
    }
    if (gender_str.empty())
        gender_str = (req.find("gender") != req.end()) ? req["gender"].get<std::string>() : "FEMALE";
    SsmlVoiceGender gender = SsmlVoiceGender::SSML_VOICE_GENDER_UNSPECIFIED;
    switch (toupper(gender_str[0]))
    {
    case 'F': gender = SsmlVoiceGender::FEMALE; break;
    case 'M': gender = SsmlVoiceGender::MALE; break;
    case 'N': gender = SsmlVoiceGender::NEUTRAL; break;
    }

    // audio_config: audio_encoding, speaking_rate, pitch, volume_gain_db, sample_rate-hertz, effect_profile_id
    AudioEncoding encoding = AudioEncoding::LINEAR16;
    if (req.find("audioConfig") != req.end())
    {
        // not configurable, for now
    }

    // request
    SynthesizeSpeechRequest request;

    // input
    SynthesisInput input;
    if (in_ssml) input.set_ssml (text); else input.set_text (text);
    //request.set_allocated_input (&input);
    SynthesisInput* allocated_input = input.New ();
    allocated_input->CopyFrom (input);
    request.set_allocated_input (allocated_input);

    // voice: langauge & gender
    VoiceSelectionParams voice;
    voice.set_language_code (lang);
    voice.set_ssml_gender (gender);
    VoiceSelectionParams* allocated_voice = voice.New ();
    allocated_voice->CopyFrom (voice);
    request.set_allocated_voice (allocated_voice);

    // audio_config: encoding
    AudioConfig config;
    config.set_audio_encoding (encoding);
    AudioConfig* allocated_config = config.New ();
    allocated_config->CopyFrom (config);
    request.set_allocated_audio_config (allocated_config);

    // gRPC call
    ClientContext context;
    SynthesizeSpeechResponse resp;
    Status status = _stub->SynthesizeSpeech (&context, request, &resp);
    // enum grpc::StatusCode (/usr/include/grpcpp/impl/codegen/status_code_enum.h)
    if (!status.ok ())
    {
        syslog (LOG_ERR, "[synth_gcloud::synthesize] failure in SynthesizeSPeech: status=%d (see \"status_code_enum.h\")",
                (int)status.error_code());
        return -1;
    }

    const std::string* content = &resp.audio_content ();
    len = content->size();
    //len = content->capacity();
    bytes = new uint8_t[len];
    memcpy (bytes, content->c_str(), len);
    syslog (LOG_ERR, "[synth_gcloud::synthesize] wave data generated: size=%d capacity=%d", content->size(), content->capacity());

    return 0;
}
