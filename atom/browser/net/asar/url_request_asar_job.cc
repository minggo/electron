// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "atom/browser/net/asar/url_request_asar_job.h"

#include <string>
#include <vector>

#include "atom/common/asar/archive.h"
#include "atom/common/asar/asar_util.h"
#include "atom/common/asar/asar_crypto.h"
#include "atom/common/atom_constants.h"
#include "base/bind.h"
#include "base/files/file_util.h"
#include "base/strings/string_util.h"
#include "base/synchronization/lock.h"
#include "base/task_runner.h"
#include "net/base/file_stream.h"
#include "net/base/filename_util.h"
#include "net/base/io_buffer.h"
#include "net/base/load_flags.h"
#include "net/base/mime_util.h"
#include "net/base/net_errors.h"
#include "net/filter/filter.h"
#include "net/http/http_util.h"
#include "net/url_request/url_request_status.h"

#if defined(OS_WIN)
#include "base/win/shortcut.h"
#endif

namespace asar {

URLRequestAsarJob::FileMetaInfo::FileMetaInfo()
    : file_size(0),
      mime_type_result(false),
      file_exists(false),
      is_directory(false) {
}

URLRequestAsarJob::URLRequestAsarJob(
    net::URLRequest* request,
    net::NetworkDelegate* network_delegate)
    : net::URLRequestJob(request, network_delegate),
      type_(TYPE_ERROR),
      remaining_bytes_(0),
      weak_ptr_factory_(this),
      decipher_(nullptr){}

URLRequestAsarJob::~URLRequestAsarJob() {
  delete decipher_;
}

void URLRequestAsarJob::Initialize(
    const scoped_refptr<base::TaskRunner> file_task_runner,
    const base::FilePath& file_path) {
  // Determine whether it is an asar file.
  base::FilePath asar_path, relative_path;
  if (!GetAsarArchivePath(file_path, &asar_path, &relative_path)) {
    InitializeFileJob(file_task_runner, file_path);
    return;
  }

  std::shared_ptr<Archive> archive = GetOrCreateAsarArchive(asar_path);
  Archive::FileInfo file_info;
  if (!archive || !archive->GetFileInfo(relative_path, &file_info)) {
    type_ = TYPE_ERROR;
    return;
  }

  if (file_info.unpacked) {
    base::FilePath real_path;
    archive->CopyFileOut(relative_path, &real_path);
    InitializeFileJob(file_task_runner, real_path);
    return;
  }

  InitializeAsarJob(file_task_runner, archive, relative_path, file_info);
}

void URLRequestAsarJob::InitializeAsarJob(
    const scoped_refptr<base::TaskRunner> file_task_runner,
    std::shared_ptr<Archive> archive,
    const base::FilePath& file_path,
    const Archive::FileInfo& file_info) {
  type_ = TYPE_ASAR;
  file_task_runner_ = file_task_runner;
  stream_.reset(new net::FileStream(file_task_runner_));
  archive_ = archive;
  file_path_ = file_path;
  file_info_ = file_info;

  if (base::LowerCaseEqualsASCII(file_path_.Extension(), ".js"))
    decipher_ = CipherBase::CreateDecipher();
}

void URLRequestAsarJob::InitializeFileJob(
    const scoped_refptr<base::TaskRunner> file_task_runner,
    const base::FilePath& file_path) {
  type_ = TYPE_FILE;
  file_task_runner_ = file_task_runner;
  stream_.reset(new net::FileStream(file_task_runner_));
  file_path_ = file_path;
}

void URLRequestAsarJob::Start() {
  if (type_ == TYPE_ASAR) {
    remaining_bytes_ = static_cast<int64>(file_info_.size);

    int flags = base::File::FLAG_OPEN |
                base::File::FLAG_READ |
                base::File::FLAG_ASYNC;
    int rv = stream_->Open(archive_->path(), flags,
                           base::Bind(&URLRequestAsarJob::DidOpen,
                                      weak_ptr_factory_.GetWeakPtr()));
    if (rv != net::ERR_IO_PENDING)
      DidOpen(rv);
  } else if (type_ == TYPE_FILE) {
    FileMetaInfo* meta_info = new FileMetaInfo();
    file_task_runner_->PostTaskAndReply(
        FROM_HERE,
        base::Bind(&URLRequestAsarJob::FetchMetaInfo, file_path_,
                   base::Unretained(meta_info)),
        base::Bind(&URLRequestAsarJob::DidFetchMetaInfo,
                   weak_ptr_factory_.GetWeakPtr(),
                   base::Owned(meta_info)));
  } else {
    NotifyStartError(net::URLRequestStatus(net::URLRequestStatus::FAILED,
                                           net::ERR_FILE_NOT_FOUND));
  }
}

void URLRequestAsarJob::Kill() {
  stream_.reset();
  weak_ptr_factory_.InvalidateWeakPtrs();

  URLRequestJob::Kill();
}

bool URLRequestAsarJob::ReadRawData(net::IOBuffer* dest,
                                    int dest_size,
                                    int* bytes_read) {
  if (remaining_bytes_ < dest_size)
    dest_size = static_cast<int>(remaining_bytes_);

  // If we should copy zero bytes because |remaining_bytes_| is zero, short
  // circuit here.
  if (!dest_size) {
    *bytes_read = 0;
    return true;
  }

  int rv = stream_->Read(dest,
                         dest_size,
                         base::Bind(&URLRequestAsarJob::DidRead,
                                    weak_ptr_factory_.GetWeakPtr(),
                                    make_scoped_refptr(dest)));

  if (rv >= 0) {
    // Data is immediately available.
    *bytes_read = rv;
    remaining_bytes_ -= rv;
    DCHECK_GE(remaining_bytes_, 0);
    
    // decrypt js files
    if (type_ == TYPE_ASAR && 
        base::LowerCaseEqualsASCII(file_path_.Extension(), ".js") &&
        rv > 0) {
      DecryptData(dest, rv);
    }

    return true;
  }

  // Otherwise, a read error occured.  We may just need to wait...
  if (rv == net::ERR_IO_PENDING) {
    SetStatus(net::URLRequestStatus(net::URLRequestStatus::IO_PENDING, 0));
  } else {
    NotifyDone(net::URLRequestStatus(net::URLRequestStatus::FAILED, rv));
  }
  return false;
}

bool URLRequestAsarJob::IsRedirectResponse(GURL* location,
                                           int* http_status_code) {
  if (type_ != TYPE_FILE)
    return false;
#if defined(OS_WIN)
  // Follow a Windows shortcut.
  // We just resolve .lnk file, ignore others.
  if (!base::LowerCaseEqualsASCII(file_path_.Extension(), ".lnk"))
    return false;

  base::FilePath new_path = file_path_;
  bool resolved;
  resolved = base::win::ResolveShortcut(new_path, &new_path, NULL);

  // If shortcut is not resolved succesfully, do not redirect.
  if (!resolved)
    return false;

  *location = net::FilePathToFileURL(new_path);
  *http_status_code = 301;
  return true;
#else
  return false;
#endif
}

net::Filter* URLRequestAsarJob::SetupFilter() const {
  // Bug 9936 - .svgz files needs to be decompressed.
  return base::LowerCaseEqualsASCII(file_path_.Extension(), ".svgz")
      ? net::Filter::GZipFactory() : NULL;
}

bool URLRequestAsarJob::GetMimeType(std::string* mime_type) const {
  if (type_ == TYPE_ASAR) {
    return net::GetMimeTypeFromFile(file_path_, mime_type);
  } else {
    if (meta_info_.mime_type_result) {
      *mime_type = meta_info_.mime_type;
      return true;
    }
    return false;
  }
}

void URLRequestAsarJob::SetExtraRequestHeaders(
    const net::HttpRequestHeaders& headers) {
  std::string range_header;
  if (headers.GetHeader(net::HttpRequestHeaders::kRange, &range_header)) {
    // We only care about "Range" header here.
    std::vector<net::HttpByteRange> ranges;
    if (net::HttpUtil::ParseRangeHeader(range_header, &ranges)) {
      if (ranges.size() == 1) {
        byte_range_ = ranges[0];
      } else {
        NotifyDone(net::URLRequestStatus(
            net::URLRequestStatus::FAILED,
            net::ERR_REQUEST_RANGE_NOT_SATISFIABLE));
      }
    }
  }
}

int URLRequestAsarJob::GetResponseCode() const {
  // Request Job gets created only if path exists.
  return 200;
}

void URLRequestAsarJob::GetResponseInfo(net::HttpResponseInfo* info) {
  std::string status("HTTP/1.1 200 OK");
  net::HttpResponseHeaders* headers = new net::HttpResponseHeaders(status);

  headers->AddHeader(atom::kCORSHeader);
  info->headers = headers;
}

void URLRequestAsarJob::FetchMetaInfo(const base::FilePath& file_path,
                                      FileMetaInfo* meta_info) {
  base::File::Info file_info;
  meta_info->file_exists = base::GetFileInfo(file_path, &file_info);
  if (meta_info->file_exists) {
    meta_info->file_size = file_info.size;
    meta_info->is_directory = file_info.is_directory;
  }
  // On Windows GetMimeTypeFromFile() goes to the registry. Thus it should be
  // done in WorkerPool.
  meta_info->mime_type_result =
      net::GetMimeTypeFromFile(file_path, &meta_info->mime_type);
}

void URLRequestAsarJob::DidFetchMetaInfo(const FileMetaInfo* meta_info) {
  meta_info_ = *meta_info;
  if (!meta_info_.file_exists || meta_info_.is_directory) {
    DidOpen(net::ERR_FILE_NOT_FOUND);
    return;
  }

  int flags = base::File::FLAG_OPEN |
              base::File::FLAG_READ |
              base::File::FLAG_ASYNC;
  int rv = stream_->Open(file_path_, flags,
                         base::Bind(&URLRequestAsarJob::DidOpen,
                                    weak_ptr_factory_.GetWeakPtr()));
  if (rv != net::ERR_IO_PENDING)
    DidOpen(rv);
}

void URLRequestAsarJob::DidOpen(int result) {
  if (result != net::OK) {
    NotifyDone(net::URLRequestStatus(net::URLRequestStatus::FAILED, result));
    return;
  }

  if (type_ == TYPE_ASAR) {
    int rv = stream_->Seek(file_info_.offset,
                           base::Bind(&URLRequestAsarJob::DidSeek,
                                      weak_ptr_factory_.GetWeakPtr()));
    if (rv != net::ERR_IO_PENDING) {
      // stream_->Seek() failed, so pass an intentionally erroneous value
      // into DidSeek().
      DidSeek(-1);
    }
  } else {
    if (!byte_range_.ComputeBounds(meta_info_.file_size)) {
      NotifyDone(net::URLRequestStatus(net::URLRequestStatus::FAILED,
                 net::ERR_REQUEST_RANGE_NOT_SATISFIABLE));
      return;
    }

    remaining_bytes_ = byte_range_.last_byte_position() -
                       byte_range_.first_byte_position() + 1;

    if (remaining_bytes_ > 0 && byte_range_.first_byte_position() != 0) {
      int rv = stream_->Seek(byte_range_.first_byte_position(),
                             base::Bind(&URLRequestAsarJob::DidSeek,
                                        weak_ptr_factory_.GetWeakPtr()));
      if (rv != net::ERR_IO_PENDING) {
        // stream_->Seek() failed, so pass an intentionally erroneous value
        // into DidSeek().
        DidSeek(-1);
      }
    } else {
      // We didn't need to call stream_->Seek() at all, so we pass to DidSeek()
      // the value that would mean seek success. This way we skip the code
      // handling seek failure.
      DidSeek(byte_range_.first_byte_position());
    }
  }
}

void URLRequestAsarJob::DidSeek(int64 result) {
  if (type_ == TYPE_ASAR) {
    if (result != static_cast<int64>(file_info_.offset)) {
      NotifyDone(net::URLRequestStatus(net::URLRequestStatus::FAILED,
                                       net::ERR_REQUEST_RANGE_NOT_SATISFIABLE));
      return;
    }
  } else {
    if (result != byte_range_.first_byte_position()) {
      NotifyDone(net::URLRequestStatus(net::URLRequestStatus::FAILED,
                                       net::ERR_REQUEST_RANGE_NOT_SATISFIABLE));
      return;
    }
  }
  set_expected_content_size(remaining_bytes_);
  NotifyHeadersComplete();
}

void URLRequestAsarJob::DidRead(scoped_refptr<net::IOBuffer> buf, int result) {
  if (result > 0) {
    SetStatus(net::URLRequestStatus());  // Clear the IO_PENDING status
    remaining_bytes_ -= result;
    DCHECK_GE(remaining_bytes_, 0);
  }

  // decrypt js files
  if (type_ == TYPE_ASAR && 
      base::LowerCaseEqualsASCII(file_path_.Extension(), ".js") && 
      result > 0) {
    DecryptData(buf.get(), result);
  }

  buf = NULL;

  if (result == 0) {
    NotifyDone(net::URLRequestStatus());
  } else if (result < 0) {
    NotifyDone(net::URLRequestStatus(net::URLRequestStatus::FAILED, result));
  }

  NotifyReadComplete(result);
}

void URLRequestAsarJob::DecryptData(net::IOBuffer *buf, int size) {
  if (remaining_bytes_ == 0)
    decipher_->Update(buf->data(), size, true);
  else
    decipher_->Update(buf->data(), size, false);
}

}  // namespace asar
