#include "WebSocketServer.h"

#include <cstdlib>
#include <iostream>
#include <string>

#include <boost/asio.hpp>

namespace {

bool hasArg(int argc, char* argv[], const std::string& name) {
  for (int i = 1; i < argc; ++i) {
    if (argv[i] == name) {
      return true;
    }
  }
  return false;
}

unsigned short readPort(int argc, char* argv[]) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string(argv[i]) == "--port" || std::string(argv[i]) == "-p") {
      return static_cast<unsigned short>(std::stoi(argv[i + 1]));
    }
  }
  return 48010;
}

} // namespace

int main(int argc, char* argv[]) {
  if (hasArg(argc, argv, "--version") || hasArg(argc, argv, "-v")) {
    std::cout << "ComtradeScope Native Backend 0.1.0\n";
    return 0;
  }

  if (hasArg(argc, argv, "--help") || hasArg(argc, argv, "-h") || hasArg(argc, argv, "/?")) {
    std::cout
      << "用法：comtradescope-backend.exe [--port 48010]\n"
      << "选项：\n"
      << "  -p, --port <port>  WebSocket 监听端口，默认 48010。\n"
      << "  -v, --version      输出后端版本。\n"
      << "  -h, --help         输出帮助信息。\n";
    return 0;
  }

  try {
    const unsigned short port = readPort(argc, argv);
    boost::asio::io_context io;
    auto server = std::make_shared<WebSocketServer>(io, port);
    if (!server->listen()) {
      std::cerr << "WebSocket 服务启动失败，端口：" << port << "\n";
      return 2;
    }

    std::cout << "ComtradeScope Native backend listening on ws://127.0.0.1:" << port << "\n";
    io.run();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "后端异常：" << error.what() << "\n";
    return 1;
  }
}
