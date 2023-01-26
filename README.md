# Summary

`tts_server` is a network TTS ([Text-to-Speech](https://en.wikipedia.org/wiki/Speech_synthesis)) server/wrapper,
written entirely in [C++](https://en.cppreference.com/w/cpp/11) and targeted for Linux,
with the following features:

- [input] input text (in [JSON](https://www.json.org)) for speech synthesis can be passed over [MQTT](https://mqtt.org/).  
  ([REST](https://en.wikipedia.org/wiki/Representational_state_transfer) is planned to be supported)
- [synthesizer] [eSpeak-ng](https://github.com/espeak-ng/espeak-ng), by default.
  [Google Cloud TTS](https://cloud.google.com/text-to-speech/docs/apis) can be selected, alternatively.
- [output] synthesized audio data (in [WAV](http://tools.ietf.org/html/rfc2361)) can be passed
  to multiple [pulseaudio](https://www.freedesktop.org/wiki/Software/PulseAudio/) and/or [sftp](https://en.wikipedia.org/wiki/SSH2) servers.

# Quick Demo

Launch a MQTT broker, such as Eclipse [mosquitto](https://mosquitto.org).

```
$ /etc/init.d/mosquitto start  
```

Make sure that you have an active pulseaudio server for digesting speech text.
By default, the server is assumed to be `localhost` (configurable).

```
$ pulseaudio --start  
$ pacmd list-sinks | grep 'Default Sink'  
$ paplay /usr/share/sounds/alsa/Noise.wav
```

When ready, start `tts_server`.

```
$ tts_server
```

Then, publish the following MQTT message that tells `tts_server` to say "hello".

```
$ mosquitto_pub -h localhost -t texter -m '{"text":"hello"}'
```

In a moment, you should hear the spearker attached to your host utter "hello".


# Build (docker container)

```
$ git clone https://github.com/handyman97/tts_server  
$ make -C tts_server docker-build
```

# Build (on a Debian/Ubuntu host)

## prerequisites

### Precompiled Debian/Ubuntu packages

- libmosquitto-dev (MQTT broker)
- nlohmann-json3-dev
- libespeak-ng-dev (eSpeak-ng TTS engine)
- libfestival-dev (Festival TTS engine)
- protobuf-compiler-grpc libgrpc++-dev
- libpulse-dev
- libssh2-1-dev

### Source packages

- [googleapis](https://github.com/googleapis/googleapis)


Once the above packages installed, run the following command:

```
$ git clone https://github.com/handyman97/tts_server  
$ cd tts_server  
$ make googleapis && make && make install  
```

The `tts_server` binary should be available at `/usr/local/bin`.

# Input to `tts_server`

Each input message to `tts_server` is a JSON object that carries the following key-value pairs:

- text: any string for speech synthesis
- language: "english", "en", "en_US", "en-US", etc, depending on TTS engines.  
  [default] "english"
- gender: "female", "male", "neutral", etc.  
  [default] "female"
- engine: synthesizer engine name such as "espeak", "festival", and "google"  
  when omitted, whichever engine that supports _language_ is picked up arbitrarily.
- synthesizer: synthesizer name that is defined as a part of configuration
- sinks: array of sink names  
  when omitted, synthesized speech is directed to all the sinks.

# Configuration

Configurations can be defined as separate JSON files.
You can provide `tts_server` with your own configuration by starting `tts_server` with `--conf <your_conf_file>`.  
See [this](https://github.com/handyman97/tts_server/tree/main/conf) for the detail.

Note, by default, `tts_server` looks up [`default.json`](../conf/default.json) in the `conf` directory.
