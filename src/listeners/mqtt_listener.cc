//

#include <array>
#include <cassert>
#include <cctype>
#include <cinttypes>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <future>
#include <list>
#include <queue>
#include <regex>
#include <thread>
#include <utility>

#include <libgen.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

//#include <grpc++/grpc++.h>
#include <mosquitto.h>
#include <nlohmann/json.hpp>

#include "mqtt_listener.h"
#include "server.h"
#include "logger.h"

using json = nlohmann::json;

// mosquitto callbacks
static void cb_connect (struct mosquitto* mosq, void* user, int rc);
static void cb_subscribe (struct mosquitto* mosq, void* user, int mid, int qos_count, const int *granted_qos);
static void cb_message (struct mosquitto* mosq, void* user, const struct mosquitto_message* msg);
static void cb_disconnect (struct mosquitto* mosq, void* user, int reason);

// ctor
mqtt_listener::mqtt_listener ()
{
    _mosq = nullptr;
}

bool mqtt_listener::_initialized = false;

int
mqtt_listener::setup (const nlohmann::json& conf)
{
    syslog (LOG_NOTICE, "[mqtt_setup] %s", conf.dump().c_str());

    // host
    assert (conf.find("host") != conf.end());
    std::string host = conf["host"];

    // address & port
    const size_t pos = host.find (':');
    if (pos != std::string::npos)
    {
        _address = host.substr (0, pos);
        _port = std::stoi (host.substr (pos + 1));
    }
    else
    {
        _address = host;
        _port = 1883;
    }

    // topic
    assert (conf.find ("topic") != conf.end ());
    const std::string topic = conf["topic"];

    // mosquitto
    if (!_initialized)
    {
        mosquitto_lib_init ();
        _initialized = true;
    }

    // instance
    _mosq = mosquitto_new (NULL, true, this);
      // client_id, clean_session, user_obj

    assert (!topic.empty());
    _topics.push_back (topic);
    //if (mqtt_topic_re) g_mqtt_topic_re = new std::regex (mqtt_topic_re);

    int rslt = MOSQ_ERR_SUCCESS;
    mosquitto_connect_callback_set (_mosq, cb_connect);
    mosquitto_subscribe_callback_set (_mosq, cb_subscribe);
    // upon receiving messages
    mosquitto_message_callback_set (_mosq, cb_message);
    // for disconnection (due to client-side problems)
    mosquitto_disconnect_callback_set (_mosq, cb_disconnect);
    //const char* mqtt_will = "disconnected";
    //rslt = mosquitto_will_set (mosq, mqtt_will_topic, strlen (mqtt_will) + 1, mqtt_will, 1, true);
      // mosq, topic, payload_len, payload, qos, retain
    //assert (rslt == MOSQ_ERR_SUCCESS);

    // connection
    const int keep_alive = 100;
      // note: connection will be lost if no message is transmitted for (1.5 * keep_alive) seconds
    rslt = mosquitto_connect_bind (_mosq, _address.c_str(), _port, keep_alive, NULL);
      // mosq, host, port, keep_alive, bind_addr
    if (rslt != MOSQ_ERR_SUCCESS)
    {
        syslog (LOG_ERR, "[mqtt_listener::setup] failure in mosquitto_connect_bind");
        syslog (LOG_ERR, mosquitto_strerror (rslt));
        raise (SIGTERM);
    }

    // subscription
    //rslt = mosquitto_subscribe (mosq, NULL, mqtt_topic, 1);
    //rslt = mosquitto_subscribe (mosq, NULL, "watchdog/ping", 1);
    //assert (rslt == MOSQ_ERR_SUCCESS);

    return 0;
}

int
mqtt_listener::run (void)
{
    // mosq
    assert (_mosq);
    // loop
    //const int timeout = 1000; // ms
    //rslt = mosquitto_loop_forever (mosq, timeout, 1);
         // mosq, timeout, max_packets (unused)
    //syslog (LOG_NOTICE, "mosquitto_loop_forever (timeout = %ds)", timeout);
    int rslt = mosquitto_loop_start (_mosq);
    if (rslt != MOSQ_ERR_SUCCESS)
    {
        syslog (LOG_ERR, "mosquitto_loop_start failed: (%d) %s", rslt, mosquitto_strerror (rslt));
        exit (1);
    }

    return 0;
}

