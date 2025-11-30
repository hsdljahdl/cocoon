#include "Ed25519.h"
#include "block.h"
#include "common/bitstring.h"
#include "runners/smartcontracts/SmartContract.hpp"
#include "td/utils/SharedSlice.h"
#include "td/utils/logging.h"
#include "td/utils/OptionParser.h"
#include "td/utils/misc.h"
#include "runners/smartcontracts/CocoonWallet.hpp"

int main(int argc, char **argv) {
  SET_VERBOSITY_LEVEL(verbosity_INFO);

  block::StdAddress owner_address;
  td::Bits256 public_key;

  td::OptionParser options;

  options.set_description("util to generate cocoon wallet address");
  options.add_checked_option('o', "wallet-owner", "owner of wallet", [&](td::Slice arg) -> td::Status {
    if (!owner_address.rdeserialize(arg)) {
      return td::Status::Error("cannot parse owner address");
    }
    return td::Status::OK();
  });
  options.add_checked_option('p', "public-key", "machine public key (in hex format)", [&](td::Slice arg) {
    if (arg.size() != 64) {
      return td::Status::Error("public key must have exactly 64 hexadecimal digits");
    }

    TRY_RESULT(r, td::hex_decode(arg));
    public_key.as_slice().copy_from(r);
    return td::Status::OK();
  });
  options.add_checked_option('P', "private-key", "machine private key (in hex format)", [&](td::Slice arg) {
    if (arg.size() != 64) {
      return td::Status::Error("private key must have exactly 64 hexadecimal digits");
    }

    TRY_RESULT(r, td::hex_decode(arg));

    td::Ed25519::PrivateKey pk{td::SecureString(r)};
    TRY_RESULT(pubk, pk.get_public_key());
    public_key.as_slice().copy_from(pubk.as_octet_string().as_slice());
    return td::Status::OK();
  });
  options.add_checked_option('h', "help", "Show help", [&]() {
    LOG(ERROR) << options;
    std::exit(0);
    return td::Status::OK();
  });

  auto S = options.run(argc, argv);
  if (S.is_error()) {
    LOG(ERROR) << "Parse error: " << S.error();
    LOG(ERROR) << options;
    return 1;
  }

  auto code = cocoon::CocoonWallet::code_boc();
  auto data = cocoon::CocoonWallet::init_data_cell(owner_address, public_key);

  auto addr = cocoon::TonScWrapper::generate_address(code, data, owner_address.testnet);

  std::cout << "cocoon wallet address is " << addr.rserialize(true);
  return 0;
}
