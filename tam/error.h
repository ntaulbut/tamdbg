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
/// @file error.h
/// This file defines the interface for constructing TAM-related runtime
/// exceptions.
//
//===-----------------------------------------------------------------------===//

#ifndef TAM_ERROR_H__
#define TAM_ERROR_H__

#include <stdint.h>

#include <stdexcept>

namespace tam {

/// Enumerates all possible kinds of runtime error aside from I/O errors.
///
enum class ExceptionKind {
    kCodeAccessViolation,  ///< Attempt to access out-of-bounds address in
                           ///< code memory
    kDataAccessViolation,  ///< Attempt to access out-of-bounds address in
                           ///< data memory
    kStackUnderflow,       ///< Stack attempted to grow into the heap
    kStackOverflow,        ///< Stack attempted to shrink past 0
    kHeapOverflow,         ///< Heap attempted to grow into the stack
    kUnknownOpcode,        ///< An unrecognised opcode was given to execute
    kDivideByZero,         ///< There was an attempt to divide by 0
};

/// Construct a runtime error.
///
/// The error message will report the kind of error and the value of the code
/// pointer when the error occurred.
///
/// @param kind the kind of error to construct
/// @param addr address of the instruction that caused the error
const std::runtime_error RuntimeError(ExceptionKind kind, uint16_t addr);

/// Construct a runtime error relating to an I/O problem.
///
/// This function direcly constructs a runtime error using the provided
/// message.
///
/// @param message specific cause of the error
const std::runtime_error IoError(const char* message);

}  // namespace tam
#endif  // TAM_ERROR_H__
