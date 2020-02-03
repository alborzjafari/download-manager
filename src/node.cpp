#include <unistd.h>
#include <sys/stat.h>

#include <regex>
#include <iomanip>
#include <iostream>

#include "node.h"
#include "url_info.h"
#include "ftp_downloader.h"
#include "http_downloader.h"
#include "https_downloader.h"

void Node::wait()
{
  for (map<int, Downloader*>::iterator it = download_threads.begin();
      it != download_threads.end(); ++it)
    it->second->join();
}

void Node::run()
{
  check_url_details();
  ++node_index;
  on_get_file_stat(node_index, file_length, &dwl_str);
  size_t trd_norm_len = file_length / num_of_trds;
  string log_file;

  node_data.node = this;

  if(check_resume()) {  // Resuming download
    DownloadChunks threads_log = logger.get_threads_data();
    size_t total_received_bytes = 0;
    for (auto thread_log : threads_log) {
      size_t downloaded_bytes =
        thread_log.second.second - thread_log.second.first;
      size_t length = trd_norm_len - downloaded_bytes;
      if (thread_log.first == (num_of_trds - 1))
        length = file_length - (length * thread_log.first);
      download_chunks[thread_log.first] =
        make_pair(thread_log.second.second, length);

      total_received_bytes +=
        thread_log.second.second - thread_log.second.first;
    }

    on_status_changed(-1, file_length, total_received_bytes, &dwl_str);
  }
  else {  // Not resuming download
    for (int i = 0; i < num_of_trds; i++) {
      size_t position = i * trd_norm_len;
      size_t length = trd_norm_len;
      if (i == (num_of_trds - 1)) {
        length = file_length - (trd_norm_len * i);
        position = i * trd_norm_len;
      }
      download_chunks[i] = make_pair(position, length);
      logger.write_first_position(i, position);
    }
  }

  Downloader* downloader;
  switch(dwl_str.protocol){
    case kHttp:
      if (dwl_str.encrypted ) { // https
        for (auto it : download_chunks) {
          downloader = new HttpsDownloader(file_io, logger, &node_data,
              dwl_str, it.second.first, it.second.second, it.first);
          downloader->start();
          download_threads[it.first] = downloader;
        }
      }
      else {  // http
        for (auto it : download_chunks) {
          downloader = new HttpDownloader(file_io, logger, &node_data,
              dwl_str, it.second.first, it.second.second, it.first);
          downloader->start();
          download_threads[it.first] = downloader;
        }
      }
      break;
    case kFtp:
      for (auto it : download_chunks) {
        downloader = new FtpDownloader(file_io, logger, &node_data,
            dwl_str, it.second.first, it.second.second, it.first);
        downloader->start();
        download_threads[it.first] = downloader;
      }
      break;
  }

  wait();

  for (auto it = download_threads.begin(); it != download_threads.end(); ++it)
    delete it->second;
  logger.remove();
}

void Node::on_get_status(struct addr_struct* addr_data,
    int downloader_trd_index, size_t total_trd_len, size_t received_bytes,
    int stat_flag)
{
  auto download_threads_it = download_threads.find(downloader_trd_index);
  if (download_threads_it != download_threads.end()) {
    total_received_bytes += received_bytes;
    on_status_changed(downloader_trd_index, total_trd_len, received_bytes,
        addr_data);
  }
}

void Node::check_url_details()
{
  Downloader* check_info_downloader;

  while (true) {
    if (dwl_str.protocol == kHttp)
      if (dwl_str.encrypted)
        check_info_downloader = new HttpsDownloader(file_io, logger,
            &node_data, dwl_str, 0, 0, 0);
      else
        check_info_downloader = new HttpDownloader(file_io, logger,
            &node_data, dwl_str, 0, 0, 0);
    else if (dwl_str.protocol == kFtp)
      check_info_downloader = new FtpDownloader(file_io, logger,
          &node_data, dwl_str, 0, 0, 0);

    string redirected_url;
    bool redirection =
      check_info_downloader->check_link(redirected_url, file_length);
    if (redirection) {
      URLInfo u_info(redirected_url);
      struct addr_struct dl_str = u_info.get_download_info();
      dwl_str = dl_str;
      delete check_info_downloader;
    }
    else
      break;
  }
  delete check_info_downloader;
}

bool Node::check_resume()
{
  bool remuse_state{true};
  string file_name = dwl_str.file_name_on_server;
  string log_file = "." + file_name + ".LOG";
  FileIO logger_file("." + file_name + ".LOG");

  if (!(file_io.check_existence() && logger_file.check_existence()))
    remuse_state = false;

  if (!remuse_state) {
    file_io.create(file_length);
    logger.create(file_length);
  }
  else {
    file_io.open();
    logger.open();
  }
  return remuse_state;
}

size_t Node::node_index = 0;
