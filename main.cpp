#include <iostream>

#include "common/process_communications/TCPTransport.hpp"

int main() {
    using common::process_communications::tcp_transport::TcpTransport;

    TcpTransport transport{TcpTransport::TcpTransportConfig{.m_port = 9000U}};

    if (const auto listening = transport.start_listening(); !listening) {
        std::cerr << "failed to start listening\n";
        return 1;
    }

    std::cout << "Hello, World!" << std::endl;
    return 0;
}
