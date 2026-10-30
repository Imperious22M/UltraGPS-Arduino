//! @file command_handler.cpp
//! @brief CommandHandler implementation: serial line buffering, command lookup and
//!        argument decoding.
//!
//! The API is documented in command_handler.h.

#include "command_handler.h"
#include <stdlib.h>

CommandHandler::CommandHandler(const CommandEntry* table, uint8_t tableSize)
    : _table(table), _tableSize(tableSize), _bufLen(0)
{}

// ---------------------------------------------------------------------------

void CommandHandler::poll() {
    while (Serial.available() > 0) {
        char c = (char)Serial.read();

        // Commands are triggered on newline or carriage return
        if (c == '\n' || c == '\r') {
            if (_bufLen > 0) {
                _buf[_bufLen] = '\0';
                decode(_buf);
                _bufLen = 0;
            }
        } else if (_bufLen < CMD_BUF_SIZE - 1) {
            _buf[_bufLen++] = c;
        } else {
            // Buffer overflow — discard the line so far and this character. This does
            // not skip to the next line: the characters that follow are buffered as a
            // new line, so the tail of an overlong line is still decoded.
            _bufLen = 0;
            // Clear the entire buffer
            memset(_buf, 0, CMD_BUF_SIZE);
        }
    }
}

// ---------------------------------------------------------------------------

const CommandDef* CommandHandler::lookup(uint8_t letter) const {
    for (uint8_t i = 0; i < _tableSize; i++) {
        if (_table[i].letter == letter) {
            return _table[i].def;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------

bool CommandHandler::decode(const char* line) {
    if (line[0] == '\0') return false;

    const CommandDef* def = lookup((uint8_t)line[0]);
    if (def == nullptr) return false;

    // Arguments the command does not take stay 0
    CmdArg args[CMD_MAX_ARGS] = {};

    // Parse each expected argument from the remainder of the line
    const char* ptr = line + 1;
    for (uint8_t i = 0; i < def->numArgs; i++) {
        // Skip whitespace (spaces and tabs) before each argument
        while (*ptr == ' ' || *ptr == '\t') ptr++;
        if (*ptr == '\0') return false;  // Not enough arguments

        // Decode the argument according to its type.
        // TBH this only works because CmdArg's int32_t and float members are the same
        // size (4 bytes each on the AVR). If that is not the case, it is suggested to
        // use only integer arguments.
        char* end;
        switch (def->argTypes[i]) {
            case ArgType::INT:
                args[i].i = (int32_t)strtol(ptr, &end, 10);
                if (end == ptr) return false;  // Token was not a valid integer
                ptr = end;
                break;

            case ArgType::FLOAT:
                args[i].f = (float)strtod(ptr, &end);
                if (end == ptr) return false;  // Token was not a valid float
                ptr = end;
                break;

            default:
                return false;
        }
    }

    // Call the command's callback with this handler and the parsed arguments
    // (anything left on the line after the last expected argument is ignored)
    def->callback(*this, args);
    return true;
}
