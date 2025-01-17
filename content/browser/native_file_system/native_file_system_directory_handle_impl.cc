// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/native_file_system/native_file_system_directory_handle_impl.h"

#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "content/browser/native_file_system/native_file_system_transfer_token_impl.h"
#include "net/base/escape.h"
#include "storage/browser/fileapi/file_system_context.h"
#include "storage/browser/fileapi/file_system_operation_runner.h"
#include "storage/common/fileapi/file_system_util.h"
#include "third_party/blink/public/mojom/native_file_system/native_file_system_error.mojom.h"
#include "third_party/blink/public/mojom/native_file_system/native_file_system_file_handle.mojom.h"
#include "third_party/blink/public/mojom/native_file_system/native_file_system_transfer_token.mojom.h"

using blink::mojom::NativeFileSystemEntry;
using blink::mojom::NativeFileSystemEntryPtr;
using blink::mojom::NativeFileSystemError;
using blink::mojom::NativeFileSystemHandle;
using blink::mojom::NativeFileSystemTransferTokenPtr;
using blink::mojom::NativeFileSystemTransferTokenRequest;

namespace content {

namespace {

// Returns true when |name| contains a path separator like "/".
bool ContainsPathSeparator(const std::string& name) {
  const base::FilePath filepath_name = storage::StringToFilePath(name);

  const size_t separator_position =
      filepath_name.value().find_first_of(base::FilePath::kSeparators);

  return separator_position != base::FilePath::StringType::npos;
}

// Returns true when |name| is "." or "..".
bool IsCurrentOrParentDirectory(const std::string& name) {
  const base::FilePath filepath_name = storage::StringToFilePath(name);
  return filepath_name.value() == base::FilePath::kCurrentDirectory ||
         filepath_name.value() == base::FilePath::kParentDirectory;
}

}  // namespace

struct NativeFileSystemDirectoryHandleImpl::ReadDirectoryState {
  GetEntriesCallback callback;
  std::vector<NativeFileSystemEntryPtr> entries;
};

NativeFileSystemDirectoryHandleImpl::NativeFileSystemDirectoryHandleImpl(
    NativeFileSystemManagerImpl* manager,
    const BindingContext& context,
    const storage::FileSystemURL& url,
    storage::IsolatedContext::ScopedFSHandle file_system)
    : NativeFileSystemHandleBase(manager,
                                 context,
                                 url,
                                 std::move(file_system)) {}

NativeFileSystemDirectoryHandleImpl::~NativeFileSystemDirectoryHandleImpl() =
    default;

void NativeFileSystemDirectoryHandleImpl::GetFile(const std::string& name,
                                                  bool create,
                                                  GetFileCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  storage::FileSystemURL child_url;
  const base::File::Error file_error = GetChildURL(name, &child_url);
  if (file_error != base::File::FILE_OK) {
    std::move(callback).Run(NativeFileSystemError::New(file_error), nullptr);
    return;
  }

  if (create) {
    operation_runner()->CreateFile(
        child_url, /*exclusive=*/false,
        base::BindOnce(&NativeFileSystemDirectoryHandleImpl::DidGetFile,
                       weak_factory_.GetWeakPtr(), child_url,
                       std::move(callback)));
  } else {
    operation_runner()->FileExists(
        child_url,
        base::BindOnce(&NativeFileSystemDirectoryHandleImpl::DidGetFile,
                       weak_factory_.GetWeakPtr(), child_url,
                       std::move(callback)));
  }
}

void NativeFileSystemDirectoryHandleImpl::GetDirectory(
    const std::string& name,
    bool create,
    GetDirectoryCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  storage::FileSystemURL child_url;
  const base::File::Error file_error = GetChildURL(name, &child_url);
  if (file_error != base::File::FILE_OK) {
    std::move(callback).Run(NativeFileSystemError::New(file_error), nullptr);
    return;
  }

  if (create) {
    operation_runner()->CreateDirectory(
        child_url, /*exclusive=*/false, /*recursive=*/false,
        base::BindOnce(&NativeFileSystemDirectoryHandleImpl::DidGetDirectory,
                       weak_factory_.GetWeakPtr(), child_url,
                       std::move(callback)));
  } else {
    operation_runner()->DirectoryExists(
        child_url,
        base::BindOnce(&NativeFileSystemDirectoryHandleImpl::DidGetDirectory,
                       weak_factory_.GetWeakPtr(), child_url,
                       std::move(callback)));
  }
}

void NativeFileSystemDirectoryHandleImpl::GetEntries(
    GetEntriesCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  operation_runner()->ReadDirectory(
      url(), base::BindRepeating(
                 &NativeFileSystemDirectoryHandleImpl::DidReadDirectory,
                 weak_factory_.GetWeakPtr(),
                 base::Owned(new ReadDirectoryState{std::move(callback)})));
}

void NativeFileSystemDirectoryHandleImpl::MoveFrom(
    NativeFileSystemTransferTokenPtr source,
    const std::string& name,
    MoveFromCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  manager()->ResolveTransferToken(
      std::move(source),
      base::BindOnce(&NativeFileSystemDirectoryHandleImpl::DoCopyOrMoveFrom,
                     weak_factory_.GetWeakPtr(), name, /*is_copy=*/false,
                     std::move(callback)));
}

void NativeFileSystemDirectoryHandleImpl::CopyFrom(
    NativeFileSystemTransferTokenPtr source,
    const std::string& name,
    CopyFromCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  manager()->ResolveTransferToken(
      std::move(source),
      base::BindOnce(&NativeFileSystemDirectoryHandleImpl::DoCopyOrMoveFrom,
                     weak_factory_.GetWeakPtr(), name, /*is_copy=*/true,
                     std::move(callback)));
}

void NativeFileSystemDirectoryHandleImpl::Remove(bool recurse,
                                                 RemoveCallback callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  operation_runner()->Remove(
      url(), recurse,
      base::BindOnce(
          [](RemoveCallback callback, base::File::Error result) {
            std::move(callback).Run(NativeFileSystemError::New(result));
          },
          std::move(callback)));
}

void NativeFileSystemDirectoryHandleImpl::Transfer(
    NativeFileSystemTransferTokenRequest token) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  manager()->CreateTransferToken(*this, std::move(token));
}

