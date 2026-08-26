#pragma once

#include <QString>

struct ClaudeSessionCookies
{
    QString orgId;
    QString header;
};

bool loadClaudeSessionCookies(ClaudeSessionCookies *out, QString *error);
