//===---------------------- system_error.cpp ------------------------------===//
//
//           originally from The LLVM Compiler Infrastructure
//
// Was distributed under the University of Illinois Open Source License.
//
//===----------------------------------------------------------------------===//
//
// This was lifted from libc++ and modified for C++03.
//
//===----------------------------------------------------------------------===//

#include "SystemError.hpp"

#include "ErrnoToString.hpp"
#include "FatalError.hpp"
#include <cstring>
#include <string>

namespace akj {

// class error_category

error_category::error_category() {
}

error_category::~error_category() {
}

error_condition
error_category::default_error_condition(int ev) const {
  return error_condition(ev, *this);
}

bool
error_category::equivalent(int code, const error_condition& condition) const {
  return default_error_condition(code) == condition;
}

bool
error_category::equivalent(const error_code& code, int condition) const {
  return *this == code.category() && code.value() == condition;
}

std::string
_do_message::message(int ev) const {
  return std::string(sys::StrError(ev));
}

class _generic_error_category : public _do_message {
public:
  virtual const char* name() const;
  virtual std::string message(int ev) const;
};

const char*
_generic_error_category::name() const {
  return "generic";
}

std::string
_generic_error_category::message(int ev) const {
#ifdef ELAST
  if (ev > ELAST)
    return std::string("unspecified generic_category error");
#endif  // ELAST
  return _do_message::message(ev);
}

const error_category&
generic_category() {
  static _generic_error_category s;
  return s;
}

class _system_error_category : public _do_message {
public:
  virtual const char* name() const ;
  virtual std::string message(int ev) const ;
  virtual error_condition default_error_condition(int ev) const ;
};

const char*
_system_error_category::name() const {
  return "system";
}

// std::string _system_error_category::message(int ev) const {
// Is in Platform/system_error.inc

// error_condition _system_error_category::default_error_condition(int ev) const
// Is in Platform/system_error.inc

const error_category&
system_category() {
  static _system_error_category s;
  return s;
}

const error_category&
posix_category() {
#ifdef _WIN32
  return generic_category();
#else
  return system_category();
#endif
}

// error_condition

std::string
error_condition::message() const {
  return _cat_->message(_val_);
}

// error_code

std::string
error_code::message() const {
  return _cat_->message(_val_);
}

void error_code::assertOK()
{
	if ((*this))
	{
		FatalError::Die(message().c_str());
	}
}


} // end namespace llvm

// Include the truly platform-specific parts of this class.
#if defined(_WIN32)

#include <windows.h>
#include <winerror.h>

using namespace akj;

std::string
_system_error_category::message(int ev) const {
	LPVOID lpMsgBuf = 0;
	DWORD retval = ::FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL,
		ev,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
		(LPSTR)&lpMsgBuf,
		0,
		NULL);
	if (retval == 0) {
		::LocalFree(lpMsgBuf);
		return std::string("Unknown error");
	}

	std::string str(static_cast<LPCSTR>(lpMsgBuf));
	::LocalFree(lpMsgBuf);

	while (str.size()
		&& (str[str.size() - 1] == '\n' || str[str.size() - 1] == '\r'))
		str.erase(str.size() - 1);
	if (str.size() && str[str.size() - 1] == '.')
		str.erase(str.size() - 1);
	return str;
}

// I'd rather not double the line count of the following.
#define MAP_ERR_TO_COND(x, y) case x: return make_error_condition(errc::y)

