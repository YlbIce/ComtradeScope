#include "WebSocketServer.h"

#include <chrono>
#include <filesystem>
#include <iostream>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace websocket = boost::beast::websocket;
using tcp = boost::asio::ip::tcp;
using protocol::Json;

namespace {

std::string dumpJson(const Json& message) {
  return message.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string requestIdOf(const Json& command) {
  return command.value("requestId", "");
}

Json backendInfo(unsigned short port) {
  return {
    {"name", "ComtradeScope Native Backend"},
    {"version", "0.1.0"},
    {"wsPort", port},
    {"features", {
      "COMTRADE CFG/DAT 解析",
      "ASCII/BINARY/BINARY32/FLOAT32 基础读取",
      "波形降采样",
      "RMS/峰值/基波相量/序分量",
      "数字量变位分析"
    }}
  };
}

} // namespace

WebSocketServer::WebSocketServer(asio::io_context& io, unsigned short port)
  : io_(io),
    acceptor_(io),
    port_(port) {
}

bool WebSocketServer::listen() {
  boost::system::error_code ec;
  acceptor_.open(tcp::v4(), ec);
  if (ec) return false;

  acceptor_.set_option(asio::socket_base::reuse_address(true), ec);
  if (ec) return false;

  acceptor_.bind(tcp::endpoint(asio::ip::make_address("127.0.0.1"), port_), ec);
  if (ec) return false;

  acceptor_.listen(asio::socket_base::max_listen_connections, ec);
  if (ec) return false;

  acceptNext();
  return true;
}

void WebSocketServer::acceptNext() {
  auto self = shared_from_this();
  acceptor_.async_accept(asio::make_strand(io_), [this, self](boost::system::error_code ec, tcp::socket socket) {
    if (!ec) {
      auto session = std::make_shared<WebSocketSession>(std::move(socket), self, port_);
      clients_.insert(session);
      session->start();
    }
    acceptNext();
  });
}

void WebSocketServer::broadcast(const Json& message) {
  const std::string text = dumpJson(message);
  for (const auto& client : clients_) {
    client->send(text);
  }
}

void WebSocketServer::routeCommand(const std::shared_ptr<WebSocketSession>& client, const std::string& message) {
  Json command;
  try {
    command = Json::parse(message);
  } catch (...) {
    sendTo(client, protocol::makeError("", "WebSocket 消息必须是 JSON 对象"));
    return;
  }

  if (!command.is_object()) {
    sendTo(client, protocol::makeError("", "WebSocket 消息必须是 JSON 对象"));
    return;
  }

  const std::string requestId = requestIdOf(command);
  const std::string type = command.value("type", "");
  const Json payload = command.value("payload", Json::object());

  if (type == "backend:info") {
    sendTo(client, protocol::makeOk(requestId, "backend:info:result", backendInfo(port_)));
    return;
  }

  if (type == "comtrade:load") {
    try {
      const auto cfgPath = std::filesystem::path(payload.value("cfgPath", ""));
      const auto datPath = std::filesystem::path(payload.value("datPath", ""));
      const std::size_t maxPoints = static_cast<std::size_t>(std::max(100, payload.value("maxPoints", 5000)));
      if (cfgPath.empty()) {
        sendTo(client, protocol::makeError(requestId, "必须提供 CFG 文件路径"));
        return;
      }

      auto record = parser_.load(cfgPath, datPath);
      auto result = analyzer_.buildLoadResult(record, maxPoints);
      sendTo(client, protocol::makeOk(requestId, "comtrade:load:result", result));
    } catch (const std::exception& error) {
      sendTo(client, protocol::makeError(requestId, error.what()));
    }
    return;
  }

  if (type == "backend:shutdown") {
    sendTo(client, protocol::makeOk(requestId, "backend:shutdown:result", {{"ok", true}}));
    auto timer = std::make_shared<asio::steady_timer>(io_, std::chrono::milliseconds(150));
    timer->async_wait([this, timer](boost::system::error_code) {
      boost::system::error_code ignored;
      acceptor_.close(ignored);
      io_.stop();
    });
    return;
  }

  sendTo(client, protocol::makeError(requestId, "未知命令：" + type));
}

void WebSocketServer::remove(const std::shared_ptr<WebSocketSession>& client) {
  clients_.erase(client);
}

void WebSocketServer::sendTo(const std::shared_ptr<WebSocketSession>& client, const Json& message) {
  if (client) {
    client->send(dumpJson(message));
  }
}

WebSocketSession::WebSocketSession(TcpSocket socket, std::weak_ptr<WebSocketServer> server, unsigned short port)
  : ws_(std::move(socket)),
    server_(std::move(server)),
    port_(port) {
}

void WebSocketSession::start() {
  auto self = shared_from_this();
  ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
  ws_.set_option(websocket::stream_base::decorator([](websocket::response_type& response) {
    response.set(beast::http::field::server, "ComtradeScope Native Backend");
  }));

  ws_.async_accept([this, self](boost::system::error_code ec) {
    if (ec) {
      if (auto server = server_.lock()) {
        server->remove(self);
      }
      return;
    }

    send(dumpJson({
      {"type", "backend:hello"},
      {"payload", backendInfo(port_)}
    }));

    readNext();
  });
}

void WebSocketSession::send(const std::string& message) {
  auto self = shared_from_this();
  asio::post(ws_.get_executor(), [this, self, message] {
    const bool busy = !outgoing_.empty();
    outgoing_.push_back(message);
    if (!busy) {
      writeNext();
    }
  });
}

void WebSocketSession::readNext() {
  auto self = shared_from_this();
  ws_.async_read(buffer_, [this, self](boost::system::error_code ec, std::size_t) {
    if (ec) {
      if (auto server = server_.lock()) {
        server->remove(self);
      }
      return;
    }

    const std::string message = beast::buffers_to_string(buffer_.data());
    buffer_.consume(buffer_.size());
    if (auto server = server_.lock()) {
      server->routeCommand(self, message);
    }
    readNext();
  });
}

void WebSocketSession::writeNext() {
  if (outgoing_.empty()) {
    return;
  }

  auto self = shared_from_this();
  ws_.text(true);
  ws_.async_write(asio::buffer(outgoing_.front()), [this, self](boost::system::error_code ec, std::size_t) {
    if (ec) {
      if (auto server = server_.lock()) {
        server->remove(self);
      }
      return;
    }

    outgoing_.pop_front();
    writeNext();
  });
}
