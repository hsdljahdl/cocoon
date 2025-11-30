#pragma once
#include "td/net/SslCtx.h"
#include "td/utils/crypto.h"
#include "td/utils/format.h"
#include "td/utils/UInt.h"
#include "td/utils/Variant.h"
#include "td/e2e/Keys.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cocoon {
class AttestationCache;
}

// Forward declarations to reduce compilation dependencies
namespace tde2e_core {
class PrivateKey;
class PublicKey;
}  // namespace tde2e_core

namespace tdx {

/**
 * @brief Custom OID constants for TDX extensions
 * 
 * Using private enterprise OID space: 1.3.6.1.4.1
 * We use 1.3.6.1.4.1.12345.x for our custom extensions
 */
namespace OID {
constexpr td::CSlice TDX_QUOTA = "1.3.6.1.4.1.12345.1";
constexpr td::CSlice TDX_USER_CLAIMS = "1.3.6.1.4.1.12345.2";
}  // namespace OID

/**
 * @brief Structure to hold extracted TDX measurement fields
 * 
 * Contains all the measurement registers and report data from a TDX attestation
 */
struct TdxAttestationData {
  td::UInt384 mr_td;                ///< Measurement of initial TD contents
  td::UInt384 mr_config_id;         ///< Software-defined config ID
  td::UInt384 mr_owner;             ///< TD owner identifier
  td::UInt384 mr_owner_config;      ///< Owner-defined config
  std::array<td::UInt384, 4> rtmr;  ///< Runtime measurement registers [0..3]
  td::UInt512 reportdata;           ///< User-defined report data

  template <class StorerT>
  void store(StorerT &storer) const {
    using td::store;
    store(mr_td, storer);
    store(mr_config_id, storer);
    store(mr_owner, storer);
    store(mr_owner_config, storer);
    for (size_t i = 0; i < 4; i++) {
      store(rtmr[i], storer);
    }
    store(reportdata, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using td::parse;
    parse(mr_td, parser);
    parse(mr_config_id, parser);
    parse(mr_owner, parser);
    parse(mr_owner_config, parser);
    for (size_t i = 0; i < 4; i++) {
      parse(rtmr[i], parser);
    }
    parse(reportdata, parser);
  }
};

/**
 * @brief Structure to hold extracted SGX measurement fields
 * 
 * Contains enclave measurements and report data from SGX attestation
 */
struct SgxAttestationData {
  //TODO: Add more SGX-specific fields
  td::UInt256 mr_enclave;  ///< Measurement of enclave
  td::UInt512 reportdata;  ///< User-defined report data

  template <class StorerT>
  void store(StorerT &storer) const {
    using td::store;
    store(mr_enclave, storer);
    store(reportdata, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using td::parse;
    parse(mr_enclave, parser);
    parse(reportdata, parser);
  }
};

/**
 * @brief Unified attestation data structure
 * 
 * Wraps either TDX or SGX attestation data, or can be empty.
 * Replaces the need for separate QuoteInfo wrapper.
 */
struct AttestationData {
  enum class Type { None, Tdx, Sgx };

  AttestationData() = default;  // Creates empty/None state
  explicit AttestationData(TdxAttestationData tdx) : data_(std::move(tdx)) {
  }
  explicit AttestationData(SgxAttestationData sgx) : data_(std::move(sgx)) {
  }
  void set_collateral_root_hash(td::UInt384 hash) {
    collateral_root_hash_ = hash;
  }

  Type type() const {
    if (!data_) {
      return Type::None;
    }
    return data_->get_offset() == 0 ? Type::Tdx : Type::Sgx;
  }

  bool is_empty() const {
    return type() == Type::None;
  }
  bool is_tdx() const {
    return type() == Type::Tdx;
  }
  bool is_sgx() const {
    return type() == Type::Sgx;
  }
  explicit operator bool() const {
    return !is_empty();
  }

  // Checked access (CHECK fails if wrong type)
  const TdxAttestationData &as_tdx() const {
    CHECK(is_tdx());
    return data_->get<TdxAttestationData>();
  }

  const SgxAttestationData &as_sgx() const {
    CHECK(is_sgx());
    return data_->get<SgxAttestationData>();
  }

  TdxAttestationData &as_tdx() {
    CHECK(is_tdx());
    return data_->get<TdxAttestationData>();
  }

