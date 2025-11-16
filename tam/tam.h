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
/// @file tam.h
/// This file declares the core of the TAM interface. It declares the
/// `TamEmulator` class itself, along with the `TamInstruction` struct used to
/// pass instructions and additional useful constants.
//
//===-----------------------------------------------------------------------===//

#ifndef TAM_TAM_H__
#define TAM_TAM_H__

#include <stdint.h>
#include <stdio.h>

#include <array>
#include <map>
#include <string>
#include <vector>

namespace tam {

typedef uint32_t TamCode;
typedef int16_t TamData;
typedef uint16_t TamAddr;

/// Number of addressable words in memory.
///
constexpr const int kMemSize = 65536;

/// Index of highest-addressed word in memory.
///
constexpr const int kMaxAddr = kMemSize - 1;

/// TAM registers, also indexes into our registers array.
///
enum TamRegister {
    CB = 0,   // Code Base
    CT = 1,   // Code Top
    PB = 2,   // Primitives Base
    PT = 3,   // Primitives Top
    SB = 4,   // Stack Base
    ST = 5,   // Stack Top
    HB = 6,   // Heap Base
    HT = 7,   // Heap Top
    LB = 8,   // Local Base
    L1 = 9,   // Local Base 1
    L2 = 10,  // Local Base 2
    L3 = 11,  // Local Base 3
    L4 = 12,  // Local Base 4
    L5 = 13,  // Local Base 5
    L6 = 14,  // Local Base 6
    CP = 15   // Code Pointer
};

/// TAM operations.
///
enum TamOpCode {
    LOAD = 0,
    LOADA = 1,
    LOADI = 2,
    LOADL = 3,
    STORE = 4,
    STOREI = 5,
    CALL = 6,
    CALLI = 7,
    RETURN = 8,
    // unused = 9,
    PUSH = 10,
    POP = 11,
    JUMP = 12,
    JUMPI = 13,
    JUMPIF = 14,
    HALT = 15
};

/// A single TAM instruction.
///
struct TamInstruction {
    uint8_t op;  ///< Opcode
    uint8_t r;   ///< Register
    uint8_t n;   ///< Unsigned operand
    int16_t d;   ///< Signed operand
};

// clang-format off

/// List of TAM primitive routine names.
///
/// Index into this array corresponds to the offset `d` of the
/// instruction for that primitive.
static const std::string primitive_names[] = {
    "0", // invalid
    "id",
    "not", "and", "or",
    "succ", "pred", "neg",
    "add", "sub", "mult", "div", "mod",
    "lt", "le",
    "ge", "gt",
    "eq", "ne",
    "eol", "eof",
    "get", "put",
    "geteol", "puteol",
    "getint", "putint",
    "new", "dispose"
};

/// List of TAM OpCode names.
///
/// Index into this array corresponds to `instr.op`.
static const std::string opcode_names[] = {
    "LOAD",
    "LOADA",
    "LOADI",
    "LOADL",
    "STORE",
    "STOREI",
    "CALL",
    "CALLI",
    "RETURN",
    "unused", // unused = 9
    "PUSH",
    "POP",
    "JUMP",
    "JUMPI",
    "JUMPIF",
    "HALT",
};

/// List of TAM register names.
///
/// Index into this array corresponds to `instr.r`.
static const std::string register_names[] = {
    "CB",
    "CT",
    "PB",
    "PT",
    "SB",
    "ST",
    "HB",
    "HT",
    "LB",
    "L1",
    "L2",
    "L3",
    "L4",
    "L5",
    "L6",
    "CP"
};
// clang-format on

/// A TAM emulator.
///
/// The emulator class is responsible for simulating all operations that would
/// happen on a physical CPU. Methods are provided for the fetch-decode-execute
/// cycle, and all methods will directly update registers and memory
/// appropriately.
class TamEmulator {
   public:
    /// Construct a new emulator that uses `stdin` and `stdout` for I/O.
    ///
    /// On creation, all memory is zeroed and registers are set to default
    /// values. Registers default to 0 except for `HB` and `HT`, which default
    /// to the highest address.
    TamEmulator() : TamEmulator(stdin, stdout) {}

    /// Construct a new emulator that uses the specified file streams for I/O.
    ///
    /// Members are initialised in the same manner as the default constructor.
    TamEmulator(FILE*, FILE*);

