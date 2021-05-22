#ifndef _DOWNLOADER_H
#define _DOWNLOADER_H

#include <map>
#include <vector>
#include <functional>

#include <openssl/bio.h>

#include "thread.h"
#include "file_io.h"
#include "socket_ops.h"
#include "http_proxy.h"
#include "state_manager.h"
#include "request_manager.h"
#include "http_transceiver.h"
#include "connection_manager.h"

using CallBack = std::function<void(size_t)>;

enum class OperationStatus {
  ERROR,
  NOT_STARTED,
  DOWNLOADING,
  FINISHED,
  TIMEOUT,
  HTTP_ERROR,
  HTTP_SEL_ERROR,
  FTP_ERROR,
  SSL_ERROR,
  RESPONSE_ERROR,
  SOCKFD_ERROR,
  SOCKET_SEND_ERROR,
  SOCKET_RECV_ERROR,
  SOCKET_CONNECT_ERROR
};

struct Connection {
  Connection() : status(OperationStatus::NOT_STARTED)
    , bio(nullptr)
    , ssl(nullptr)
    , last_recv_time_point(std::chrono::steady_clock::now())
    , http_proxy(nullptr)
    , header_skipped(false)
    , inited(false)
    , request_sent(false)
  {
  }

  enum class Status {
    FAILED,
    SUCCEED,
    REJECTED,
    NEW_PART_NOT_AVAILABLE
  };

  OperationStatus status;
  Chunk chunk;
  BIO* bio;
  SSL* ssl;
  // Used for http, https and ftp command channel.
  std::unique_ptr<SocketOps> socket_ops;
  // Used for ftp media channel.
  std::unique_ptr<SocketOps> ftp_media_socket_ops;
  std::chrono::steady_clock::time_point last_recv_time_point;
  std::string temp_http_header;
  std::unique_ptr<HttpProxy> http_proxy;
  bool header_skipped;
  bool inited;
  bool request_sent;
};

class Downloader : public Thread {
  public:
    const static std::string HTTP_HEADER;

    Downloader(std::unique_ptr<RequestManager> request_manager,
               std::shared_ptr<StateManager> state_manager,
               std::unique_ptr<FileIO> file_io);

    /**
     * Check the size of file and redirection
     *
     * @param redirect_url: Will be filled with redirected url if redirection
     *                      exist.
     * @param size: Will be filled with size of file if exist.
     *
     * @return 1 if link is redirected otherwise 0, in case of error
     *  it will return -1
     */

    // TODO: remove empty function.
    virtual int check_link(std::string& redirect_url, size_t& size) {};

    void use_http_proxy(std::string proxy_host, uint16_t proxy_port);

    void register_callback(CallBack callback);

    void set_speed_limit(size_t speed_limit);

    void set_download_parts(std::queue<std::pair<size_t, Chunk>> initial_parts);

    void set_parts(uint16_t parts);

  protected:
    bool regex_search_string(const std::string& input,
                             const std::string& pattern,
                             std::string& output, int pos_of_pattern = 2);

    bool regex_search_string(const std::string& input,
                             const std::string& pattern);

    void run() override;

    virtual bool receive_data(Connection& connection, Buffer& buffer);

    virtual bool send_data(const Connection& connection, const Buffer& buffer);

    // TODO: remove all empty functions.
    virtual bool send_requests() {};

    virtual bool send_request(Connection& connection) {};
    // Return max_fd
    virtual int set_descriptors();

    virtual void receive_from_connection(size_t index, Buffer& buffer);

    virtual void init_connections();

    virtual void init_connection(size_t connection_index);

    virtual Connection::Status create_connection(bool info_connection=false);

    virtual std::vector<int> check_timeout();
    // Retry download in connections
    virtual void retry(const std::vector<int>& connection_indices);

    std::unique_ptr<FileIO> file_io;
    std::shared_ptr<StateManager> state_manager;
    time_t timeout_seconds;
    // <index, connection> [index: same as part index]
    std::map<size_t, Connection> connections;
    fd_set readfds;
    uint16_t number_of_parts;

  private:
    std::unique_ptr<RequestManager> request_manager;
    CallBack callback;
    // <index, chunk>
    std::queue<std::pair<size_t, Chunk>> initial_parts;

    struct NewAvailPart {
      uint16_t part_index;
      std::unique_ptr<SocketOps> sock_ops;
    };
    // part index, socket
    std::mutex new_available_parts_mutex;
    std::queue<NewAvailPart> new_available_parts;

    struct RateParams {
      size_t limit = 0;
      size_t speed = 0;
      size_t total_recv_bytes = 0;
      size_t total_limiter_bytes = 0;
      size_t last_overall_recv_bytes = 0;
      std::chrono::steady_clock::time_point last_recv_time_point;
    } rate;

    void rate_process(RateParams& rate, size_t recvd_bytes);

    void update_connection_stat(size_t recvd_bytes, size_t index);

    void survey_connections();

    // <index, socket_ops object>
    void on_dwl_available(uint16_t index,
                          std::unique_ptr<SocketOps> socket_ops);

    void check_new_sock_ops();
    
    std::unique_ptr<HttpTransceiver> transceiver;
};

#endif
