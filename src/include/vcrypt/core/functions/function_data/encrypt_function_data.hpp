#pragma once

#include "vcrypt/common.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/common/types.hpp"
#include "mbedtls_wrapper.hpp"

namespace vcrypt {
namespace core {

struct EncryptFunctionData : FunctionData {

  // Save the ClientContext
  ClientContext &context;
  // Save the Key
  string key_name;
  string key;
  LogicalType type;
  //  BoundStatement relation;
  // Encryption Util
  shared_ptr<EncryptionUtil> encryption_util;

  EncryptFunctionData(ClientContext &context, string key_name, LogicalType type) : context(context), key_name(key_name), type(type) {
    // generate encryption key and store
    key = GetKeyFromSecret(context, key_name);
    auto &config = DBConfig::GetConfig(context);

    if (config.encryption_util) {
      encryption_util = config.encryption_util;
    } else {
      encryption_util = make_shared_ptr<
          duckdb_mbedtls::MbedTlsWrapper::AESGCMStateMBEDTLSFactory>();
    }

  }

  static unique_ptr<FunctionData>
  EncryptBind(ClientContext &context, ScalarFunction &bound_function,
              vector<unique_ptr<Expression>> &arguments);

  unique_ptr<FunctionData> Copy() const override;
  bool Equals(const FunctionData &other_p) const override;
  string GetKeyFromSecret(ClientContext &context, string key_name);
  void CalculateOffset(uint32_t counter_val, uint32_t& increment);
};

} // namespace core
} // namespace vcrypt