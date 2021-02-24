//

#include "sink_sftp.h"
#include "logger.h"

#include <libssh2_sftp.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <cassert>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <thread>
#include <unistd.h>

bool sink_sftp::_initialized = false;

sink_sftp::sink_sftp (const nlohmann::json& spec)
{
    syslog (LOG_INFO, "[sftp] spec: %s", spec.dump().c_str());

    // address & port
    if (spec.find ("host") == spec.end ())
    {
        syslog (LOG_ERR, "[sftp] host not specified");
        _address = "localhost";
    }
    else
        _address = spec["host"];

    _port = 22;
    
    // sink name
    if (spec.find ("name") != spec.end () && spec["name"].is_string ())
        name = spec["name"];
    else
        name = _address;

    // sftp username
    if (spec.find ("username") == spec.end ())
    {
        syslog (LOG_ERR, "[sftp] username not specified");
        _username = "nobody";
    }
    else
        _username  = spec["username"];

    // public&private keys
    if (spec.find ("publickey") != spec.end ())
        _publickey_path  = spec["publickey"];
    if (spec.find ("privatekey") != spec.end ())
        _privatekey_path  = spec["privatekey"];
    // password
    if (spec.find ("password") != spec.end () && spec["password"].is_string())
        _password = spec["password"];

    //
    if (!_initialized)
    {
        int err = libssh2_init (0);
        if (err)
        {
            syslog (LOG_ERR, "[sftp] libssh2 initialization failed");
            return;
        }
        else
            _initialized = true;
    }
}

// wait until socket becomes ready
// we need this, since we choose non-blocking session
static int
waitsocket (int sock, LIBSSH2_SESSION *session)
{
    // wait until socket becomes ready
    fd_set fd;
    FD_ZERO(&fd);
    FD_SET(sock, &fd);

    int dir = libssh2_session_block_directions(session);
    fd_set *readfd = (dir & LIBSSH2_SESSION_BLOCK_INBOUND) ? &fd : nullptr;
    fd_set *writefd = (dir & LIBSSH2_SESSION_BLOCK_OUTBOUND) ? &fd : nullptr;

    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;

    syslog (LOG_DEBUG, "[sftp::waitsocket] retrying libssh2_sftp_init: dir=%d blocked(r=%d, w=%d)",
            dir, dir & LIBSSH2_SESSION_BLOCK_INBOUND, dir & LIBSSH2_SESSION_BLOCK_OUTBOUND);

    int rslt = select (sock + 1, readfd, writefd, NULL, &timeout);
    // >0: #fd (on success), 0: timeout, <0: error
    if (rslt <= 0)
        syslog (LOG_ERR, "[sftp::waitsocket] select failed: error=%d", rslt);

    return rslt;
}

