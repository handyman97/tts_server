//

#include "server.h"
#include "logger.h"
#include "listeners/mqtt_listener.h"
#include "synthesizers/synth_espeak.h"
#include "synthesizers/synth_festival.h"
#include "synthesizers/synth_gcloud.h"
#include "sinks/sink_pulseaudio.h"
#include "sinks/sink_sftp.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cassert>
#include <cctype>
#include <cinttypes>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <list>
#include <queue>
#include <regex>
#include <utility>

#include <functional>
#include <future>
#include <thread>
#include <condition_variable>

#include <libgen.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef PREFIX
#define PREFIX "/usr/local"
#endif

using json = nlohmann::json;

// --------------------------------------------------------------------------------
// listeners
// --------------------------------------------------------------------------------

std::list<listener*> _listeners;

//
static listener*
lstnr_add (const nlohmann::json& spec)
{
    syslog (LOG_NOTICE, "[lstnr_add] %s", spec.dump().c_str());

    // protocol
    const std::string proto ((spec.find ("protocol") != spec.end ()) ? spec["protocol"] : "");

    listener* l = nullptr;
    if (!proto.empty())
    {
        if (!proto.compare(0, 4, "mqtt"))
            l = new mqtt_listener ();
    }
    if (!l)
    {
        syslog (LOG_ERR, "[lstnr_add] no listener created: protocol=%s", proto.c_str());
        return nullptr;
    }

    assert (l);
    l->setup (spec);
    _listeners.push_back (l);

    return (l);
}

// --------------------------------------------------------------------------------
// synthesizers
// --------------------------------------------------------------------------------

std::list<synthesizer*> _synthesizers;

//static synthesizer* synth_add (const char* addr, const char* engine, std::list<const char*> langs);
//static synthesizer* synth_find (const nlohmann::json& req);

static synthesizer*
synth_add (const nlohmann::json& spec)
{
    syslog (LOG_NOTICE, "[synth_add] %s", spec.dump().c_str());

    // engine
    assert (spec.find ("engine") != spec.end ());
    const std::string engine = spec["engine"];
    // langs
    assert (spec.find ("languages") != spec.end ());
    assert (spec["languages"].is_array ());
    const std::vector<std::string> langs = spec["languages"];

    // host
    const std::string host = (spec.find ("host") != spec.end ()) ? spec["host"] : "";
    // api
    const std::string api = (spec.find ("api") != spec.end ()) ? spec["api"] : "";

    //
    synthesizer* synth = nullptr;
    if (api.empty())
    {
        // builtin synthesizers
        if (!engine.compare ("espeak"))
            synth = new synth_espeak (spec);
        else if (!engine.compare ("festival"))
            synth = new synth_festival (spec);
    }
    else
    {
        // google cloud tts
        if (!api.compare ("google::cloud::texttospeech::v1") && !host.empty())
            synth = new synth_gcloud (spec);
    }
    if (!synth)
    {
        syslog (LOG_ERR, "[synth_add] no synthesizer defined for %s", spec.dump().c_str());
        return nullptr;
    }

    assert (synth);
    _synthesizers.push_back (synth);

    return (synth);
}

static synthesizer*
synth_find (const nlohmann::json& req)
{
    //if (!req) return nullptr;
    syslog (LOG_DEBUG, "[synth_find] %s", req.dump().c_str());

    std::string name;
    if (req.find("synthesizer") != req.end()) name = req["synthesizer"];

    for (synthesizer* s : _synthesizers)
    {
        assert (s);
        //std::cerr << "lang=" << syn.language << " address=" << syn.address << "\n";
        if (!name.empty() && name.compare(s->name)) continue;
        if (!s->synthesizable (req)) continue;
        syslog (LOG_DEBUG, "[synth_find] found: engine=\"%s\"", s->engine.c_str());
        return (s);
    }

    syslog (LOG_ERR, "[synth_find] no synthesizer found");
    return nullptr;
}

// --------------------------------------------------------------------------------
// sinks
// --------------------------------------------------------------------------------

std::list<sink*> _sinks;

//
//static sink* sink_add (const char* addr, const char* dev);
//static sink* sink_find (const char* addr, const char* dev = nullptr);
//static const std::list<sink*> sink_select (const nlohmann::json& req);

static sink*
sink_add (const nlohmann::json& spec)
{
    syslog (LOG_NOTICE, "[sink_add] %s", spec.dump().c_str());

    // api
    const std::string api = (spec.find ("api") != spec.end ()) ? spec["api"] : "";

    sink* s = nullptr;
    if (!api.compare("pulseaudio"))
        s = new sink_pulseaudio (spec);
    else if (!api.compare("sftp"))
        s = new sink_sftp (spec);
    else
    {
        syslog (LOG_ERR, "[sink_add] no sink defined for %s", spec.dump().c_str());
        return nullptr;
    }

    assert (s);
    _sinks.push_back (s);

    return (s);
}