error_condition
_system_error_category::default_error_condition(int ev) const {
	switch (ev) {
		MAP_ERR_TO_COND(0, success);
		// Windows system -> posix_errno decode table  ---------------------------//
		// see WinError.h comments for descriptions of errors
		MAP_ERR_TO_COND(ERROR_ACCESS_DENIED, permission_denied);
		MAP_ERR_TO_COND(ERROR_ALREADY_EXISTS, file_exists);
		MAP_ERR_TO_COND(ERROR_BAD_UNIT, no_such_device);
		MAP_ERR_TO_COND(ERROR_BUFFER_OVERFLOW, filename_too_long);
		MAP_ERR_TO_COND(ERROR_BUSY, device_or_resource_busy);
		MAP_ERR_TO_COND(ERROR_BUSY_DRIVE, device_or_resource_busy);
		MAP_ERR_TO_COND(ERROR_CANNOT_MAKE, permission_denied);
		MAP_ERR_TO_COND(ERROR_CANTOPEN, io_error);
		MAP_ERR_TO_COND(ERROR_CANTREAD, io_error);
		MAP_ERR_TO_COND(ERROR_CANTWRITE, io_error);
		MAP_ERR_TO_COND(ERROR_CURRENT_DIRECTORY, permission_denied);
		MAP_ERR_TO_COND(ERROR_DEV_NOT_EXIST, no_such_device);
		MAP_ERR_TO_COND(ERROR_DEVICE_IN_USE, device_or_resource_busy);
		MAP_ERR_TO_COND(ERROR_DIR_NOT_EMPTY, directory_not_empty);
		MAP_ERR_TO_COND(ERROR_DIRECTORY, invalid_argument);
		MAP_ERR_TO_COND(ERROR_DISK_FULL, no_space_on_device);
		MAP_ERR_TO_COND(ERROR_FILE_EXISTS, file_exists);
		MAP_ERR_TO_COND(ERROR_FILE_NOT_FOUND, no_such_file_or_directory);
		MAP_ERR_TO_COND(ERROR_HANDLE_DISK_FULL, no_space_on_device);
		MAP_ERR_TO_COND(ERROR_HANDLE_EOF, value_too_large);
		MAP_ERR_TO_COND(ERROR_INVALID_ACCESS, permission_denied);
		MAP_ERR_TO_COND(ERROR_INVALID_DRIVE, no_such_device);
		MAP_ERR_TO_COND(ERROR_INVALID_FUNCTION, function_not_supported);
		MAP_ERR_TO_COND(ERROR_INVALID_HANDLE, invalid_argument);
		MAP_ERR_TO_COND(ERROR_INVALID_NAME, invalid_argument);
		MAP_ERR_TO_COND(ERROR_LOCK_VIOLATION, no_lock_available);
		MAP_ERR_TO_COND(ERROR_LOCKED, no_lock_available);
		MAP_ERR_TO_COND(ERROR_NEGATIVE_SEEK, invalid_argument);
		MAP_ERR_TO_COND(ERROR_NOACCESS, permission_denied);
		MAP_ERR_TO_COND(ERROR_NOT_ENOUGH_MEMORY, not_enough_memory);
		MAP_ERR_TO_COND(ERROR_NOT_READY, resource_unavailable_try_again);
		MAP_ERR_TO_COND(ERROR_NOT_SAME_DEVICE, cross_device_link);
		MAP_ERR_TO_COND(ERROR_OPEN_FAILED, io_error);
		MAP_ERR_TO_COND(ERROR_OPEN_FILES, device_or_resource_busy);
		MAP_ERR_TO_COND(ERROR_OPERATION_ABORTED, operation_canceled);
		MAP_ERR_TO_COND(ERROR_OUTOFMEMORY, not_enough_memory);
		MAP_ERR_TO_COND(ERROR_PATH_NOT_FOUND, no_such_file_or_directory);
		MAP_ERR_TO_COND(ERROR_BAD_NETPATH, no_such_file_or_directory);
		MAP_ERR_TO_COND(ERROR_READ_FAULT, io_error);
		MAP_ERR_TO_COND(ERROR_RETRY, resource_unavailable_try_again);
		MAP_ERR_TO_COND(ERROR_SEEK, io_error);
		MAP_ERR_TO_COND(ERROR_SHARING_VIOLATION, permission_denied);
		MAP_ERR_TO_COND(ERROR_TOO_MANY_OPEN_FILES, too_many_files_open);
		MAP_ERR_TO_COND(ERROR_WRITE_FAULT, io_error);
		MAP_ERR_TO_COND(ERROR_WRITE_PROTECT, permission_denied);
		MAP_ERR_TO_COND(ERROR_SEM_TIMEOUT, timed_out);
		MAP_ERR_TO_COND(WSAEACCES, permission_denied);
		MAP_ERR_TO_COND(WSAEADDRINUSE, address_in_use);
		MAP_ERR_TO_COND(WSAEADDRNOTAVAIL, address_not_available);
		MAP_ERR_TO_COND(WSAEAFNOSUPPORT, address_family_not_supported);
		MAP_ERR_TO_COND(WSAEALREADY, connection_already_in_progress);
		MAP_ERR_TO_COND(WSAEBADF, bad_file_descriptor);
		MAP_ERR_TO_COND(WSAECONNABORTED, connection_aborted);
		MAP_ERR_TO_COND(WSAECONNREFUSED, connection_refused);
		MAP_ERR_TO_COND(WSAECONNRESET, connection_reset);
		MAP_ERR_TO_COND(WSAEDESTADDRREQ, destination_address_required);
		MAP_ERR_TO_COND(WSAEFAULT, bad_address);
		MAP_ERR_TO_COND(WSAEHOSTUNREACH, host_unreachable);
		MAP_ERR_TO_COND(WSAEINPROGRESS, operation_in_progress);
		MAP_ERR_TO_COND(WSAEINTR, interrupted);
		MAP_ERR_TO_COND(WSAEINVAL, invalid_argument);
		MAP_ERR_TO_COND(WSAEISCONN, already_connected);
		MAP_ERR_TO_COND(WSAEMFILE, too_many_files_open);
		MAP_ERR_TO_COND(WSAEMSGSIZE, message_size);
		MAP_ERR_TO_COND(WSAENAMETOOLONG, filename_too_long);
		MAP_ERR_TO_COND(WSAENETDOWN, network_down);
		MAP_ERR_TO_COND(WSAENETRESET, network_reset);
		MAP_ERR_TO_COND(WSAENETUNREACH, network_unreachable);
		MAP_ERR_TO_COND(WSAENOBUFS, no_buffer_space);
		MAP_ERR_TO_COND(WSAENOPROTOOPT, no_protocol_option);
		MAP_ERR_TO_COND(WSAENOTCONN, not_connected);
		MAP_ERR_TO_COND(WSAENOTSOCK, not_a_socket);
		MAP_ERR_TO_COND(WSAEOPNOTSUPP, operation_not_supported);
		MAP_ERR_TO_COND(WSAEPROTONOSUPPORT, protocol_not_supported);
		MAP_ERR_TO_COND(WSAEPROTOTYPE, wrong_protocol_type);
		MAP_ERR_TO_COND(WSAETIMEDOUT, timed_out);
		MAP_ERR_TO_COND(WSAEWOULDBLOCK, operation_would_block);
	default: return error_condition(ev, system_category());
	}
}



#else

using namespace akj;

std::string
_system_error_category::message(int ev) const {
	return _do_message::message(ev);
}

error_condition
_system_error_category::default_error_condition(int ev) const {
#ifdef ELAST
	if (ev > ELAST)
		return error_condition(ev, system_category());
#endif  // ELAST
	return error_condition(ev, generic_category());
}

#endif
