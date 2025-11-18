//===-----------------------------------------------------------------------===//
//
// This file is part of tam-cpp, copyright (c) Ian Knight 2025.
//
// tam-cpp is free software: you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// tam-cpp is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with tam-cpp. If not, see <https://www.gnu.org/licenses/>.
//
//===-----------------------------------------------------------------------===//
//
/// @file tam.cc
/// This file defines all methods of `TamEmulator` except for `Allocate`,
/// `Free`, and the methods for executing primitive operations.
//
//===-----------------------------------------------------------------------===//

#include "tam.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <algorithm>
#include <format>
#include <iomanip>
#include <sstream>
#include <stack>
#include <vector>
#include <fstream>

#include "error.h"

namespace tam {

TamEmulator::TamEmulator(FILE* instream, FILE* outstream) {
    if (!(instream && outstream)) {
        throw IoError("NULL passed for input or output");
    }
    this->instream = instream;
    this->outstream = outstream;

    Reset();
}

void TamEmulator::Reset()
{
    this->data_store.fill(0);

    allocated_blocks.clear();
    free_blocks.clear();

    this->registers.fill(0);
    this->registers[HB] = kMaxAddr;
    this->registers[HT] = kMaxAddr;
    this->registers[CT] = this->program.size();
    this->registers[PB] = this->program.size();
    this->registers[PT] = this->registers[PB] + 29;

    this->halted = false;

    output.clear();
}

void TamEmulator::LoadProgramFromFile(std::string filename)
{
    std::ifstream in_stream(filename, std::ios::binary);

    // find file size
    in_stream.seekg(0, in_stream.end);
    int file_len = in_stream.tellg();
    in_stream.seekg(0, in_stream.beg);

    if (file_len % 4 != 0)
        throw IoError("program file contained incomplete instruction");

    if (file_len / 4 > kMemSize)
        throw IoError("program file too large");

    for (int j = 0; j < file_len / 4; ++j) {
        int c;
        uint32_t code = 0;
        for (int i = 0; i < 4; ++i) {
            c = in_stream.get();
            code = (code << 8) | c;
        }
        TamInstruction instruction = Decode(code);
        program.push_back(instruction);
        mnemonics.push_back(GetMnemonic(instruction));
    }

    Reset();
}

TamInstruction Decode(uint32_t code)
{
    uint8_t op = (code & 0xf0000000) >> 28;
    uint8_t r  = (code & 0x0f000000) >> 24;
    uint8_t n  = (code & 0x00ff0000) >> 16;
    int16_t d  =  code & 0x0000ffff;

    assert(op <= 0xf);
    assert(r  <= 0xf);
    assert(n  <= 0xff);

    return {op, r, n, d};
}

TamInstruction TamEmulator::FetchDecode()
{
    TamAddr addr = this->registers[CP]++;
    if (addr >= this->registers[CT])
        throw std::runtime_error(std::format("code access violation at loc {}: attempted to cycle", this->registers[CP] - 1));

    return this->program[addr];
}

void TamEmulator::PushData(TamData value)
{
    TamAddr addr = this->registers[ST];
    if (addr >= this->registers[HT])
        throw RuntimeError(ExceptionKind::kStackOverflow, this->registers[CP] - 1);

    this->data_store[addr] = value;
    this->registers[ST]++;
    assert(this->data_store[addr] == value);
}

TamData TamEmulator::PopData()
{
    TamAddr addr = this->registers[ST];
    if (this->registers[ST] == 0)
        throw RuntimeError(ExceptionKind::kStackUnderflow, this->registers[CP] - 1);

    this->registers[ST]--;
    return this->data_store[this->registers[ST]];
}

void TamEmulator::Execute(TamInstruction instr)
{
    switch (instr.op) {
        case LOAD:
            this->ExecuteLoad(instr);
            break;
        case LOADA:
            this->ExecuteLoada(instr);
            break;
        case LOADI:
            this->ExecuteLoadi(instr);
            break;
        case LOADL:
            this->ExecuteLoadl(instr);
            break;
        case STORE:
            this->ExecuteStore(instr);
            break;
        case STOREI:
            this->ExecuteStorei(instr);
            break;
        case CALL:
            if (instr.r == PB && instr.d > 0 && instr.d < 29) {
                this->ExecuteCallPrimitive(instr);
            } else {
                this->ExecuteCall(instr);
            }
            break;
        case CALLI:
            this->ExecuteCalli(instr);
            break;
        case RETURN:
            this->ExecuteReturn(instr);
            break;
        case PUSH:
            this->ExecutePush(instr);
            break;
        case POP:
            this->ExecutePop(instr);
            break;
        case JUMP:
            this->ExecuteJump(instr);
            break;
        case JUMPI:
            this->ExecuteJumpi(instr);
            break;
        case JUMPIF:
            this->ExecuteJumpif(instr);
            break;
        case HALT:
            this->halted = true;
            break;
        default:
            throw RuntimeError(ExceptionKind::kUnknownOpcode, this->registers[CP] - 1);
    }
}

const std::string TamEmulator::GetSnapshot() const
{
    std::stringstream ss;

    ss << std::hex << std::setfill('0');

    ss << "Stack";
    for (int I = 0; I < this->registers[ST]; ++I) {
        if (I % 8 == 0) ss << std::endl;
        ss << std::setw(4) << this->data_store[I] << " ";
    }
    ss << std::endl;

    for (auto Block : this->allocated_blocks) {
        ss << "Heap " << std::setw(4) << Block.first;
        for (int I = 0; I < Block.second; ++I) {
            if (I % 8 == 0) {
                ss << std::endl;
            }
            ss << std::setw(4) << this->data_store[Block.first + I] << " ";
        }
    }

    return ss.str();
}

void TamEmulator::ExecuteLoad(TamInstruction instr)
{
    TamAddr base_addr = this->registers[instr.r] + instr.d;

    for (int I = 0; I < instr.n; ++I) {
        TamAddr addr = base_addr + I;
        if (addr >= this->registers[ST] && addr <= this->registers[HT])
            throw RuntimeError(ExceptionKind::kDataAccessViolation,
                               this->registers[CP] - 1);

        TamData value = this->data_store[addr];
        this->PushData(value);
    }
}

void TamEmulator::ExecuteLoada(TamInstruction instr)
{
    TamAddr addr = this->registers[instr.r] + instr.d;
    this->PushData(addr);
}

void TamEmulator::ExecuteLoadi(TamInstruction instr)
{
    TamAddr base_addr = this->PopData();

    for (int I = 0; I < instr.n; ++I) {
        TamAddr addr = base_addr + I;
        if (addr >= this->registers[ST] && addr <= this->registers[HT])
            throw RuntimeError(ExceptionKind::kDataAccessViolation,
                               this->registers[CP] - 1);

        TamData value = this->data_store[addr];
        this->PushData(value);
    }
}

void TamEmulator::ExecuteLoadl(TamInstruction instr)
{
    this->PushData(instr.d);
}

void TamEmulator::ExecuteStore(TamInstruction instr)
{
    std::stack<TamData> Data;
    for (int I = 0; I < instr.n; ++I) Data.push(this->PopData());

    TamAddr base_addr = this->registers[instr.r] + instr.d;
    for (int I = 0; I < instr.n; ++I) {
        TamAddr addr = base_addr + I;
        if (addr >= this->registers[ST] && addr <= this->registers[HT])
            throw RuntimeError(ExceptionKind::kDataAccessViolation,
                               this->registers[CP] - 1);

        this->data_store[addr] = Data.top();
        Data.pop();
    }

    assert(Data.empty());
}

void TamEmulator::ExecuteStorei(TamInstruction instr)
{
    TamAddr base_addr = this->PopData();

    std::stack<TamData> Data;
    for (int I = 0; I < instr.n; ++I) Data.push(this->PopData());

    for (int I = 0; I < instr.n; ++I) {
        TamAddr addr = base_addr + I;
        if (addr >= this->registers[ST] && addr <= this->registers[HT])
            throw RuntimeError(ExceptionKind::kDataAccessViolation,
                               this->registers[CP] - 1);

        this->data_store[addr] = Data.top();
        Data.pop();
    }

    assert(Data.empty());
}

void TamEmulator::ExecuteCall(TamInstruction instr)
{
    TamAddr call_address = this->registers[instr.r] + instr.d;
    if (call_address >= this->registers[CT])
        throw std::runtime_error(std::format("code access violation at loc {}: attempted to call function at loc {}", this->registers[CP] - 1, call_address));

    TamAddr static_link = this->registers[instr.n];
    assert(static_link < this->registers[ST]);
    TamAddr dynamic_link = this->registers[LB];
    assert(dynamic_link < this->registers[ST]);
    TamAddr return_addr = this->registers[CP];
    assert(return_addr < this->registers[CT]);

    this->PushData(static_link);
    this->PushData(dynamic_link);
    this->PushData(return_addr);

    this->registers[LB] = this->registers[ST] - 3;
    this->registers[CP] = call_address;
}

void TamEmulator::ExecuteCalli(TamInstruction instr)
{
    TamAddr call_address = this->PopData();
    TamAddr static_link = this->PopData();
    assert(static_link < this->registers[ST]);

    if (call_address >= this->registers[CT])
        throw std::runtime_error(std::format("code access violation at loc {}: attempted to call function at loc {}", this->registers[CP] - 1, call_address));

    TamAddr dynamic_link = this->registers[LB];
    assert(dynamic_link < this->registers[ST]);
    TamAddr return_addr = this->registers[CP];
    assert(return_addr < this->registers[CT]);

    this->PushData(static_link);
    this->PushData(dynamic_link);
    this->PushData(return_addr);

    this->registers[LB] = this->registers[ST] - 3;
    this->registers[CP] = call_address;
}

void TamEmulator::ExecuteReturn(TamInstruction instr)
{
    std::stack<TamData> return_val;
    for (int I = 0; I < instr.n; ++I) return_val.push(this->PopData());

    assert(return_val.size() == instr.n);

    TamAddr dynamic_link = this->data_store[this->registers[LB] + 1];
    TamAddr return_addr = this->data_store[this->registers[LB] + 2];
    if (return_addr >= this->registers[CT])
        throw std::runtime_error(std::format("code access violation at loc {}: attempted to return to loc {}", this->registers[CP] - 1, return_addr));

    // pop stack frame
    while (this->registers[ST] > this->registers[LB]) this->PopData();
    assert(this->registers[ST] == this->registers[LB]);

    // pop arguments
    for (int I = 0; I < instr.d; ++I) this->PopData();
    assert(this->registers[ST] == this->registers[LB] - instr.d);

    // push result
    for (int I = 0; I < instr.n; ++I) {
        assert(!return_val.empty());
        this->PushData(return_val.top());
        return_val.pop();
    }
    assert(return_val.empty());

    this->registers[LB] = dynamic_link;
    assert(this->registers[LB] == dynamic_link);
    this->registers[CP] = return_addr;
    assert(this->registers[CP] == return_addr);
}

void TamEmulator::ExecutePush(TamInstruction instr)
{
    if (this->registers[ST] + instr.d >= this->registers[HT])
        throw RuntimeError(ExceptionKind::kStackOverflow,
                           this->registers[CT] - 1);

    this->registers[ST] += instr.d;
}

void TamEmulator::ExecutePop(TamInstruction instr)
{
    std::stack<TamData> data;
    for (int i = 0; i < instr.n; ++i) data.push(this->PopData());

    for (int i = 0; i < instr.d; ++i) this->PopData();

    while (!data.empty()) {
        this->PushData(data.top());
        data.pop();
    }

    assert(data.empty());
}

void TamEmulator::ExecuteJump(TamInstruction instr)
{
    TamAddr addr = this->registers[instr.r] + instr.d;
    if (addr >= this->registers[CT])
        throw std::runtime_error(std::format("code access violation at loc {}: attempted to jump to loc {}", this->registers[CP] - 1, addr));

    this->registers[CP] = addr;
    assert(this->registers[CP] == addr);
}

void TamEmulator::ExecuteJumpi(TamInstruction instr)
{
    TamAddr addr = this->PopData();
    if (addr >= this->registers[CT])
        throw std::runtime_error(std::format("code access violation at loc {}: attempted to jump to loc {}", this->registers[CP] - 1, addr));

    this->registers[CP] = addr;
    assert(this->registers[CP] == addr);
}

void TamEmulator::ExecuteJumpif(TamInstruction instr)
{
    TamData value = this->PopData();
    if (value != instr.n) return;

    assert(value == instr.n);

    TamAddr addr = this->registers[instr.r] + instr.d;
    if (addr >= this->registers[CT])
        throw std::runtime_error(std::format("code access violation at loc {}: attempted to jump to loc {}", this->registers[CP] - 1, addr));

    this->registers[CP] = addr;
    assert(this->registers[CP] == addr);
}

std::string GetMnemonic(TamInstruction instr)
{
    std::stringstream ss;
    switch (instr.op) {
        case LOAD:
        case STORE:
        case CALL:
        case JUMPIF:
            // CALL put
            if (instr.op == CALL && instr.r == PB) {
                ss << "CALL " << primitive_names[instr.d];
                return ss.str();
            }

            // OPCODE(n) d[r]
            ss << opcode_names[instr.op] << "(" << instr.n << ") " << instr.d
               << "[" << register_names[instr.r] << "]";
            return ss.str();

        case LOADA:
        case JUMP:
            // OPCODE d[r]
            ss << opcode_names[instr.op] << " " << instr.d << "["
               << register_names[instr.r] << "]";
            return ss.str();

        case RETURN:
        case POP:
            // OPCODE(n) d
            ss << opcode_names[instr.op] << "(" << instr.n << ") " << instr.d;
            return ss.str();

        case LOADI:
        case STOREI:
            // OPCODE (n)
            ss << opcode_names[instr.op] << " (" << instr.n << ")";
            return ss.str();

        case LOADL:
        case PUSH:
            // OPCODE d
            ss << opcode_names[instr.op] << " " << instr.d;
            return ss.str();

        case CALLI:
        case JUMPI:
        case HALT:
            // OPCODE
            return opcode_names[instr.op];

        default:
            return "INVALID";
    }
}

inline std::ostream& operator<<(std::ostream& os, uint8_t v)
{
    return os << static_cast<unsigned int>(v);
}

}  // namespace tam