// audio sink
static sink*
sink_find (const std::string& name)
{
    syslog (LOG_DEBUG, "[sink_find] name=%s", name.c_str());

    for (sink*& s : _sinks)
    {
        if (!name.compare (s->name)) return s;
    }

    //std::list<const sink*> matched;
    return nullptr;
}

static const std::list<sink*>
sink_select (const nlohmann::json& req)
{
    syslog (LOG_DEBUG, "[sink_select] %s", req.dump().c_str());

    std::list<sink*> selected;

    // case: sinks specified
    if (req.find ("sinks") != req.end ())
    {
        const nlohmann::json seq = req["sinks"];
        if (!seq.is_array ())
        {
            syslog (LOG_ERR, "invalid sinks");
            return selected;
        }
        for (const std::string& name : seq)
        {
            sink* s = sink_find (name);
            if (!s) continue;
            selected.push_back (s);
        }
        return (selected);
    }

    // case: sinks not specified -- fallback
    for (sink* s : _sinks)
    {
        selected.push_back (s);
    }
    return (selected);
}

// --------------------------------------------------------------------------------
// request handling
// --------------------------------------------------------------------------------

// shared variables
std::queue<nlohmann::json*> g_requests;
std::mutex g_mutex;

static bool
req_empty ()
{
    return (g_requests.empty());
}

int
tts_server::req_enqueue (json* req)
{
    g_mutex.lock ();
    g_requests.push (req);
    g_mutex.unlock ();
    return 0;
}

static json*
req_dequeue ()
{
    if (req_empty()) return nullptr;

    g_mutex.lock ();
    json* req = g_requests.front ();
    g_requests.pop ();
    g_mutex.unlock ();

    return req;
}

// --------------------------------------------------------------------------------
// thread pool
// --------------------------------------------------------------------------------

std::vector<std::thread*> g_workers; // pool of worker threads

typedef std::function<int(void)> task_t;
  // order b.w. tasks is not preserved in their execution by workers.
std::queue<task_t> g_taskq;
std::mutex g_taskq_mutex;
//std::condition_variable g_taskq_cv;

static int
task_enqueue (task_t task)
{
    {
        std::unique_lock<std::mutex> lock(g_taskq_mutex);
        g_taskq.push (task);
    }
    //g_taskq_cv.notify_one();  // allows the waiting worker to dequeue

    return (0);
}

static int
task_dequeue (task_t& task)
{
    {
        std::unique_lock<std::mutex> lock(g_taskq_mutex);
        if (g_taskq.empty ()) return -1;
        task = g_taskq.front();
        g_taskq.pop();
    }
    return 0;
}

static void thread_work ();

// non-blocking
static int
workers_run (const int n = 0)
{
    const int nworker = (n > 0) ? n : std::thread::hardware_concurrency();
    for (int i = 0; i < nworker; i++)
    {
        std::thread* worker = new std::thread (thread_work);
        g_workers.push_back (worker);
    }
    return 0;
}

// worker
static void
thread_work ()
{
    task_t task;
    int err = 0;

    while (1)
    {
        err = task_dequeue (task);
        if (err)
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
            continue;
        }

        err = task ();
        if (err)
        {
            syslog (LOG_ERR, "[thread_work] task error");
        }
    }
}

// --------------------------------------------------------------------------------
// service functions for settings
// --------------------------------------------------------------------------------

//
int
tts_server::setup (const json& conf)
{
    // inputs
    if (conf.find ("inputs") != conf.end ())
    {
        json seq = conf["inputs"];
        // for now, seq should be a singleton
        assert (seq.is_array ());
        for (json& s : seq)
        {
            assert (s.is_object ());
            lstnr_add (s);
        }
    }
    // fallback
    if (_listeners.empty ())
    {
        return -1;
    }

    // synthesizers
    if (conf.find ("synthesizers") != conf.end ())
    {
        json seq = conf["synthesizers"];
        assert (seq.is_array ());
        for (json& s : seq)
        {
            assert (s.is_object ());
            synth_add (s);
        }
    }
    // fallback
    if (_synthesizers.empty ())
    {
        return -1;
    }

    // outputs
    if (conf.find ("outputs") != conf.end ())
    {
        json seq = conf["outputs"];
        assert (seq.is_array ());
        for (json& s : seq)
        {
            assert (s.is_object ());
            sink_add (s);
        }
    }
    // fallback
    if (_sinks.empty ())
    {
        return -1;
    }

    return 0;
}

