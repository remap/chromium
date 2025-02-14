// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_NETWORK_P2P_SOCKET_TCP_SERVER_H_
#define SERVICES_NETWORK_P2P_SOCKET_TCP_SERVER_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <vector>

#include "base/compiler_specific.h"
#include "base/component_export.h"
#include "base/macros.h"
#include "net/base/completion_repeating_callback.h"
#include "net/socket/tcp_server_socket.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/p2p/socket.h"
#include "services/network/public/cpp/p2p_socket_type.h"

namespace net {
class StreamSocket;
}  // namespace net

namespace network {
class P2PSocketTcpBase;

class COMPONENT_EXPORT(NETWORK_SERVICE) P2PSocketTcpServer : public P2PSocket {
 public:
  P2PSocketTcpServer(P2PSocketManager* socket_manager,
                     mojom::P2PSocketClientPtr client,
                     mojom::P2PSocketRequest socket,
                     P2PSocketType client_type);
  ~P2PSocketTcpServer() override;

  // P2PSocket overrides.
  bool Init(const net::IPEndPoint& local_address,
            uint16_t min_port,
            uint16_t max_port,
            const P2PHostAndIPEndPoint& remote_address) override;

  // mojom::P2PSocket implementation:
  void AcceptIncomingTcpConnection(const net::IPEndPoint& remote_address,
                                   mojom::P2PSocketClientPtr client,
                                   mojom::P2PSocketRequest request) override;
  void Send(const std::vector<int8_t>& data,
            const P2PPacketInfo& packet_info,
            const net::MutableNetworkTrafficAnnotationTag& traffic_annotation)
      override;
  void SetOption(P2PSocketOption option, int32_t value) override;

 private:
  friend class P2PSocketTcpServerTest;

  std::unique_ptr<P2PSocketTcpBase> AcceptIncomingTcpConnectionInternal(
      const net::IPEndPoint& remote_address,
      mojom::P2PSocketClientPtr client,
      mojom::P2PSocketRequest request);

  void OnError();

  void DoAccept();
  void HandleAcceptResult(int result);

  // Callback for Accept().
  void OnAccepted(int result);

  const P2PSocketType client_type_;
  std::unique_ptr<net::ServerSocket> socket_;
  net::IPEndPoint local_address_;

  std::unique_ptr<net::StreamSocket> accept_socket_;
  std::map<net::IPEndPoint, std::unique_ptr<net::StreamSocket>>
      accepted_sockets_;

  const net::CompletionRepeatingCallback accept_callback_;

  DISALLOW_COPY_AND_ASSIGN(P2PSocketTcpServer);
};

}  // namespace network

#endif  // SERVICES_NETWORK_P2P_SOCKET_TCP_SERVER_H_
