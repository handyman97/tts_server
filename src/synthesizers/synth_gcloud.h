//

#ifndef SYNTH_GCLOUD_H
#define SYNTH_GCLOUD_H

#include <nlohmann/json.hpp>
#include <grpc++/grpc++.h>
#if 1
#include "google/cloud/texttospeech/v1/cloud_tts.grpc.pb.h"
using google::cloud::texttospeech::v1::TextToSpeech;
#else
#include "google/cloud/texttospeech/v1beta1/cloud_tts.grpc.pb.h"
using google::cloud::texttospeech::v1beta1::TextToSpeech;
#endif

#include "synthesizer.h"

class synth_gcloud final: public synthesizer
{
public:
    synth_gcloud (const nlohmann::json& spec);

public:
    int synthesize (const nlohmann::json& req, const char* outfile) override;
    int synthesize (const nlohmann::json& req, uint8_t*& bytes, size_t& len) override;
    bool synthesizable (const nlohmann::json& req) const override;

private:
    std::string _host;
    std::string _port;
    std::unique_ptr<TextToSpeech::Stub> _stub;
};

#endif
