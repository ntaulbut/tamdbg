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
/// @file error.cc
/// This file defines functions for creating TAM-related runtime errors.
//
//===-----------------------------------------------------------------------===//

#include "error.h"

#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace tam {


const std::runtime_error RuntimeError(ExceptionKind kind, uint16_t addr) {
    std::stringstream ss;

    switch (kind) {
        case ExceptionKind::kDataAccessViolation:
            ss << "data access violation";
            break;
        case ExceptionKind::kStackOverflow:
            ss << "stack overflow";
            break;
        case ExceptionKind::kStackUnderflow:
            ss << "stack underflow";
            break;
        case ExceptionKind::kUnknownOpcode:
            ss << "unknown opcode";
            break;
        case tam::ExceptionKind::kDivideByZero:
            ss << "divide by zero";
            break;
    }

    ss << " at loc " << addr;

    return std::runtime_error(ss.str());
}

const std::runtime_error IoError(const char* message) {
    std::stringstream ss;
    ss << "error: IO error: " << message;
    return std::runtime_error(ss.str());
}

}  // namespace tam
