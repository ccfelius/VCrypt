#pragma once
#include "simple_encryption/common.hpp"
#include "simple_encryption_state.hpp"
#include "duckdb/main/client_context.hpp"

namespace simple_encryption {

namespace core {

// Get VCryptState from ClientContext
shared_ptr<VCryptState>
GetSimpleEncryptionState(ClientContext &context);
} // namespace core

} // namespace simple_encryption
