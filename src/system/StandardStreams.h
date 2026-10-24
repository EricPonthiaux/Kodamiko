// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The standard input and output of a helper program, which exchanges bytes
// with the program that started it. One implementation per system:
// StandardStreamsWindows.cpp, StandardStreamsUnix.cpp.
namespace StandardStreams {

// Bytes as they are, both ways: Windows' text mode turns "\n" into "\r\n"
// on output, which breaks a framing that counts bytes.
void useBinaryMode();

} // namespace StandardStreams
