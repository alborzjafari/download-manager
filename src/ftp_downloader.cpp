#include "ftp_downloader.h"

#include <regex>
#include <cstdlib>
#include <cassert>
#include <unistd.h>
#include <iostream>

#include <arpa/inet.h>

#include "node.h"

using namespace std;

FtpDownloader::FtpDownloader(const struct DownloadSource& download_source)
  : Downloader(download_source)
{
  create_connection(true);
}

FtpDownloader::FtpDownloader(const struct DownloadSource& download_source,
                             unique_ptr<FileIO> file_io,
                             shared_ptr<StateManager> state_manager,
                             long int timeout,
                             int number_of_parts)
  : Downloader(download_source, move(file_io), state_manager, timeout,
               number_of_parts)
{
}

int FtpDownloader::check_link(string& redirected_url, size_t& size)
{
  ftp_init(connections[0]);

  string reply;
  const string command = "SIZE " + download_source.file_name + "\r\n";
  send_ftp_command(connections[0], command, reply);

  stringstream reply_stream(reply);
  size_t status;

  reply_stream >> status >> size;

  return 0;
}

void FtpDownloader::ftp_init(Connection& connection, string username,
                             string password)
{
  const vector<string> init_commands = {
    "",
    "USER " + username + "\r\n",
    "PASS " + password + "\r\n",
    "TYPE I\r\n",
    "PWD\r\n"
  };

  string reply;
  for (const auto& command : init_commands)
    if (!send_ftp_command(connection, command, reply))
      cerr << "Error occurred: " << reply << endl;

  string path = download_source.file_path;
  string file_name = download_source.file_name;

  const string kCwdCommand = "CWD " + path + "\r\n";
  send_ftp_command(connection, kCwdCommand, reply);
}

void FtpDownloader::ftp_init(string username, string password)
{
  const vector<string> init_commands = {
    "",
    "USER " + username + "\r\n",
    "PASS " + password + "\r\n",
    "TYPE I\r\n",
    "PWD\r\n"
  };

  string reply;
  for (size_t index = 0; index < connections.size(); ++index) {
    Connection& connection = connections[index];

    for (const auto& command : init_commands)
      if (!send_ftp_command(connection, command, reply))
        cerr << "Error occurred: " << reply << endl;

    string path = download_source.file_path;
    string file_name = download_source.file_name;

    const string kCwdCommand = "CWD " + path + "\r\n";
    send_ftp_command(connection, kCwdCommand, reply);
  }
}

bool FtpDownloader::send_ftp_command(Connection& connection,
                                     const string& command, string& result)
{
  Buffer command_buffer;
  command_buffer << command;
  if (!send_data(connection, command_buffer))
    return false;

  //constexpr static size_t kHeaderCapacity = 1000;
  //char response[kHeaderCapacity];
  Buffer response(1000);

  while (true) {
    if (!receive_data(connection, response.seek(response.length()))) {
      cerr << "Socket receive error" << endl;
      break;
    }

    // RFC9559:
    // A reply is defined to contain the 3-digit code, followed by space.
    string status;
    string response_str(response, response.length());
    if (regex_search_string(response_str, "(\\d{3}\\s)(.*)")) {
      result = string(response, response.length());
      return true;
    }
  }
  return false;
}

vector<string> FtpDownloader::split_string(const string& buffer, char delimiter)
{
  stringstream buffer_stream(buffer);
  string token;
  vector<string> splited_strings;
  while (getline(buffer_stream, token, delimiter))
      splited_strings.push_back(token);
  return splited_strings;
}

pair<string, uint16_t> FtpDownloader::get_data_ip_port(const string& buffer)
{
  string ip_port_string;
  regex_search_string(buffer, "(\\d+,\\d+,\\d+,\\d+,\\d+,\\d+)",
                      ip_port_string, 1);

  vector<string> splitted_ip_port = split_string(ip_port_string, ',');

  string ip = splitted_ip_port[0] + "." + splitted_ip_port[1] + "." +
              splitted_ip_port[2] + "." + splitted_ip_port[3];

  string p1 = splitted_ip_port[4];
  string p2 = splitted_ip_port[5];

  uint16_t port = static_cast<uint8_t>(stoi(p1));
  port <<= 8;
  port |= static_cast<uint8_t>(stoi(p2));

  return make_pair(ip, port);
}

void FtpDownloader::open_data_channel(Connection& connection, const string& ip,
                                      uint16_t port)
{
  connection.ftp_media_socket_ops = make_unique<SocketOps>(ip, port);
  connection.ftp_media_socket_ops->connect();
}

bool FtpDownloader::send_request(Connection& connection)
{
  bool result = true;

  ftp_init(connection);

  string reply;
  pair<string, uint16_t> ip_port_pair;

  if (send_ftp_command(connection, "PASV\r\n", reply))
    ip_port_pair = get_data_ip_port(reply);
  else {
    cerr << "Error occurred: " << reply << endl;
    result = false;
  }

  string ip = ip_port_pair.first;
  uint16_t port = ip_port_pair.second;
  open_data_channel(connection, ip, port);

  const size_t kCurrentPos = connection.chunk.current;
  const string kRestCommand = "REST " + to_string(kCurrentPos) + "\r\n";
  if (!send_ftp_command(connection, kRestCommand, reply)) {
    cerr << "Error occurred: " << reply << endl;
    result = false;
  }

  const string kRetrCommand = "RETR " + download_source.file_name + "\r\n";
  if (!send_ftp_command(connection, kRetrCommand, reply)) {
    cerr << "Error occurred: " << reply << endl;
    result = false;
  }
  connection.request_sent = true;

  return result;
}

bool FtpDownloader::send_requests()
{
  bool result = true;
  for (auto& [index, connection] : connections)
    if (!connection.request_sent)
      result &= send_request(connection);

  return result;
}

int FtpDownloader::set_descriptors()
{
  int max_fd = 0;
  FD_ZERO(&readfds);
  for (auto& [index, connection] : connections) {
    size_t current_pos = connection.chunk.current;
    size_t end_pos = connection.chunk.end;
    // Check if chunk is completed
    if (current_pos < end_pos) {
      int sock_desc = connection.ftp_media_socket_ops->get_socket_descriptor();
      FD_SET(sock_desc, &readfds);
      max_fd = (max_fd < sock_desc) ? sock_desc : max_fd;
    }
  }

  return max_fd;
}

void FtpDownloader::receive_from_connection(size_t index, Buffer& buffer)
{
  Connection& connection = connections[index];
  int sock_desc = connection.ftp_media_socket_ops->get_socket_descriptor();
  buffer.clear();

  if (FD_ISSET(sock_desc, &readfds)) {  // read from the socket
    ftp_receive_data(connection, buffer);

    // Correct length of last received part for each chunk.
    size_t end_pos = connection.chunk.end;
    size_t current_pos = connection.chunk.current;
    int64_t redundant_bytes = current_pos + buffer.length() - end_pos;
    if (redundant_bytes > 0) {
      size_t new_length = buffer.length() - redundant_bytes;
      buffer.set_length(new_length);
    }
  }
}

bool FtpDownloader::ftp_receive_data(Connection& connection, Buffer& buffer)
{
  int sock_desc = connection.ftp_media_socket_ops->get_socket_descriptor();
  size_t recvd_bytes = recv(sock_desc, buffer, buffer.capacity(), 0);
  if (recvd_bytes < 0) {
    buffer.clear();
    connection.status = OperationStatus::SOCKET_RECV_ERROR;
    return false;
  }
  buffer.set_length(recvd_bytes);

  return true;
}
