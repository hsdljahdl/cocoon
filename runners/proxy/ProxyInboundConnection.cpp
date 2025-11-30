#include "ProxyInboundConnection.h"
#include "ProxyRunner.hpp"

namespace cocoon {

ProxyRunner *ProxyInboundConnection::runner() {
  return static_cast<ProxyRunner *>(BaseInboundConnection::runner());
}

}  // namespace cocoon