void NativeFileSystemDirectoryHandleImpl::DidGetFile(storage::FileSystemURL url,
                                                     GetFileCallback callback,
                                                     base::File::Error result) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  if (result != base::File::FILE_OK) {
    std::move(callback).Run(NativeFileSystemError::New(result), nullptr);
    return;
  }

  std::move(callback).Run(
      NativeFileSystemError::New(base::File::FILE_OK),
      manager()->CreateFileHandle(context(), url, file_system()));
}

void NativeFileSystemDirectoryHandleImpl::DidGetDirectory(
    storage::FileSystemURL url,
    GetDirectoryCallback callback,
    base::File::Error result) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  if (result != base::File::FILE_OK) {
    std::move(callback).Run(NativeFileSystemError::New(result), nullptr);
    return;
  }

  std::move(callback).Run(
      NativeFileSystemError::New(base::File::FILE_OK),
      manager()->CreateDirectoryHandle(context(), url, file_system()));
}

void NativeFileSystemDirectoryHandleImpl::DidReadDirectory(
    ReadDirectoryState* state,
    base::File::Error result,
    std::vector<filesystem::mojom::DirectoryEntry> file_list,
    bool has_more) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  if (result != base::File::FILE_OK) {
    DCHECK(!has_more);
    std::move(state->callback).Run(NativeFileSystemError::New(result), {});
    return;
  }

  for (const auto& entry : file_list) {
    std::string name = storage::FilePathToString(entry.name);

    storage::FileSystemURL child_url;
    const base::File::Error file_error = GetChildURL(name, &child_url);

    // All entries must exist in this directory as a direct child with a valid
    // |name|.
    CHECK_EQ(file_error, base::File::FILE_OK);

    state->entries.push_back(
        CreateEntry(name, child_url,
                    entry.type == filesystem::mojom::FsFileType::DIRECTORY));
  }

  // TODO(mek): Change API so we can stream back entries as they come in, rather
  // than waiting till we have retrieved them all.
  if (!has_more) {
    std::move(state->callback)
        .Run(NativeFileSystemError::New(base::File::FILE_OK),
             std::move(state->entries));
  }
}

