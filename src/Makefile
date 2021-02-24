#

PREFIX		?=	/usr/local
TOP_DIR		?=	..
BUILD_DIR	?=	$(TOP_DIR)/_build

CFLAGS		=	-std=c11
CXXFLAGS	=	-std=c++11
CXXFLAGS	+=	-g
CPPFLAGS	=	-I$(TOP_DIR)/src
CPPFLAGS	+=	-DPREFIX=\"$(PREFIX)\"
ifdef	DAEMONIZE
CPPFLAGS	+=	-DDAEMONIZE=$(DAEMONIZE)
endif
LDFLAGS		= 	-lpthread -ldl

all::

BINS		=	tts_server
OBJS		=	logger server synthesizer sink

# mosquitto
OBJS		+=	listeners/mqtt_listener
LDFLAGS		+=	-lmosquitto

# espeak-ng
OBJS		+=	synthesizers/synth_espeak
LDFLAGS		+=	-lespeak-ng

# festival
OBJS		+=	synthesizers/synth_festival
CPPFLAGS	+=	-I/usr/include/speech_tools
LDFLAGS		+=	-lFestival \
			-L/usr/lib/speech_tools/lib -lestools -lestbase -leststring -lgomp

# google cloud_tts
-include Makefile.googleapis
OBJS		+=	synthesizers/synth_gcloud
#OBJS		+=	$(API_OBJS) $(TTS_OBJS)

# pulseaudio
OBJS		+=	sinks/sink_pulseaudio
LDFLAGS		+=	-lpulse-simple

# sftp
OBJS		+=	sinks/sink_sftp
LDFLAGS		+=	-lssh2

#
BINS		:=	$(BINS:%=$(BUILD_DIR)/%)
OBJS		:=	$(OBJS:%=$(BUILD_DIR)/%.o)

#
all::	$(BINS)

#
$(BUILD_DIR)/tts_server:	$(API_OBJS) $(TTS_OBJS) $(OBJS) $(BUILD_DIR)/main.o
	@mkdir -p $$(dirname $@)
	$(CXX) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o:	%.cc
	@mkdir -p $$(dirname $@)
	$(CXX) -o $@ $(CPPFLAGS) $(CXXFLAGS) -c $<

$(BUILD_DIR)/%.o:	%.c
	@mkdir -p $$(dirname $@)
	$(CC) -o $@ $(CPPFLAGS) $(CFLAGS) -c $<

install::	all
	@mkdir -p $(PREFIX)/bin
	cp -fp $(BINS) $(PREFIX)/bin

clean::;	rm -f *~
veryclean::	clean
	rm -rf $(BUILD_DIR)

#
-include Makefile.depend
Makefile.depend:	$(shell echo *.h *.c *.cc)
	@rm -f $@ && touch $@
#	@echo "# SRC_DIRS = $(SRC_DIRS)" >> $@
	$(CPP) -I. -MM $^ 2> /dev/null |\
	while read LINE; do \
	echo $$LINE | sed -r 's/:/ : /g' |\
	gawk -v dir=$(BUILD_DIR) 'BEGIN{FS="[ \t:]+"}/^[^:]/{printf("%s/%s ",dir,gensub(/^(\.\/)?(.*)/,"\\2","1",$$0));next}{printf("%s ",$$0)}END{printf("\n")}';\
	done >> $@
	@echo >> $@
	@echo "# generated at: `date +%Y-%m-%dT%H:%M:%S%z`" >> $@
