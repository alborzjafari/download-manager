#include "https_downloader.h"

#include <unistd.h>

#include <arpa/inet.h>
#include <openssl/err.h>

#include <cstring>
#include <iostream>

#include "https_socket_ops.h"

using namespace std;

HttpsDownloader::~HttpsDownloader()
{
}

HttpsDownloader::HttpsDownloader(const struct DownloadSource& download_source)
  : HttpDownloader(download_source)
{
}

SSL* HttpsDownloader::get_ssl(BIO* bio)
{
  SSL* ssl = nullptr;
  BIO_get_ssl(bio, &ssl);
  if (ssl == nullptr)
    cerr << "Error occurred when getting ssl." << endl;
  return ssl;
}

bool HttpsDownloader::receive_data(Connection& connection, char* buffer,
                                   size_t& received_len,
                                   size_t buffer_capacity)
{
  bool retval = false;

  SSL* ssl = dynamic_cast<HttpsSocketOps*>(
      connection.socket_ops.get())->get_ssl();

  int len = SSL_read(ssl, buffer, buffer_capacity);
  if (len < 0)
    cerr << "error SSL_read" << endl;
  else if (len > 0) 
    retval = true;
  else 
    cerr << "empty BIO_read" << endl;

  received_len = len;

  return retval;
}

bool HttpsDownloader::receive_data(Connection& connection, Buffer& buffer)
{
  bool retval = false;

  SSL* ssl = dynamic_cast<HttpsSocketOps*>(
      connection.socket_ops.get())->get_ssl();

  int rec_len = SSL_read(ssl, buffer, buffer.capacity());
  if (rec_len < 0) {
    buffer.clear();
    cerr << "error SSL_read" << endl;
  }
  else if (rec_len > 0) {
    retval = true;
    buffer.set_length(rec_len);
  }
  else {
    buffer.clear();
    cerr << "empty BIO_read" << endl;
  }

  return retval;
}
bool HttpsDownloader::send_data(Connection& connection, const char* buffer,
                                size_t len)
{
  BIO* bio = dynamic_cast<HttpsSocketOps*>(
      connection.socket_ops.get())->get_bio();

  BIO_write(bio, buffer, len);
  BIO_flush(bio);

  return true;
}

bool HttpsDownloader::init_connection(Connection& connection)
{
    string& host = download_source.host_name;
    uint16_t port = download_source.port;

    connection.socket_ops = make_unique<HttpsSocketOps>(host, port);
    return connection.socket_ops->connect();
}
