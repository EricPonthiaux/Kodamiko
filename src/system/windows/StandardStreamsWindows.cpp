// SPDX-License-Identifier: GPL-3.0-only

#include "system/StandardStreams.h"

#include <fcntl.h>
#include <io.h>

#include <cstdio>

namespace StandardStreams {

void useBinaryMode()
{
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
}

} // namespace StandardStreams