  SgxAttestationData &as_sgx() {
    CHECK(is_sgx());
    return data_->get<SgxAttestationData>();
  }

  // Pointer-safe access (returns nullptr if wrong type or empty)
  const TdxAttestationData *tdx() const {
    return is_tdx() ? &data_->get<TdxAttestationData>() : nullptr;
  }

  const SgxAttestationData *sgx() const {
    return is_sgx() ? &data_->get<SgxAttestationData>() : nullptr;
  }

  TdxAttestationData *tdx() {
    return is_tdx() ? &data_->get<TdxAttestationData>() : nullptr;
  }

  SgxAttestationData *sgx() {
    return is_sgx() ? &data_->get<SgxAttestationData>() : nullptr;
  }

  td::UInt256 image_hash() const;
  td::UInt384 collateral_root_hash() const {
    return collateral_root_hash_;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using td::store;
    store(data_.has_value(), storer);
    if (data_) {
      store(*data_, storer);
    }
    store(collateral_root_hash_, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using td::parse;
    bool has_value;
    parse(has_value, parser);
    if (has_value) {
      td::Variant<TdxAttestationData, SgxAttestationData> variant;
      parse(variant, parser);
      data_ = std::move(variant);
    } else {
      data_ = std::nullopt;
    }
    parse(collateral_root_hash_, parser);
  }

 private:
  td::UInt384 collateral_root_hash_{};
  std::optional<td::Variant<TdxAttestationData, SgxAttestationData>> data_;

  friend td::StringBuilder &operator<<(td::StringBuilder &sb, const AttestationData &data);
};

td::StringBuilder &operator<<(td::StringBuilder &sb, const SgxAttestationData &data);
td::StringBuilder &operator<<(td::StringBuilder &sb, const TdxAttestationData &data);
td::StringBuilder &operator<<(td::StringBuilder &sb, const AttestationData &data);

/**
 * @brief User claims structure for attestation
 * 
 * Contains user-specific data that will be included in attestation reports
 */
struct UserClaims {
  tde2e_core::PublicKey public_key;  ///< User's public key
  // TODO: Add other claims like user ID, permissions, etc.

  /**
   * @brief Generate hash of user claims for inclusion in attestation
   * @return SHA-512 hash of serialized claims
   */
  td::UInt512 to_hash() const;

  /**
   * @brief Serialize user claims to string format
   * @return Serialized claims as string
   */
  std::string serialize() const;
};

/**
 * @brief Raw quote data from TDX/SGX attestation
 */
struct Quote {
  std::string raw_quote;  ///< Raw binary quote data
};

/**
 * @brief Raw report data from TDX attestation
 */
struct Report {
  std::string raw_report;  ///< Raw binary report data
};

// Forward declaration
struct TdxInterface;
using TdxInterfaceRef = std::shared_ptr<const TdxInterface>;

/**
 * @brief Interface for TDX/SGX attestation operations
 * 
 * Provides abstraction over different attestation mechanisms (TDX, SGX, fake)
 */
struct TdxInterface {
  virtual ~TdxInterface() = default;

  /**
   * @brief Extract attestation data from a quote
   * @param quota The quote to extract data from
   * @return Attestation data or error
   */
  virtual td::Result<AttestationData> get_data(const Quote &quota) const = 0;

  /**
   * @brief Extract attestation data from a report
   * @param report The report to extract data from
   * @return Attestation data or error
   */
  virtual td::Result<AttestationData> get_data(const Report &report) const = 0;

  /**
   * @brief Generate a new quote with user claims hash
   * @param user_claims_hash Hash of user claims to include
   * @return Generated quote or error
   */
  virtual td::Result<Quote> make_quote(td::UInt512 user_claims_hash) const = 0;

  /**
   * @brief Generate a new report with user claims hash
   * @param user_claims_hash Hash of user claims to include
   * @return Generated report or error
   */
  virtual td::Result<Report> make_report(td::UInt512 user_claims_hash) const = 0;

  /**
   * @brief Validate a quote and extract attestation data
   * @param quote The quote to validate
   * @return Attestation data or error
   */
  virtual td::Result<AttestationData> validate_quote(const Quote &quote) const = 0;

