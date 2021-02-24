//

#include "synthesizer.h"
#include "logger.h"

bool
synthesizer::engine_compliant (const char* eng) const
{
    syslog (LOG_DEBUG, "[engine_compliant] engine=\"%s\"", eng ? eng : "");

    if (!eng) return true;
    if (!strcmp (eng, "any")) return true;
    if (!strncmp (eng, engine.c_str(), strlen (eng))) return true;

    return false;
}

bool
synthesizer::language_supported (const char* lang) const
{
    syslog (LOG_DEBUG, "[language_supported] engine=%s language=%s", engine.c_str(), lang);

    for (const std::string& l : languages)
    {
        if (!strncmp (l.c_str(), lang, 2)) return true;
    }

    return false;
}
