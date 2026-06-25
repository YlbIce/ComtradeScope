#pragma once

#include "ComtradeAnalyzer.h"
#include "ComtradeParser.h"
#include "ProtocolUtils.h"

#include <deque>
#include <memory>
#include <set>
#include <string>

#include <boost/asio.hpp>
#include <boost/beast.hpp>

class WebSocketSession;

class WebSocketServer final : public std::enable_shared_from_this<WebSocketServer> {
public:
  WebSocketServer(boost::asio::io_context& io, unsigned short port);

  bool listen();
  void broadcast(const protocol::Json& message);
  void routeCommand(const std::shared_ptr<WebSocketSession>& client, const std::string& message);
  void remove(const std::shared_ptr<WebSocketSession>& client);

private:
  void acceptNext();
  void sendTo(const std::shared_ptr<WebSocketSession>& client, const protocol::Json& message);

  boost::asio::io_context& io_;
  boost::asio::ip::tcp::acceptor acceptor_;
  comtrade::ComtradeParser parser_;
  comtrade::ComtradeAnalyzer analyzer_;
  std::set<std::shared_ptr<WebSocketSession>> clients_;
  unsigned short port_;
};

class WebSocketSession final : public std::enable_shared_from_this<WebSocketSession> {
public:
  using TcpSocket = boost::asio::ip::tcp::socket;
  using WebSocket = boost::beast::websocket::stream<TcpSocket>;

  WebSocketSession(TcpSocket socket, std::weak_ptr<WebSocketServer> server, unsigned short port);

  void start();
  void send(const std::string& message);

private:
  void readNext();
  void writeNext();

  WebSocket ws_;
  boost::beast::flat_buffer buffer_;
  std::weak_ptr<WebSocketServer> server_;
  unsigned short port_;
  std::deque<std::string> outgoing_;
};