int
sink_sftp::consume (int wav_fd)
{
    //syslog (LOG_DEBUG, "[play] address=%s device=\"%s\"", _address.c_str(), _device.c_str());

    int err = 0;

    // socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    const char* address = _address.c_str();
    int ok = inet_aton (address, &sin.sin_addr);
    if (!ok)
    {
        syslog (LOG_ERR, "[sftp::consume] invalid address: %s", address);
        return -1;
    }
    //sin.sin_addr.s_addr = htonl(0x7F000001); // localhost=127.0.0.1
    sin.sin_port = htons(_port);
    err = connect(sock, (struct sockaddr*)(&sin), sizeof(struct sockaddr_in));
    if (err)
    {
        syslog (LOG_ERR, "[sftp] connection failure");
        return err;
    }
    syslog (LOG_DEBUG, "[sftp::consume] socket connected to: %s:%d", address, _port);

    // libssh2 session
    LIBSSH2_SESSION *session = libssh2_session_init();
    if (!session) return -1;

    err = libssh2_session_handshake(session, sock);
    if (err)
    {
        syslog (LOG_ERR, "[sftp::consume] failure in establishing ssh session: %d", err);
        return -1;
    }

    libssh2_session_set_blocking (session, 0); // non-blocking

    // authentication (by public key)

    const char* username = _username.c_str();
    const char* pubkey_path = _publickey_path.empty() ? nullptr : _publickey_path.c_str();
    const char* privkey_path = _privatekey_path.empty() ? nullptr : _privatekey_path.c_str();
    const char* password = _password.c_str();

    //const char* fingerprint = libssh2_hostkey_hash (session, LIBSSH2_HOSTKEY_HASH_SHA1);
    while(1)
    {
        if (pubkey_path && privkey_path)
            err = libssh2_userauth_publickey_fromfile (session, username, pubkey_path, privkey_path, password);
        else
            err = libssh2_userauth_password (session, username, password);

        if (!err) break;
        if (err ==  LIBSSH2_ERROR_EAGAIN) continue;

        syslog (LOG_ERR, "[sftp::consume] authentication failed: error=%d", err);
        // special case
        if (err == LIBSSH2_ERROR_PUBLICKEY_UNVERIFIED)
            syslog (LOG_ERR, "[sftp::consume] unsupported publickey format (probably)");

        goto disconnect;
    }
    syslog (LOG_INFO, "[sftp::consume] ssh2 authentication complete");

    // sftp session
    LIBSSH2_SFTP *sftp_session;
    sftp_session = nullptr;
    while (1)
    {
        sftp_session = libssh2_sftp_init(session);
        if (sftp_session) break;

        err = libssh2_session_last_errno(session);
        if (err != LIBSSH2_ERROR_EAGAIN)
        {
            syslog (LOG_ERR, "[sftp::consume] sftp_session creation failed: error=%d", err);
            goto disconnect;
        }

        int rslt = waitsocket (sock, session);
        // >0: #fd (on success), 0: timeout, <0: error
        if (rslt <= 0) { err = -1; goto shutdown; }
    }
    syslog (LOG_DEBUG, "[sftp::consume] sftp session started");

    // upload: wav_fd -> dest

    FILE* src; src = fdopen (wav_fd, "r");
    if (!src) { err = -1; goto shutdown; }

    char dest[100];
    time_t t; t = time (nullptr); // sec since 1970-1-1
    struct tm* now; now = localtime (&t);
    snprintf (dest, 100, "/tmp/speech_%04d%02d%02dT%02d%02d%02d.wav",
              now->tm_year + 1900, now->tm_mon + 1, now->tm_mday,
              now->tm_hour, now->tm_min, now->tm_sec);

    unsigned long flags; flags = LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_EXCL;
    // R/W for user, R for group and other
    long mode; mode = LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR | LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH;

    // --------------------------------------------------------------------------------
    // sft_handle for uploading
    LIBSSH2_SFTP_HANDLE* sftp_handle;
    while (1)
    {
        sftp_handle = libssh2_sftp_open (sftp_session, dest, flags, mode);
        if (sftp_handle) break;

        err = libssh2_session_last_errno(session);
        if (err != LIBSSH2_ERROR_EAGAIN)
        {
            syslog (LOG_ERR, "[sftp::consume] sftp_handle creation failed: error=%d outfile=\"%s\"", err, dest);
            goto shutdown;
        }

        int rslt = waitsocket (sock, session);
        // >0: #fd (on success), 0: timeout, <0: error
        if (rslt <= 0) { err = -1; goto shutdown; }
    }
    syslog (LOG_DEBUG, "[sftp::consume] sftp_handle created");

    // libssh2_sftp_write
    while (1)
    {
        err = 0;

        // wav_fd -> buff
        char buff[1024];
        size_t nread = fread (buff, 1, sizeof(buff), src);

        if (nread == 0) break;  // eof

        if (nread < 0) { err = nread; goto shutdown; }
        assert (nread <= 1024);
        syslog (LOG_DEBUG, "[sftp::consume] done w. reading %dB of wav", nread);

        // buff -> remote file
        char* ptr = buff;
        size_t nremaining = nread;
        while (nremaining > 0)
        {
            ssize_t ntransferred = libssh2_sftp_write (sftp_handle, ptr, nremaining);
            if (ntransferred >= 0)
            {
                ptr += ntransferred;
                nremaining -= ntransferred;
                if (nremaining == 0) break;
                if (nremaining > 0) continue;
            }
            else if (ntransferred != LIBSSH2_ERROR_EAGAIN)
            {
                err = ntransferred;
                goto shutdown;
            }

            // wait until socket becomes ready
            fd_set fd_R, fd_W;
            FD_ZERO (&fd_R); FD_SET (sock, &fd_R);
            FD_ZERO (&fd_W); FD_SET (sock, &fd_W);
            // timeout = 10s
            struct timeval timeout;
            timeout.tv_sec = 10;
            timeout.tv_usec = 0;
            syslog (LOG_DEBUG, "[sftp::consume] wait until socket becomes ready");
            int rslt = select (sock + 1, &fd_R, &fd_W, NULL, &timeout);
            // >0: #fd (on success), 0: timeout, <0: error
            if (rslt <= 0) { err = -1; goto shutdown; }
        }
        syslog (LOG_DEBUG, "[sftp::consume] transferred %dB of wav", nread);
    }

    libssh2_sftp_close (sftp_handle);
    // --------------------------------------------------------------------------------

 shutdown:
    libssh2_sftp_shutdown (sftp_session);

 disconnect:
    libssh2_session_disconnect(session, "disconnected");
    libssh2_session_free(session);
    close(sock);

    syslog (LOG_DEBUG, "[sftp::consume] tcp session closed (%d)", err);

    return err;
}
