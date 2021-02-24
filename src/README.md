# tts\_talk

`tts_talk` is a daemon process.  
Upon receiving a text message via MQTT, it synthesizes a speech from the text, and sends
it to one or more speakers.

## flow
cleint -(text via mqtt)-> tts\_talk -(pulseaudio)-> speaker(s)

## message (MQTT)

- topic = .../_var<sb>1</sb>=val<sb>1</sb>_/_var<sb>2</sb>=val<sb>2</sb>_/...  
  topic to subscribe to can be chosen arbitrarily,  
  though those topic levels of the form _var_=_val_ are regarded as
  parameters for adjusting the behavior of tts\_talk.

  _var_=_val_:
  - language=_lang_ (en | es | ja | ..)
  - facility=_fac_ (BOT | SLACK)
  - level=_lev_ (EMERG | ALERT | CRIT | ERR | WARNING | NOTICE | INFO | DEBUG)

- payload: text in UTF-8

## configuration

- tts servers

  syntax  
  ```
synthesizer <ip>(:<port>)? <lang>(,<lang>)*
  ```

  (fallback: `synthesizer localhost any`)

- sinks (pulseaudio servers)

  syntax  
  ```
sink <ip>(:<port>)? <location>
  ```

- rules

  syntax  
  ```
rule <facility>.<level> -> <location>( <location>)*
  ```

  (fallback: `rule any.any -> all`)