  /**
   * @brief Create a fake TDX interface for testing
   * @return Shared pointer to fake interface
   */
  static TdxInterfaceRef create_fake();

  // Wrap an existing TdxInterface with caching layer
  static TdxInterfaceRef add_cache(TdxInterfaceRef inner, std::shared_ptr<cocoon::AttestationCache> cache);

  /**
   * @brief Create a real TDX interface
   * @return Shared pointer to real interface
   */
  static TdxInterfaceRef create();
};

// Forward declaration
struct Policy;
using PolicyRef = std::shared_ptr<const Policy>;

/**
 * @brief Policy configuration for attestation validation
 */
struct PolicyConfig {
  // TDX measurement validation
  std::vector<td::UInt384> allowed_mrtd;                 ///< Allowed MRTD values (empty = any)
  std::vector<std::array<td::UInt384, 4>> allowed_rtmr;  ///< Allowed RTMR sets (empty = any)

  // Image hash verification
  std::vector<td::UInt256> allowed_image_hashes;  ///< Expected image hashes (empty = no verification)

  // Collateral root hash verification (Intel DCAP root key IDs)
  std::vector<td::UInt384> allowed_collateral_root_hashes;  ///< Allowed Intel root key IDs (empty = any)
};

td::StringBuilder &operator<<(td::StringBuilder &sb, const PolicyConfig &config);

/**
 * @brief Policy interface for attestation validation
 * 
 * Defines validation rules for quotes and user claims
 */
struct Policy {
  virtual ~Policy() = default;

  /**
   * @brief Validate quote and user claims against policy
   * @param quote Optional quote to validate
   * @param user_claims User claims to validate
   * @return AttestationData which may be empty if no attestation was performed
   */
  virtual td::Result<AttestationData> validate(const Quote *quote, const UserClaims &user_claims) const = 0;

  /**
   * @brief Create a policy instance with default configuration
   * @param tdx TDX interface to use for validation
   * @return Shared pointer to policy
   */
  static PolicyRef make(TdxInterfaceRef tdx);

  /**
   * @brief Create a policy instance with custom configuration
   * @param tdx TDX interface to use for validation
   * @param config Policy configuration
   * @return Shared pointer to policy
   */
  static PolicyRef make(TdxInterfaceRef tdx, PolicyConfig config);
};

struct CertConfig {
  std::string country = "AE";  // ISO 3166-1 alpha-2 country code for UAE (must be exactly 2 characters)
  std::string state = "DUBAI";
  std::string locality = "";
  std::string organization = "TDLib Development";
  std::string organizational_unit = "Security";
  std::string common_name = "localhost";
  std::vector<std::string> san_names = {"localhost", "127.0.0.1", "::1"};
  td::uint32 validity_seconds = 86400;  // Default: 1 day
  std::vector<std::pair<std::string, std::string>> extra_extensions;
  std::optional<td::uint32> current_time;  // If set, use this time instead of system time for certificate generation
};

td::Result<std::string> generate_self_signed_cert(const tde2e_core::PrivateKey &private_key,
                                                  const CertConfig &config = {});

td::Result<std::string> generate_tdx_self_signed_cert(const tde2e_core::PrivateKey &private_key, CertConfig config,
                                                      const UserClaims &user_claims, const TdxInterface &tdx);

class CertAndKey {
 public:
  CertAndKey() = default;
  CertAndKey(std::string cert_pem, std::string key_pem);

  const std::string &cert_pem() const;
  const std::string &key_pem() const;

 private:
  struct Impl {
    std::string cert_pem;
    std::string key_pem;
  };
  std::shared_ptr<const Impl> impl_;
};

CertAndKey generate_cert_and_key(const TdxInterface *tdx, const CertConfig &config = {});
td::Result<CertAndKey> load_cert_and_key(td::Slice name);

struct VerifyCallbackBuilder {
  static std::function<int(int, void *)> from_policy(PolicyRef policy);
};

struct SslCtxFree {
  void operator()(void *ptr) const;
};
using SslCtxHolder = std::unique_ptr<void, SslCtxFree>;
struct SslOptions {
  enum class Mode { Server, Client } mode{Mode::Client};
  CertAndKey cert_and_key;
  std::function<int(int, void *)> custom_verify;
};
td::Result<SslCtxHolder> create_ssl_ctx(SslOptions options);

}  // namespace tdx