//
int
tts_server::setup (const char* conf_file)
{
    if (!conf_file) return -1;

    const char* conf_file_adjusted = conf_file;
    if (*conf_file != '/')
    {
        //fprintf (stderr, "config path (\"%s\") should be absolute\n", config_file);
        //exit (1);

        char dir[100];
        // case: relative path to the conf file
        if (strchr (conf_file, '/'))
            getcwd (dir, 100);
        // case: default path
        else
            snprintf (dir, 100, "%s/share/tts_server", PREFIX);

        char* path = (char*) malloc (100);
        snprintf (path, 100, "%s/%s", dir, conf_file);
        conf_file_adjusted = path;
    }

    json conf;
    try
    {
        FILE* f = fopen (conf_file_adjusted, "r");
        if (!f)
        {
            syslog (LOG_ERR, "[conf] file not found: \"%s\"\n", conf_file_adjusted);
            raise (SIGKILL);
        }

        struct stat buff;
        int err = stat (conf_file_adjusted, &buff);
        assert (!err);
        const int len = buff.st_size;
        assert (len < 100000);
        uint8_t content[len + 1];
        fread (content, len, 1, f);
        content[len] = '\0';
        fclose (f);

        conf = json::parse (content);
    }
    catch (...)
    {
        syslog (LOG_ERR, "[conf] parse error: %s", conf_file_adjusted);
        return -1;
    }

    return setup (conf);
}

void
tts_server::quit (int sig)
{
    syslog (LOG_NOTICE, "quit (signal = %d)", sig);

    for (listener* l : _listeners) l->quit();

    // syslog
    closelog ();

    exit (0);
}

// --------------------------------------------------------------------------------
// service function (main loop)
// --------------------------------------------------------------------------------

// helper
static int process_request (json* req);

// blocking
int
tts_server::run ()
{
    syslog (LOG_DEBUG, "[run]");

    // listeners (who put requests into the request queue)
    for (listener* l : _listeners) l->run();

    // workers (who extract requests and call process_requet for their processing)
    //int nworker = std::thread::hardware_concurrency();
    int nworker = 2;
    workers_run (nworker);
    syslog (LOG_INFO, "[tts_server::run] %d workers invoked", nworker);

    // blocking
    while (1)
    {
        if (req_empty())
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
            continue;
        }

        // fetch the (oldest) request message
        json* req = req_dequeue ();
        if (!req) continue;

        // execution of 'process_request' using a worker thread
        task_t task = [req]() { int err = process_request (req); delete req; return err; };
        task_enqueue (task);

#if 0
        // async call of 'process_request' -- INEFFICIENT
        // note: req is to be deallocated during the call
        auto f = std::async (process_request, req);

        // wait for completion of f (for 10sec)
        std::future_status status = f.wait_for(std::chrono::seconds(10));
        if (status == std::future_status::ready)
            f.get ();  // blocking
        else
        {
            assert ((int)status < 3);
            const char* status_msgs[] = {"ready", "timeout", "deferred"};
            syslog (LOG_ERR, "failure (%s) in process_request", status_msgs[(int)status]);
        }
#endif
    }

    return 0;
}

// topic = texter
// payload = {text, language, engine, host, sinks:[..]}
static int
process_request (json* req)
{
    if (!req) return -1;

    // find synthesizer
    synthesizer* synth = synth_find (*req);
    if (!synth)
    {
        syslog (LOG_ERR, "no synthesizer found");
        return -1;
    }
    assert (synth);

    // select sinks
    const std::list<sink*> sinks = sink_select (*req);
    if (sinks.empty ())
    {
        syslog (LOG_ERR, "no sink found");
        return -1;
    }
    assert (!sinks.empty());

    // call synthesizer (gRPC)
    char filename[100];
    std::tmpnam (filename);
    strcat (filename, ".wav");
    // synthesizer call (gRPC)
    int err = synth->synthesize (*req, filename);
    if (err)
    {
        syslog (LOG_ERR, "[process_request] synthesis failed (%d)", err);
        std::remove (filename);
        return -1;
    }
    syslog (LOG_DEBUG, "wave file (%s) generated", filename);

    // send to pulseaudio sinks
    syslog (LOG_NOTICE, "output to %d speaker(s)", sinks.size());
    // output to sinks (async)
    std::future<int> rslts[sinks.size ()];
    int n = 0;
    for (sink* s : sinks)
    {
        rslts[n++] = std::async (static_cast<int (sink::*)(const char*)>(&sink::consume), s, filename);
    }
    for (int i = 0; i < n; i++) rslts[i].get ();
    std::remove (filename);

    return 0;
}