void NativeFileSystemDirectoryHandleImpl::DoCopyOrMoveFrom(
    const std::string& new_name,
    bool is_copy,
    CopyOrMoveCallback callback,
    NativeFileSystemTransferTokenImpl* source) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  if (!source) {
    std::move(callback).Run(
        NativeFileSystemError::New(base::File::FILE_ERROR_NOT_FOUND), nullptr);
    return;
  }

  storage::FileSystemURL url;
  const base::File::Error file_error = GetChildURL(new_name, &url);
  if (file_error != base::File::FILE_OK) {
    std::move(callback).Run(NativeFileSystemError::New(file_error), nullptr);
    return;
  }

  if (url == source->url()) {
    std::move(callback).Run(
        NativeFileSystemError::New(base::File::FILE_ERROR_INVALID_OPERATION),
        nullptr);
    return;
  }

  auto result_callback = base::BindOnce(
      &NativeFileSystemDirectoryHandleImpl::DidCopyOrMove,
      weak_factory_.GetWeakPtr(), std::move(callback), new_name, url,
      source->type() ==
          NativeFileSystemTransferTokenImpl::HandleType::kDirectory);
  if (is_copy) {
    operation_runner()->Copy(
        source->url(), url, storage::FileSystemOperation::OPTION_NONE,
        storage::FileSystemOperation::ERROR_BEHAVIOR_ABORT,
        /*progress_callback=*/base::NullCallback(), std::move(result_callback));
  } else {
    operation_runner()->Move(source->url(), url,
                             storage::FileSystemOperation::OPTION_NONE,
                             std::move(result_callback));
  }
}

void NativeFileSystemDirectoryHandleImpl::DidCopyOrMove(
    CopyOrMoveCallback callback,
    const std::string& new_name,
    const storage::FileSystemURL& new_url,
    bool is_directory,
    base::File::Error result) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  if (result != base::File::FILE_OK) {
    std::move(callback).Run(NativeFileSystemError::New(result), nullptr);
    return;
  }

  std::move(callback).Run(NativeFileSystemError::New(base::File::FILE_OK),
                          CreateEntry(new_name, new_url, is_directory));
}

base::File::Error NativeFileSystemDirectoryHandleImpl::GetChildURL(
    const std::string& name,
    storage::FileSystemURL* result) {
  // TODO(mek): Rather than doing URL serialization and parsing we should just
  // have a way to get a child FileSystemURL directly from its parent.

  if (name.empty()) {
    return base::File::FILE_ERROR_NOT_FOUND;
  }

  if (ContainsPathSeparator(name) || IsCurrentOrParentDirectory(name)) {
    // |name| must refer to a entry that exists in this directory as a direct
    // child.
    return base::File::FILE_ERROR_SECURITY;
  }

  std::string escaped_name =
      net::EscapeQueryParamValue(name, /*use_plus=*/false);

  GURL parent_url = url().ToGURL();
  std::string path = base::StrCat({parent_url.path(), "/", escaped_name});
  GURL::Replacements replacements;
  replacements.SetPathStr(path);
  GURL child_url = parent_url.ReplaceComponents(replacements);

  *result = file_system_context()->CrackURL(child_url);
  return base::File::FILE_OK;
}

NativeFileSystemEntryPtr NativeFileSystemDirectoryHandleImpl::CreateEntry(
    const std::string& name,
    const storage::FileSystemURL& url,
    bool is_directory) {
  if (is_directory) {
    return NativeFileSystemEntry::New(
        NativeFileSystemHandle::NewDirectory(
            manager()
                ->CreateDirectoryHandle(context(), url, file_system())
                .PassInterface()),
        name);
  }
  return NativeFileSystemEntry::New(
      NativeFileSystemHandle::NewFile(
          manager()
              ->CreateFileHandle(context(), url, file_system())
              .PassInterface()),
      name);
}

}  // namespace content