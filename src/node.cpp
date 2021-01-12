#include <unistd.h>
#include <sys/stat.h>

#include <regex>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <functional>

#include "node.h"
#include "ftp_downloader.h"
#include "http_downloader.h"
#include "https_downloader.h"

using namespace std;

Node::Node(const string& url, const string& optional_path,
           uint16_t number_of_parts, long int timeout)
  : url_ops(url)
  , optional_path(optional_path)
  , number_of_parts(number_of_parts)
  , timeout(timeout)
  , resume(false)
{
  ++node_index;
}

void Node::run()
{
  // Get download_source
  check_url();
  on_get_file_info(node_index, file_length, download_source.file_name);
  file_path = get_output_path(optional_path, download_source.file_name);

  unique_ptr<FileIO> file_io = make_unique<FileIO>(file_path);

  shared_ptr<FileIO> stat_file_io = make_unique<FileIO>("." + file_path + ".stat");

  state_manager = make_shared<StateManager>(file_path);
  bool state_file_available = state_manager->state_file_available();

  if (resume && state_file_available) { // Resuming download
    state_manager->retrieve();
    file_io->open();
  }
  else {  // Not resuming download, create chunks collection
    file_io->create(file_length);
    state_manager->create_new_state(file_length);
  }

  if (number_of_parts == 1)
    state_manager->set_chunk_size(file_length);

  downloader = make_downloader(move(file_io));


  // Create and register callback
  CallBack callback = bind(&Node::on_data_received_node, this,
                           placeholders::_1);
  downloader->register_callback(callback);

  downloader->set_speed_limit(speed_limit);
  downloader->start();
  downloader->join();
}

void Node::check_url()
{
  while (true) {
    unique_ptr<Downloader> info_downloader = make_downloader();

    // Check if redirected
    string redirected_url;
    int check_link = info_downloader->check_link(redirected_url, file_length);
    if (check_link > 0)   // Redirected
      url_ops = UrlOps(redirected_url);
    else if (check_link < 0) {
      cerr << "Could not connect." << endl << "Exiting." << endl;
      exit(1);
    }
    else  // Not redirected.
      break;
  }
}

unique_ptr<Downloader> Node::make_downloader(unique_ptr<FileIO> file_io)
{
  unique_ptr<Downloader> downloader_obj;

  switch(download_source.protocol) {
    case Protocol::HTTP:
      downloader_obj = make_unique<HttpDownloader>(download_source,
                                                   move(file_io),
                                                   state_manager,
                                                   timeout,
                                                   number_of_parts);
      break;
    case Protocol::HTTPS:
      downloader_obj = make_unique<HttpsDownloader>(download_source,
                                                    move(file_io),
                                                    state_manager,
                                                    timeout,
                                                    number_of_parts);
      break;
    case Protocol::FTP:
      downloader_obj = make_unique<FtpDownloader>(download_source,
                                                  move(file_io),
                                                  state_manager,
                                                  timeout,
                                                  number_of_parts);
      break;
  }

  return downloader_obj;
}

DownloadSource Node::make_download_source(UrlOps& url_ops)
{
  if (!proxy_url.empty())
    url_ops.set_proxy(proxy_url);

  return {
    .ip = url_ops.get_ip(),
    .file_path = url_ops.get_path(),
    .file_name = url_ops.get_file_name(),
    .host_name = url_ops.get_hostname(),
    .protocol = url_ops.get_protocol(),
    .port = url_ops.get_port(),
    .proxy_ip = url_ops.get_proxy_ip(),
    .proxy_port = url_ops.get_proxy_port()
  };
}

unique_ptr<Downloader> Node::make_downloader()
{
  unique_ptr<Downloader> downloader_obj;

  download_source = make_download_source(url_ops);

  switch (download_source.protocol) {
    case Protocol::HTTP:
      downloader_obj = make_unique<HttpDownloader>(download_source);
      break;
    case Protocol::HTTPS:
      downloader_obj = make_unique<HttpsDownloader>(download_source);
      break;
    case Protocol::FTP:
      downloader_obj = make_unique<FtpDownloader>(download_source);
      break;
  }

  return downloader_obj;
}

string Node::get_output_path(const string& optional_path,
                             const string& source_name)
{
  string path;
  FileIO output_file(optional_path);

  if (output_file.check_existence()) {
    if (output_file.check_path_type() == PathType::DIRECTORY_T) {
      if (optional_path[optional_path.length() - 1] == '/')
        path = optional_path + source_name;
      else
        path = optional_path + "/" + source_name;
    }

    else if (output_file.check_path_type() == PathType::FILE_T)
      path = optional_path;
    else {
      cerr << "Unknown optional path, using default file name." << endl;
      path = source_name;
    }
  }
  else {
    if (!optional_path.empty())
      path = optional_path;
    else
      path = source_name;
  }

  return path;
}

void Node::set_proxy(string proxy_url)
{
  this->proxy_url = proxy_url;
}

void Node::set_speed_limit(size_t speed_limit)
{
  this->speed_limit = speed_limit;
}

void Node::set_resume(bool resume)
{
  this->resume = resume;
}

void Node::on_data_received_node(size_t speed)
{
  size_t total_received_bytes = 0;
  //total_received_bytes = download_state_manager->get_total_written_bytes();
  total_received_bytes = state_manager->get_total_recvd_bytes();
  on_data_received(total_received_bytes, speed);
}

size_t Node::node_index = 0;