    /// Close the input and output streams if they are not stdin or stdout.
    ///
    ~TamEmulator() {
        if (this->instream != stdin) {
            fclose(this->instream);
        }

        if (this->outstream != stdout) {
            fclose(this->outstream);
        }
    }

    /// Sets the program to be run by this emulator.
    ///
    /// This method also sets `CT`, `PB`, `PT` based on the size of the program.
    ///
    /// @param program program code to load
    /// @throws std::runtime_error if the provided program is too large to
    /// fit in memory
    void LoadProgram(const std::vector<TamCode>& program);

    /// Obtains the next instruction to execute.
    ///
    /// This method increments the code pointer as part of its operation.
    ///
    /// @return the instruction
    /// @throws std::runtime_error if the code pointer pointed outside of
    /// allocated code memory
    TamInstruction FetchDecode();

    /// Executes the given instruction.
    ///
    /// @param instr instruction to execute
    /// @throws std::runtime_error if any error occurred during execution
    void Execute(TamInstruction instr);

    /// Return a string representing the current contents of the stack and any
    /// allocated blocks on the heap.
    ///
    /// @return the stack and heap contents
    const std::string GetSnapshot() const;

    /// Attempt to allocate some memory on the heap.
    ///
    /// @param n size of requested block
    /// @return address of first word in the block
    /// @throws std::runtime_error if there was no room to allocate the memory
    TamAddr Allocate(int n);

    /// Attempt to free the allocated block beginning at `addr`.
    ///
    /// @param addr start address of block
    /// @param size expected size of block
    void Free(TamAddr addr, TamData size);

    /// Push a value to the top of the stack and increment the `ST` register.
    ///
    /// @param value value to push
    /// @throws std::runtime_error if the push would cause a stack overflow
    void PushData(TamData value);

    /// Remove and return the top value of the stack and decrement the `ST`
    /// register.
    ///
    /// @return the data
    /// @throws std::runtime_error if the pop would cause a stack underflow
    TamData PopData();

    void ExecuteLoad(TamInstruction instr);
    void ExecuteLoada(TamInstruction instr);
    void ExecuteLoadi(TamInstruction instr);
    void ExecuteLoadl(TamInstruction instr);
    void ExecuteStore(TamInstruction instr);
    void ExecuteStorei(TamInstruction instr);
    void ExecuteCall(TamInstruction instr);
    void ExecuteCallPrimitive(TamInstruction instr);
    void ExecuteCalli(TamInstruction instr);
    void ExecuteReturn(TamInstruction instr);
    void ExecutePush(TamInstruction instr);
    void ExecutePop(TamInstruction instr);
    void ExecuteJump(TamInstruction instr);
    void ExecuteJumpi(TamInstruction instr);
    void ExecuteJumpif(TamInstruction instr);

    void PrimitiveNot();
    void PrimitiveAnd();
    void PrimitiveOr();
    void PrimitiveSucc();
    void PrimitivePred();
    void PrimitiveNeg();
    void PrimitiveAdd();
    void PrimitiveSub();
    void PrimitiveMult();
    void PrimitiveDiv();
    void PrimitiveMod();
    void PrimitiveLt();
    void PrimitiveLe();
    void PrimitiveGe();
    void PrimitiveGt();
    void PrimitiveEq();
    void PrimitiveNe();
    void PrimitiveEol();
    void PrimitiveEof();
    void PrimitiveGet();
    void PrimitivePut();
    void PrimitiveGeteol();
    void PrimitivePuteol();
    void PrimitiveGetint();
    void PrimitivePutint();
    void PrimitiveNew();
    void PrimitiveDispose();

    std::array<TamCode, kMemSize> code_store;  ///< Stores code words
    std::array<TamData, kMemSize> data_store;  ///< Stores data words
    std::array<TamAddr, 16> registers;         ///< Stores register values

    std::map<TamAddr, int>
        allocated_blocks,  ///< Records blocks of heap memory in use
        free_blocks;       ///< Records blocks of unused heap memory

    FILE *instream,  ///< File that input is read from
        *outstream;  ///< File that output is written to

    bool halted = false;

    std::string output;
};

/// Get Mnemonic of an instruction.
///
/// @param instr Instruction
/// @return Mnemonic of Instruction
std::string GetMnemonic(TamInstruction instr);

/// Make it so that `uint8_t`s added to string streams
/// are treated as integers and not characters.
///
inline std::ostream& operator<<(std::ostream& os, uint8_t v);

}  // namespace tam

#endif  // TAM_TAM_H__