void
mqtt_listener::quit (void)
{
    syslog (LOG_NOTICE, "[mqtt_listener::quit] host=%s:%d", _address, _port);
    //std::cerr << "quit: SIGNAL=" << sig << "\n";
    //fprintf (stderr, "SIGNAL = %d\n", sig);

    // mosquitto
    if (!_mosq) return;

    mosquitto_disconnect (_mosq);
    mosquitto_destroy (_mosq);
    mosquitto_lib_cleanup ();
    _mosq = nullptr;
}

// --------------------------------------------------------------------------------
// mqtt callbacks
// --------------------------------------------------------------------------------

// called when the broker sends a CONNACK message in response to a connection
void
cb_connect (struct mosquitto* mosq, void* user, int rc)
{
    syslog (LOG_NOTICE, "[cb_connect] connected (%d)", rc);
    //if (rc == 0) return; // mosquitto_connect call

    mqtt_listener* l = (mqtt_listener*)user;
    // subscription
    // g_mqtt_toipic = 'texter' by default
    for (std::string& t : l->topics())
    {
        assert (!t.empty());
        int rslt = mosquitto_subscribe (mosq, NULL, t.c_str(), 1);
        if (rslt != MOSQ_ERR_SUCCESS)
        {
            syslog (LOG_ERR, "[cb_connect] subscription to \"%s\" failed (%d): %s",
                    t.c_str(), rslt, mosquitto_strerror (rslt));
            raise (SIGTERM);
        }
    }
}

// called when the broker responds to a subscription request.
void
cb_subscribe (struct mosquitto* mosq, void* user, int mid, int qos_count, const int *granted_qos)
{
    syslog (LOG_NOTICE, "[cb_subscribe] subscription granted: mid=%d qos_count=%d", mid, qos_count);
}

// called when a message is received from the broker.
// note: those messages stored into g_mqtt_messages are processed by calling async(process_message, msg) running as a separate thread
void
cb_message (struct mosquitto* mosq, void* user, const struct mosquitto_message* msg)
{
    if (!msg) return;

    const uint8_t* payload = (uint8_t*)msg->payload;
    syslog (LOG_DEBUG, "[cb_message] %s", payload);
    json req;
    try
    {
        req = json::parse ((const uint8_t*)payload);
    }
    catch (...)
    {
        syslog (LOG_ERR, "[cb_message] parse error: %s", payload);
        return;
    }

    tts_server::req_enqueue (new json(req));

    /*
    mosquitto_message* copy = (mosquitto_message*) malloc (sizeof (mosquitto_message));
    mosquitto_message_copy (copy, msg);
    g_mutex.lock ();
    g_mqtt_messages.push_back (copy);
    g_mutex.unlock ();
    int len = g_mqtt_messages.size ();
    if (len > 10) syslog (LOG_WARNING, "too many pending messages (%d)", len);
    */
}

// called when the broker has received the DISCONNECT command and has disconnected the client.
void
cb_disconnect (struct mosquitto* mosq, void* user, int reason)
{
    if (reason == 0) return; // mosquitto_disconnect call
    syslog (LOG_DEBUG, "[cb_disconnect] reason=%d", reason);

    if (reason != MOSQ_ERR_CONN_LOST)
    {
        syslog (LOG_ERR, "disconnected");
        raise (SIGHUP);
    }

    // reconnection
    int rslt;
    for (int k = 0; k < 10; k++)
    {
        sleep (3);
        rslt = mosquitto_reconnect (mosq);
        if (rslt == MOSQ_ERR_SUCCESS) return;
    }

    syslog (LOG_ERR, "[cb_disconnect] error=%d", rslt);
    raise (SIGHUP);

    return;
}
