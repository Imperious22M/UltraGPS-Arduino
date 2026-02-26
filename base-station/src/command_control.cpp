#include "command_control.h"
#include <stdlib.h>

CommandControl::CommandControl(const CommandEntry* table, uint8_t tableSize)
    : _table(table), _tableSize(tableSize), _bufLen(0)
{}

// ---------------------------------------------------------------------------

void CommandControl::poll() {
    while (Serial.available() > 0) {
        char c = (char)Serial.read();

        //! Commands are triggered on newline or carriage return
        if (c == '\n' || c == '\r') {
            if (_bufLen > 0) {
                _buf[_bufLen] = '\0';
                decode(_buf);
                _bufLen = 0;
            }
        } else if (_bufLen < CMD_BUF_SIZE - 1) {
            _buf[_bufLen++] = c;
        } else {
            //! Buffer overflow — discard and wait for next line
            _bufLen = 0;
            //! Clear the entire buffer
            memset(_buf, 0, CMD_BUF_SIZE);
        }
    }
}

// ---------------------------------------------------------------------------

const CommandDef* CommandControl::lookup(uint8_t letter) const {
    for (uint8_t i = 0; i < _tableSize; i++) {
        if (_table[i].letter == letter) {
            return _table[i].def;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------

bool CommandControl::decode(const char* line) {
    if (line[0] == '\0') return false;

    const CommandDef* def = lookup((uint8_t)line[0]);
    if (def == nullptr) return false;

    CmdArg args[CMD_MAX_ARGS] = {};

    //! Parse each expected argument from the remainder of the line
    const char* ptr = line + 1;
    for (uint8_t i = 0; i < def->numArgs; i++) {
        //! Skip whitespace between arguments
        while (*ptr == ' ' || *ptr == '\t') ptr++;
        if (*ptr == '\0') return false;  //! Not enough arguments

        //! Decode the command arguments based on their types
        //! TBH this only works because the union of CMDArg treats
        //! int and float as the same size. If that is not the case
        //! it is suggested to only use integer arguments
        char* end;
        switch (def->argTypes[i]) {
            case ArgType::INT:
                args[i].i = (int32_t)strtol(ptr, &end, 10);
                if (end == ptr) return false;  //! Token was not a valid integer
                ptr = end;
                break;

            case ArgType::FLOAT:
                args[i].f = (float)strtod(ptr, &end);
                if (end == ptr) return false;  //! Token was not a valid float
                ptr = end;
                break;

            default:
                return false;
        }
    }

    //! Call the command's callback with the parsed arguments
    def->callback(args);
    return true;
}
