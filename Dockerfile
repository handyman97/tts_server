FROM debian:buster-slim as builder
MAINTAINER handyman (https://github.com/handyman97)

ENV DEBIAN_FRONTEND noninteractive
ENV DEBIAN_PRIORITY critical
ENV DEBCONF_NOWARNINGS yes

RUN echo "dash dash/sh boolean false" | debconf-set-selections;\
    dpkg-reconfigure -f noninteractive dash;\
    echo "/usr/local/lib" > /etc/ld.so.conf.d/usr-local-lib.conf;\
    apt update;\
    apt install -y build-essential bison flex gawk git rsync wget;\
    apt install -y nlohmann-json3-dev libmosquitto-dev libespeak-ng-dev festival-dev protobuf-compiler-grpc libgrpc++-dev libpulse-dev libssh2-1-dev;\
    apt install -y mosquitto mosquitto-clients rsyslogd

# tts_server
ADD . /root/tts_server
RUN cd /root/tts_server;\
    make veryclean && make -C src googleapis && PREFIX=/usr/local make install

WORKDIR /root
CMD ["/bin/bash"]

# ====================
# final image
# ====================
FROM debian:buster-slim

RUN echo "dash dash/sh boolean false" | debconf-set-selections;\
    dpkg-reconfigure -f noninteractive dash;\
    echo "/usr/local/lib" > /etc/ld.so.conf.d/usr-local-lib.conf;\
    apt update;\
    apt install -y libmosquitto-dev libespeak-ng-dev festival-dev libgrpc++-dev libpulse-dev libssh2-1-dev libgomp1;\
    apt install -y rsyslog

COPY --from=builder /usr/local /usr/local

WORKDIR /root
CMD ["/bin/bash"]
