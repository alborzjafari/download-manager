#ifndef _NODE_H
#define _NODE_H

#include <map>

#include "thread.h"
#include "file_io.h"
#include "url_ops.h"
#include "downloader.h"
#include "download_state_manager.h"

class Node : public Thread {
  public:
    Node(const std::string& url, const std::string& optional_path = kCurrDir,
         uint16_t number_of_connections=1,
         long int timeout=DEFAULT_TIMEOUT_SECONDS);
    virtual void on_get_file_info(size_t node_index, size_t file_size,
                                  const std::string& file_name) {};

    // Set proxy if proxy_url is not empty.
    void set_proxy(std::string proxy_url);

  protected:
    // callback refresh interval in milliseconds
    size_t callback_refresh_interval = 500;

    virtual void on_data_received(size_t received_bytes, size_t speed) = 0;

  private:
    constexpr static char kCurrDir[] = "./";
    constexpr static time_t DEFAULT_TIMEOUT_SECONDS = 10;

    std::unique_ptr<Downloader> make_downloader(std::unique_ptr<Writer> writer);
    std::unique_ptr<Downloader> make_downloader();

    void run();
    void check_url();
    void check_download_state();
    DownloadSource make_download_source(UrlOps& url_ops);
    // Checks the downloading file existence and its LOG file.
    bool check_resume();
    std::string get_output_path(const std::string& optional_path,
                                const std::string& source_name);

    void on_data_received_node(size_t speed);

    std::unique_ptr<Downloader> downloader;
    ChunksCollection chunks_collection;
    std::shared_ptr<DownloadStateManager> download_state_manager;

    std::shared_ptr<FileIO> file_io;
    std::unique_ptr<FileIO> stat_file_io;

    size_t file_length;
    size_t total_received_bytes;
    static size_t node_index; // index of node

    UrlOps url_ops;
    std::string file_path;
    std::string optional_path;
    uint16_t number_of_connections;
    struct DownloadSource download_source;
    long int timeout;

    std::string proxy_url;
};

#endif
